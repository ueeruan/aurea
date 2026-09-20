import 'dart:async';
import 'dart:io';
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart'
    show WidgetsBinding, WidgetsBindingObserver;

/// IMAGENS QUE VESTEM OBJETOS 3D, decodificadas uma vez e guardadas.
///
/// O pintor pede a imagem de forma SINCRONA no meio do paint: se ela ja
/// esta aqui, pinta com ela; se nao, pinta a cor lisa e dispara a
/// leitura. Quando a leitura termina, [revision] muda e quem escuta
/// repinta — a textura "chega" sem ninguem ter de esperar por ela.
///
/// Decodifica no maximo a 1024 px de largura: e textura de face, nao
/// foto de galeria, e cada face em tela raramente passa de algumas
/// centenas de pixels.
class TextureCache with WidgetsBindingObserver {
  TextureCache._();
  bool _observing = false;

  void observeMemoryPressure(WidgetsBinding binding) {
    if (_observing) return;
    _observing = true;
    binding.addObserver(this);
  }

  static final TextureCache instance = TextureCache._();

  /// Sobe a cada imagem que termina de carregar.
  final ValueNotifier<int> revision = ValueNotifier<int>(0);

  final _images = <String, ui.Image>{};
  static const int maxBytes = 64 * 1024 * 1024;
  int _bytes = 0;
  int _generation = 0;
  Future<void> _queue = Future<void>.value();
  int get decodedBytes => _bytes;
  int get entryCount => _images.length;
  final Set<String> _failed = {};
  final Map<String, Future<void>> _pending = {};

  /// A imagem de [path], ou null se ainda nao chegou (ou falhou).
  ui.Image? imageFor(String path) {
    final img = _images.remove(path);
    if (img != null) {
      _images[path] = img;
      return img;
    }
    if (!_failed.contains(path)) {
      unawaited(_loadOnce(path));
    }
    return null;
  }

  /// Decodifica [path] e so conclui quando a imagem pode ser usada pelo
  /// pintor. Chamadas concorrentes compartilham a mesma leitura.
  Future<bool> prepare(String path) async {
    if (path.isEmpty) return false;
    if (_images.containsKey(path)) return true;
    if (_failed.contains(path)) return false;
    await _loadOnce(path);
    return _images.containsKey(path);
  }

  // ==================== OS MESMOS PIXELS, EM BYTES =====================
  //
  // O MOTOR NAO ACEITA `ui.Image`: a porta do 3D quer RGBA8 cru para copiar
  // para a placa. E `toByteData` e ASSINCRONO, enquanto quem monta a malha
  // (`malhasCruas3DDe`) e sincrono, no meio da construcao do quadro.
  //
  // ENTAO O CAMINHO E O MESMO DO PINTOR: quem pede recebe o que ja esta
  // aqui, ou nulo; o nulo dispara a leitura, e quando ela termina o
  // [revision] muda e o palco remonta a malha — agora com o mapa. O modelo
  // aparece sem textura por um quadro e vestido no seguinte, que e o que o
  // dono ja ve para a textura de cor no pintor.
  //
  // O TETO E SEPARADO do [maxBytes] das imagens porque o que mora aqui e
  // outra coisa: um mapa de 1024 sao 4 MB, e sao os cinco mapas de cada
  // material do modelo. Sem teto, importar tres modelos texturizados numa
  // sessao segurava meio giga de RGBA que ninguem mais desenhava.
  final _rgba = <String, Uint8List>{};
  final _tamanhos = <String, (int, int)>{};
  static const int maxBytesRgba = 96 * 1024 * 1024;
  int _bytesRgba = 0;
  final Map<String, Future<void>> _pendingRgba = {};
  int get rgbaBytes => _bytesRgba;

  /// OS PIXELS DE [path] EM RGBA8, ou nulo enquanto eles nao chegam.
  ///
  /// Devolve `(pixels, largura, altura)`. Nulo NAO e erro: e "ainda nao" —
  /// a leitura foi disparada e [revision] avisa quando terminar.
  (Uint8List, int, int)? rgbaFor(String path) {
    if (path.isEmpty) return null;
    final pronto = _rgba.remove(path);
    if (pronto != null) {
      // REINSERIR PARA O FIM: o mapa e a fila do descarte, e o que se usa
      // agora nao pode ser o primeiro a sair.
      _rgba[path] = pronto;
      final tamanho = _tamanhos[path]!;
      return (pronto, tamanho.$1, tamanho.$2);
    }
    if (!_failed.contains(path)) unawaited(_loadRgbaOnce(path));
    return null;
  }

  /// Decodifica [path] para RGBA8 e so conclui quando ele pode ser usado
  /// pelo motor. Chamadas concorrentes dividem a mesma leitura.
  Future<bool> prepareRgba(String path) async {
    if (path.isEmpty) return false;
    if (_rgba.containsKey(path)) return true;
    if (_failed.contains(path)) return false;
    await _loadRgbaOnce(path);
    return _rgba.containsKey(path);
  }

  Future<void> _loadRgbaOnce(String path) {
    final running = _pendingRgba[path];
    if (running != null) return running;
    final generation = _generation;
    final future = _queue.then((_) => _loadRgba(path, generation));
    _queue = future;
    _pendingRgba[path] = future;
    return future.whenComplete(() {
      if (identical(_pendingRgba[path], future)) _pendingRgba.remove(path);
    });
  }

  Future<void> _loadRgba(String path, int generation) async {
    if (generation != _generation || _rgba.containsKey(path)) return;
    try {
      // A IMAGEM JA DECODIFICADA E REAPROVEITADA. Quando o pintor de CPU ja
      // pediu este mesmo caminho, o PNG nao e decodificado duas vezes: sai
      // do `ui.Image` que esta aqui, que ja veio limitado a 1024.
      final pronta = _images[path];
      var imagem = pronta;
      if (imagem == null) {
        await _load(path, generation);
        if (generation != _generation) return;
        imagem = _images[path];
        // A IMAGEM PODE TER SIDO DESCARTADA pelo teto do cache entre o
        // `_load` e esta linha. Sem esta conferencia, `toByteData` de uma
        // imagem ja liberada estoura dentro do motor de render.
        if (imagem == null) {
          if (_failed.length >= 128) _failed.remove(_failed.first);
          _failed.add(path);
          return;
        }
      }
      final dados = await imagem.toByteData(
        format: ui.ImageByteFormat.rawRgba,
      );
      if (dados == null || generation != _generation) return;
      putRgba(
        path,
        dados.buffer.asUint8List(),
        imagem.width,
        imagem.height,
      );
    } catch (_) {
      if (generation == _generation) {
        if (_failed.length >= 128) _failed.remove(_failed.first);
        _failed.add(path);
      }
    }
  }

  /// Guarda pixels prontos. Para testes e para quem ja decodificou.
  void putRgba(String path, Uint8List pixels, int largura, int altura) {
    final anterior = _rgba.remove(path);
    if (anterior != null) _bytesRgba -= anterior.length;
    _rgba[path] = pixels;
    _tamanhos[path] = (largura, altura);
    _bytesRgba += pixels.length;
    while (_bytesRgba > maxBytesRgba && _rgba.length > 1) {
      final velho = _rgba.keys.first;
      _bytesRgba -= _rgba.remove(velho)!.length;
      _tamanhos.remove(velho);
    }
    _failed.remove(path);
    revision.value++;
  }

  /// Para testes e para trocar a imagem de um caminho reaproveitado.
  void put(String path, ui.Image image) {
    final previous = _images.remove(path);
    if (previous != null) {
      _bytes -= previous.width * previous.height * 4;
      if (!identical(previous, image)) previous.dispose();
    }
    _images[path] = image;
    _bytes += image.width * image.height * 4;
    while (_bytes > maxBytes && _images.isNotEmpty) {
      final oldest = _images.remove(_images.keys.first)!;
      _bytes -= oldest.width * oldest.height * 4;
      oldest.dispose();
    }
    _failed.remove(path);
    revision.value++;
  }

  Future<void> _loadOnce(String path) {
    final running = _pending[path];
    if (running != null) return running;
    final generation = _generation;
    // One decode at a time bounds transient compressed + RGBA allocations.
    final future = _queue.then((_) => _load(path, generation));
    _queue = future;
    _pending[path] = future;
    return future.whenComplete(() {
      if (identical(_pending[path], future)) _pending.remove(path);
    });
  }

  Future<void> _load(String path, int generation) async {
    if (generation != _generation || _images.containsKey(path)) return;
    ui.ImmutableBuffer? buffer;
    ui.ImageDescriptor? descriptor;
    ui.Codec? codec;
    try {
      final bytes = path.startsWith('data:')
          ? UriData.parse(path).contentAsBytes()
          : await File(path).readAsBytes();
      buffer = await ui.ImmutableBuffer.fromUint8List(bytes);
      descriptor = await ui.ImageDescriptor.encoded(buffer);
      final scale = math.min(
        1.0,
        1024 / math.max(descriptor.width, descriptor.height),
      );
      codec = await descriptor.instantiateCodec(
        targetWidth: math.max(1, (descriptor.width * scale).round()),
        targetHeight: math.max(1, (descriptor.height * scale).round()),
      );
      final frame = await codec.getNextFrame();
      if (generation != _generation) {
        frame.image.dispose();
      } else {
        put(path, frame.image);
      }
    } catch (_) {
      if (generation == _generation) {
        if (_failed.length >= 128) _failed.remove(_failed.first);
        _failed.add(path);
      }
    } finally {
      codec?.dispose();
      descriptor?.dispose();
      buffer?.dispose();
    }
  }

  void clear() {
    _generation++;
    _pending.clear();
    _pendingRgba.clear();
    for (final img in _images.values) {
      img.dispose();
    }
    _images.clear();
    _bytes = 0;
    _rgba.clear();
    _tamanhos.clear();
    _bytesRgba = 0;
    _failed.clear();
  }

  @override
  void didHaveMemoryPressure() => clear();
}
