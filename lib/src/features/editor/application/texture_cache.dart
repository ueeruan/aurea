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
    for (final img in _images.values) {
      img.dispose();
    }
    _images.clear();
    _bytes = 0;
    _failed.clear();
  }

  @override
  void didHaveMemoryPressure() => clear();
}
