/// O NUCLEO NATIVO DO AUREA — trabalho pesado em C++ atras da UI Flutter.
///
/// Primeira fatia: o codificador de video do Android (MediaCodec pelo NDK),
/// alimentado por FFI com o ponteiro do quadro, sem canal de plataforma.
library;

import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

@Native<Int32 Function()>(symbol: 'aurea_core_versao', isLeaf: true)
external int _versao();

@Native<Int32 Function(Pointer<Uint8>, Int32, Int32, Pointer<Uint8>)>(
  symbol: 'aurea_core_rgba_para_nv12',
  isLeaf: true,
)
external int _rgbaParaNv12(
  Pointer<Uint8> rgba,
  int w,
  int h,
  Pointer<Uint8> out,
);

@Native<Int32 Function()>(
  symbol: 'aurea_core_codificador_disponivel',
  isLeaf: true,
)
external int _disponivel();

@Native<Int32 Function(Pointer<Utf8>, Int32, Int32, Int32, Int32, Int32)>(
  symbol: 'aurea_core_codificador_abrir',
)
external int _abrir(
  Pointer<Utf8> caminho,
  int w,
  int h,
  int fps,
  int bitrate,
  int hevc,
);

// Nao e folha: pode esperar a fila do codificador (dois quadros).
@Native<Int32 Function(Pointer<Uint8>, Int32, Int32)>(
  symbol: 'aurea_core_codificador_quadro',
)
external int _quadro(Pointer<Uint8> rgba, int w, int h);

@Native<Int32 Function()>(symbol: 'aurea_core_codificador_terminar')
external int _terminar();

@Native<Void Function()>(symbol: 'aurea_core_codificador_cancelar')
external void _cancelar();

/// Se a biblioteca do nucleo carregou.
bool get nucleoCarregado {
  try {
    return _versao() >= 1;
  } catch (_) {
    return false;
  }
}

/// RGBA -> NV12 BT.709 (a conta do codificador), para teste.
Uint8List? rgbaParaNv12(Uint8List rgba, int w, int h) {
  final src = calloc<Uint8>(rgba.length);
  final out = calloc<Uint8>(w * h * 3 ~/ 2);
  try {
    src.asTypedList(rgba.length).setAll(0, rgba);
    if (_rgbaParaNv12(src, w, h, out) == 0) return null;
    return Uint8List.fromList(out.asTypedList(w * h * 3 ~/ 2));
  } finally {
    calloc
      ..free(src)
      ..free(out);
  }
}

/// O codificador de video do nucleo.
abstract final class CodificadorNativo {
  static bool? _disp;

  /// Existe nesta plataforma (hoje: Android) e a biblioteca carregou.
  static bool get disponivel {
    if (_disp != null) return _disp!;
    try {
      _disp = _disponivel() == 1;
    } catch (_) {
      _disp = false;
    }
    return _disp!;
  }

  static Pointer<Uint8>? _buffer;
  static int _capacidade = 0;

  static bool abrir({
    required String caminho,
    required int largura,
    required int altura,
    required int fps,
    required int bitrate,
    bool hevc = false,
  }) {
    if (!disponivel) return false;
    final c = caminho.toNativeUtf8();
    try {
      return _abrir(c, largura, altura, fps, bitrate, hevc ? 1 : 0) == 1;
    } finally {
      calloc.free(c);
    }
  }

  /// Entrega um quadro RGBA. O nucleo copia e volta; espera so com dois
  /// quadros ja na fila.
  static bool quadro(Uint8List rgba, int largura, int altura) {
    if (_capacidade < rgba.length) {
      if (_buffer != null) calloc.free(_buffer!);
      _buffer = calloc<Uint8>(rgba.length);
      _capacidade = rgba.length;
    }
    _buffer!.asTypedList(rgba.length).setAll(0, rgba);
    return _quadro(_buffer!, largura, altura) == 1;
  }

  static bool terminar() {
    final ok = _terminar() == 1;
    _soltarBuffer();
    return ok;
  }

  static void cancelar() {
    try {
      _cancelar();
    } catch (_) {}
    _soltarBuffer();
  }

  static void _soltarBuffer() {
    if (_buffer != null) calloc.free(_buffer!);
    _buffer = null;
    _capacidade = 0;
  }
}
