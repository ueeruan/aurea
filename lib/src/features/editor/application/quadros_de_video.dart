import 'dart:async';
import 'dart:collection';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:ffmpeg_kit_flutter_new_full/ffmpeg_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/return_code.dart';
import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

/// Bounded temporal preview cache: one-second blocks, one extraction and
/// at most two image decodes. No whole-clip extraction during playback.
class QuadrosDeVideo {
  QuadrosDeVideo._();
  static final instance = QuadrosDeVideo._();
  static const fps = 30;
  static const altura = 360;
  static const _tetoDeImagens = 64;
  static const _tetoDeBlocos = 8;
  final ValueNotifier<int> revision = ValueNotifier(0);
  final LinkedHashMap<String, _Bloco> _blocos = LinkedHashMap();
  final LinkedHashMap<String, ui.Image> _imagens = LinkedHashMap();
  final LinkedHashMap<String, (String, int)> _pedidos = LinkedHashMap();
  final Set<String> _decodificando = {};
  final Set<String> _decodesFalhos = {};
  final Map<String, DateTime> _falhas = {};
  String? _extracaoAtual;
  int _geracao = 0;
  Timer? _notificacao;

  @visibleForTesting
  static bool desligado = false;
  @visibleForTesting
  int get imagensEmCache => _imagens.length;
  @visibleForTesting
  int get extracoesPendentes => _pedidos.length;

  void _avisar() {
    _notificacao ??= Timer(const Duration(milliseconds: 16), () {
      _notificacao = null;
      revision.value++;
    });
  }

  /// Borrowed reference; retained renderers own a clone so LRU eviction
  /// cannot dispose a frame still displayed by a paused composition.
  ui.Image? quadro(String arquivo, Duration fonte, {bool prefetch = true}) {
    if (desligado || arquivo.isEmpty) return null;
    final us = fonte.inMicroseconds.clamp(0, 1 << 52);
    // Round-trip Duration values such as 33,333 us must keep frame 1.
    final frame = ((us + .5) * fps / 1000000).floor();
    final segundo = frame ~/ fps;
    final chave = '$arquivo@$segundo';
    final bloco = _blocos.remove(chave);
    if (bloco == null) {
      _pedir(arquivo, segundo);
      return null;
    }
    _blocos[chave] = bloco;
    final indice = (frame % fps).clamp(
      0,
      bloco.contagem - 1,
    );
    final frameKey = '${bloco.pasta.path}/$indice';
    final img = _imagens.remove(frameKey);
    if (img != null) {
      _imagens[frameKey] = img;
      if (prefetch) _adiantar(arquivo, frame);
      return img;
    }
    if (_decodificando.length < 2 && !_decodificando.contains(frameKey) &&
        !_decodesFalhos.contains(frameKey)) {
      _decodificar(frameKey, bloco, indice);
    }
    return null;
  }

  void _adiantar(String arquivo, int atual) {
    // Request the next second before crossing its boundary, while the
    // current block remains visible. Decode only a small look-ahead window.
    if (atual % fps >= fps ~/ 2 && !_blocos.containsKey('$arquivo@${atual ~/ fps + 1}')) {
      _pedir(arquivo, atual ~/ fps + 1);
    }
    for (var i = 1; i <= 6 && _decodificando.length < 2; i++) {
      final frame = atual + i;
      final bloco = _blocos['$arquivo@${frame ~/ fps}'];
      if (bloco == null) continue;
      final indice = (frame % fps).clamp(0, bloco.contagem - 1);
      final key = '${bloco.pasta.path}/$indice';
      if (!_imagens.containsKey(key) && !_decodificando.contains(key) && !_decodesFalhos.contains(key)) {
        _decodificar(key, bloco, indice);
      }
    }
  }

  void antecipar(String arquivo, Duration fonte) =>
      _adiantar(arquivo, ((fonte.inMicroseconds + .5) * fps / 1000000).floor());

  /// Legacy callers declare a range here; extraction is driven only by
  /// requested timestamps in quadro(), never by the entire range.
  void preparar(String arquivo, Duration inicio, Duration fim) {}

  void _pedir(String arquivo, int segundo) {
    final chave = '$arquivo@$segundo';
    if (_extracaoAtual == chave) return;
    final falhou = _falhas[chave];
    if (falhou != null && DateTime.now().difference(falhou).inSeconds < 10) {
      return;
    }
    _pedidos.remove(chave);
    _pedidos[chave] = (arquivo, segundo);
    while (_pedidos.length > 6) {
      _pedidos.remove(_pedidos.keys.first);
    }
    if (_extracaoAtual == null) unawaited(_extrair());
  }

  Future<void> _extrair() async {
    if (_extracaoAtual != null || _pedidos.isEmpty) return;
    final chave = _pedidos.keys.first;
    _extracaoAtual = chave;
    final geracao = _geracao;
    final (arquivo, segundo) = _pedidos.remove(chave)!;
    try {
      final tmp = await getTemporaryDirectory();
      final raiz = await Directory('${tmp.path}/quadros_temporais')
          .create(recursive: true);
      final pasta = await raiz.createTemp('bloco_');
      final sessao = await FFmpegKit.executeWithArguments([
        '-y',
        '-v',
        'error',
        '-nostats',
        '-threads',
        '1',
        '-ss',
        '$segundo',
        '-i',
        arquivo,
        '-t',
        '1',
        '-an',
        '-sn',
        '-filter_threads',
        '1',
        '-vf',
        "fps=$fps,scale=w='min(360,iw)':h='min(360,ih)':force_original_aspect_ratio=decrease",
        '-threads',
        '1',
        '-q:v',
        '4',
        '${pasta.path}/%03d.jpg',
      ]);
      final ok = ReturnCode.isSuccess(await sessao.getReturnCode());
      var contagem = 0;
      if (ok) {
        await for (final f in pasta.list()) {
          if (f is File && f.path.endsWith('.jpg')) contagem++;
        }
      }
      if (geracao != _geracao || contagem == 0) {
        if (geracao == _geracao) {
          _falhas[chave] = DateTime.now();
          debugPrint(
            'Quadros temporais: falha em $segundo s: ${await sessao.getFailStackTrace()}',
          );
        }
        await pasta.delete(recursive: true);
      } else {
        _blocos[chave] = _Bloco(pasta, contagem);
        _pedidos.remove(chave);
        while (_blocos.length > _tetoDeBlocos) {
          final antigo = _blocos.remove(_blocos.keys.first)!;
          _decodesFalhos.removeWhere((k) => k.startsWith('${antigo.pasta.path}/'));
          for (final k
              in _imagens.keys
                  .where((k) => k.startsWith('${antigo.pasta.path}/'))
                  .toList()) {
            _imagens.remove(k)?.dispose();
          }
          await antigo.pasta.delete(recursive: true);
        }
        _avisar();
      }
    } catch (e, st) {
      _falhas[chave] = DateTime.now();
      debugPrint('Quadros temporais: $e\n$st');
    } finally {
      _extracaoAtual = null;
      if (_pedidos.isNotEmpty) unawaited(_extrair());
    }
  }

  void _decodificar(String chave, _Bloco bloco, int indice) {
    _decodificando.add(chave);
    final geracao = _geracao;
    unawaited(() async {
      ui.Codec? codec;
      var pronto = false;
      try {
        final file = File(
          '${bloco.pasta.path}/${(indice + 1).toString().padLeft(3, '0')}.jpg',
        );
        codec = await ui.instantiateImageCodec(await file.readAsBytes());
        final frame = await codec.getNextFrame();
        if (geracao != _geracao || !_blocos.containsValue(bloco)) {
          frame.image.dispose();
          return;
        }
        _imagens.remove(chave)?.dispose();
        _imagens[chave] = frame.image;
        pronto = true;
        while (_imagens.length > _tetoDeImagens) {
          _imagens.remove(_imagens.keys.first)?.dispose();
        }
      } catch (e) {
        if (geracao == _geracao && _blocos.containsValue(bloco)) {
          _decodesFalhos.add(chave);
        }
        debugPrint('Decode temporal: $e');
      } finally {
        codec?.dispose();
        _decodificando.remove(chave);
        if (pronto && geracao == _geracao) _avisar();
      }
    }());
  }

  @visibleForTesting
  void limpar() {
    _geracao++;
    _notificacao?.cancel();
    _notificacao = null;
    _pedidos.clear();
    for (final bloco in _blocos.values) {
      unawaited(bloco.pasta.delete(recursive: true));
    }
    _blocos.clear();
    for (final img in _imagens.values) {
      img.dispose();
    }
    _imagens.clear();
    _falhas.clear();
    _decodesFalhos.clear();
  }
}

class _Bloco {
  const _Bloco(this.pasta, this.contagem);
  final Directory pasta;
  final int contagem;
}
