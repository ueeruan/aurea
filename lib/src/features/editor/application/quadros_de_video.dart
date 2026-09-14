import 'dart:async';
import 'dart:collection';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:ffmpeg_kit_flutter_new_full/ffmpeg_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/return_code.dart';
import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

/// QUADROS DE VIDEO DE OUTRO INSTANTE, para a previa.
///
/// O tocador de video so mostra UM quadro: o de agora. Time Slice,
/// Posterize Time e Echo precisam do mesmo video em outros instantes —
/// sem isto, as doze faixas de um Time Slice mostravam o mesmo quadro.
///
/// A primeira vez que um efeito pede, o trecho usado do arquivo (com
/// folga de alguns segundos) e extraido pelo FFmpeg em JPEG pequeno
/// (360 px, 30 quadros por segundo). Os quadros pedidos sao decodificados
/// sob demanda num LRU; enquanto nao chegam, quem pediu desenha o quadro
/// ao vivo — o efeito nunca some.
///
/// A exportacao nao passa por aqui: ela ja tem todos os quadros do clipe
/// em PNG no tamanho de saida e decodifica os de outro instante antes de
/// desenhar.
class QuadrosDeVideo {
  QuadrosDeVideo._();
  static final instance = QuadrosDeVideo._();

  static const fps = 30;
  static const altura = 360;
  static const _tetoDeImagens = 48;
  static const _folga = Duration(seconds: 3);

  final ValueNotifier<int> revision = ValueNotifier(0);

  final Map<String, _Extracao> _extracoes = {};
  final LinkedHashMap<String, ui.Image> _imagens = LinkedHashMap();
  final Set<String> _decodificando = {};

  /// Desligado (teste, ou aparelho sem FFmpeg): nunca extrai nada.
  @visibleForTesting
  static bool desligado = false;

  /// O quadro de [arquivo] no instante [fonte] do arquivo, se ja estiver
  /// na memoria. Se nao estiver e o trecho ja foi extraido, pede a
  /// decodificacao e devolve null (quem pediu escuta [revision]).
  ui.Image? quadro(String arquivo, Duration fonte) {
    final e = _extracoes[arquivo];
    if (e == null || e.estado != _Estado.pronta || e.contagem == 0) return null;
    if (fonte < e.inicio - const Duration(milliseconds: 40) ||
        fonte > e.fim + const Duration(milliseconds: 40)) {
      return null;
    }
    final indice = ((fonte - e.inicio).inMicroseconds * fps / 1e6)
        .round()
        .clamp(0, e.contagem - 1);
    final chave = '$arquivo@$indice';
    final img = _imagens.remove(chave);
    if (img != null) {
      _imagens[chave] = img;
      return img;
    }
    _decodificar(
      chave,
      File('${e.pasta}/${(indice + 1).toString().padLeft(6, '0')}.jpg'),
    );
    return null;
  }

  /// Garante que [inicio]..[fim] do arquivo esteja extraido. Idempotente:
  /// pedir de novo um trecho coberto nao faz nada; uma falha so tenta de
  /// novo depois de meio minuto.
  void preparar(String arquivo, Duration inicio, Duration fim) {
    if (desligado || arquivo.isEmpty) return;
    final atual = _extracoes[arquivo];
    final de = inicio - _folga < Duration.zero ? Duration.zero : inicio - _folga;
    final ate = fim + _folga;
    if (atual != null) {
      if (atual.estado == _Estado.extraindo) return;
      if (atual.estado == _Estado.falhou &&
          DateTime.now().difference(atual.quando) <
              const Duration(seconds: 30)) {
        return;
      }
      if (atual.estado == _Estado.pronta &&
          atual.inicio <= de &&
          atual.fim >= fim) {
        return;
      }
    }
    final nova = _Extracao(de, ate);
    _extracoes[arquivo] = nova;
    unawaited(_extrair(arquivo, nova));
  }

  Future<void> _extrair(String arquivo, _Extracao e) async {
    try {
      final tmp = await getTemporaryDirectory();
      final pasta = Directory(
        '${tmp.path}/quadros/${arquivo.hashCode.toRadixString(16)}_'
        '${e.inicio.inMilliseconds}_${e.fim.inMilliseconds}',
      );
      if (pasta.existsSync()) pasta.deleteSync(recursive: true);
      pasta.createSync(recursive: true);
      final sessao = await FFmpegKit.executeWithArguments([
        '-y',
        '-v',
        'error',
        '-nostats',
        '-ss',
        (e.inicio.inMicroseconds / 1e6).toStringAsFixed(3),
        '-i',
        arquivo,
        '-t',
        ((e.fim - e.inicio).inMicroseconds / 1e6).toStringAsFixed(3),
        '-an',
        '-vf',
        'fps=$fps,scale=-2:$altura',
        '-q:v',
        '5',
        '${pasta.path}/%06d.jpg',
      ]);
      final ok = ReturnCode.isSuccess(await sessao.getReturnCode());
      final contagem = ok
          ? pasta
                .listSync()
                .whereType<File>()
                .where((f) => f.path.endsWith('.jpg'))
                .length
          : 0;
      if (!identical(_extracoes[arquivo], e)) return;
      if (contagem == 0) {
        e.estado = _Estado.falhou;
        e.quando = DateTime.now();
        return;
      }
      e
        ..pasta = pasta.path
        ..contagem = contagem
        ..estado = _Estado.pronta;
      revision.value++;
    } catch (_) {
      e
        ..estado = _Estado.falhou
        ..quando = DateTime.now();
    }
  }

  void _decodificar(String chave, File arquivo) {
    if (_decodificando.contains(chave)) return;
    _decodificando.add(chave);
    unawaited(() async {
      try {
        final bytes = await arquivo.readAsBytes();
        final codec = await ui.instantiateImageCodec(bytes);
        final quadro = await codec.getNextFrame();
        codec.dispose();
        _imagens[chave] = quadro.image;
        // A imagem expulsa NAO e liberada aqui: ela pode estar num
        // RawImage da arvore atual. O coletor a solta quando ninguem
        // mais a segurar.
        while (_imagens.length > _tetoDeImagens) {
          _imagens.remove(_imagens.keys.first);
        }
        revision.value++;
      } catch (_) {
        // Arquivo sumiu (limpeza do sistema): a extracao daquele video e
        // esquecida e refeita no proximo pedido.
        _extracoes.remove(chave.substring(0, chave.lastIndexOf('@')));
      } finally {
        _decodificando.remove(chave);
      }
    }());
  }

  @visibleForTesting
  void limpar() {
    _extracoes.clear();
    _imagens.clear();
    _decodificando.clear();
  }
}

enum _Estado { extraindo, pronta, falhou }

class _Extracao {
  _Extracao(this.inicio, this.fim);
  final Duration inicio;
  final Duration fim;
  _Estado estado = _Estado.extraindo;
  DateTime quando = DateTime.now();
  String? pasta;
  int contagem = 0;
}
