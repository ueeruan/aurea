// PONTE FFI PARA native/enhance (libaurea_enhance, Real-ESRGAN sobre ncnn).
//
// Carregamento explicito e com estado verdadeiro: se a biblioteca ou o
// modelo nao carregam, [NativeEnhancer.open] devolve o motivo — a tela
// nunca mostra "IA ativada" com o modelo fora do ar.
import 'dart:ffi';
import 'dart:io';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import '../../editor/domain/aprimoramento_ia.dart';

final class _AeInfo extends Struct {
  @Int32()
  external int gpu;
  @Int32()
  external int fp16;
  @Int32()
  external int tile;
  @Int32()
  external int modelScale;
  @Int64()
  external int heapBudgetMb;
  @Array(128)
  external Array<Uint8> device;
}

const aeOk = 0;
const aeErrCancelled = -3;
const aeErrIo = -5;

/// Onde a biblioteca mora: Android empacota pelo CMake do app; no host,
/// os testes apontam AUREA_ENHANCE_LIB para a DLL construida localmente.
/// A mesma biblioteca traz o RIFE (native_interpolator.dart).
DynamicLibrary? abrirBibliotecaDeIa() => _abrirBiblioteca();

DynamicLibrary? _abrirBiblioteca() {
  try {
    final host = Platform.environment['AUREA_ENHANCE_LIB'];
    if (host != null && host.isNotEmpty) return DynamicLibrary.open(host);
    if (Platform.isAndroid) return DynamicLibrary.open('libaurea_enhance.so');
  } catch (_) {}
  return null;
}

class EnhancerInfo {
  const EnhancerInfo(this.gpu, this.fp16, this.tile, this.heapBudgetMb, this.device);
  final bool gpu, fp16;
  final int tile, heapBudgetMb;
  final String device;
}

/// Um motor carregado. Use num isolate de trabalho: [process] bloqueia.
class NativeEnhancer {
  NativeEnhancer._(this._lib, this._engine)
    : _process = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Int32, Int32, Int32, Float, Pointer<Uint8>, Pointer<Int32>),
        int Function(Pointer<Void>, Pointer<Uint8>, int, int, int, double, Pointer<Uint8>, Pointer<Int32>)
      >('ae_process'),
      _destroy = _lib.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>('ae_destroy'),
      _resize = _lib.lookupFunction<
        Int32 Function(Pointer<Uint8>, Int32, Int32, Pointer<Uint8>, Int32, Int32),
        int Function(Pointer<Uint8>, int, int, Pointer<Uint8>, int, int)
      >('ae_resize');

  final DynamicLibrary _lib;
  Pointer<Void> _engine;
  final int Function(Pointer<Void>, Pointer<Uint8>, int, int, int, double, Pointer<Uint8>, Pointer<Int32>) _process;
  final void Function(Pointer<Void>) _destroy;
  final int Function(Pointer<Uint8>, int, int, Pointer<Uint8>, int, int) _resize;

  /// Cancelamento lido pelo C++ entre tiles.
  final Pointer<Int32> cancel = calloc<Int32>();

  static bool get libraryAvailable => _abrirBiblioteca() != null;

  /// A biblioteca tem o caminho de PNG da exportacao do editor (uma build
  /// antiga so tinha o de buffers).
  static bool get pngAvailable {
    final lib = _abrirBiblioteca();
    return lib != null && lib.providesSymbol('ae_process_png');
  }

  /// O motor do [perfil] a partir de uma pasta de modelo: `x4.param` e
  /// `x4.bin` e, no video real, `x4-wdn.bin` — misturado na proporcao
  /// [reducaoDeRuido] (1 = so o modelo que limpa; 0 = so o que preserva
  /// o grao).
  static NativeEnhancer openProfile(
    String modelDir,
    PerfilDoAprimoramento perfil, {
    double reducaoDeRuido = reducaoDeRuidoPadrao,
    bool gpu = true,
  }) => perfil == PerfilDoAprimoramento.animacao
      ? open(modelDir, gpu: gpu)
      : openMixed(
          param: '$modelDir/x4.param',
          binA: '$modelDir/x4.bin',
          binB: '$modelDir/x4-wdn.bin',
          weightA: reducaoDeRuido,
          gpu: gpu,
        );

  /// DNI: pesos = [weightA] * A + (1 - [weightA]) * B, com o mesmo param
  /// (ae_create_dni). Lanca StateError com o motivo real.
  static NativeEnhancer openMixed({
    required String param,
    required String binA,
    required String binB,
    required double weightA,
    bool gpu = true,
  }) {
    final lib = _abrirBiblioteca();
    if (lib == null || !lib.providesSymbol('ae_create_dni')) {
      throw StateError('Motor de IA indisponível neste aparelho');
    }
    if (!weightA.isFinite || weightA < 0 || weightA > 1) {
      throw StateError('Redução de ruído fora de 0..1');
    }
    final create = lib.lookupFunction<
      Pointer<Void> Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, Float, Int32, Int32, Pointer<Utf8>, Int32),
      Pointer<Void> Function(Pointer<Utf8>, Pointer<Utf8>, Pointer<Utf8>, double, int, int, Pointer<Utf8>, int)
    >('ae_create_dni');
    final p = param.toNativeUtf8(), a = binA.toNativeUtf8(), b = binB.toNativeUtf8();
    final err = calloc<Uint8>(256).cast<Utf8>();
    try {
      final engine = create(p, a, b, weightA, 4, gpu ? 1 : 0, err, 256);
      if (engine == nullptr) {
        throw StateError('Modelo de IA não carregou: ${err.toDartString()}');
      }
      return NativeEnhancer._(lib, engine);
    } finally {
      calloc.free(p);
      calloc.free(a);
      calloc.free(b);
      calloc.free(err);
    }
  }

  /// Carrega o modelo x4 de [modelDir] (x4.param/x4.bin). Devolve o motor
  /// ou lanca StateError com o motivo real.
  static NativeEnhancer open(String modelDir, {bool gpu = true}) {
    final lib = _abrirBiblioteca();
    if (lib == null) {
      throw StateError('Motor de IA indisponível neste aparelho');
    }
    final create = lib.lookupFunction<
      Pointer<Void> Function(Pointer<Utf8>, Pointer<Utf8>, Int32, Int32, Pointer<Utf8>, Int32),
      Pointer<Void> Function(Pointer<Utf8>, Pointer<Utf8>, int, int, Pointer<Utf8>, int)
    >('ae_create');
    final param = '$modelDir/x4.param'.toNativeUtf8();
    final bin = '$modelDir/x4.bin'.toNativeUtf8();
    final err = calloc<Uint8>(256).cast<Utf8>();
    try {
      final engine = create(param, bin, 4, gpu ? 1 : 0, err, 256);
      if (engine == nullptr) {
        throw StateError('Modelo de IA não carregou: ${err.toDartString()}');
      }
      return NativeEnhancer._(lib, engine);
    } finally {
      calloc.free(param);
      calloc.free(bin);
      calloc.free(err);
    }
  }

  EnhancerInfo get info {
    final get = _lib.lookupFunction<Int32 Function(Pointer<Void>, Pointer<_AeInfo>), int Function(Pointer<Void>, Pointer<_AeInfo>)>('ae_info_get');
    final p = calloc<_AeInfo>();
    try {
      get(_engine, p);
      final bytes = <int>[];
      for (var i = 0; i < 128 && p.ref.device[i] != 0; i++) {
        bytes.add(p.ref.device[i]);
      }
      return EnhancerInfo(p.ref.gpu == 1, p.ref.fp16 == 1, p.ref.tile, p.ref.heapBudgetMb, String.fromCharCodes(bytes));
    } finally {
      calloc.free(p);
    }
  }

  /// RGB24 -> RGB24 (w*scale x h*scale). [strength] mistura com o original
  /// redimensionado. Lanca StateError em falha — nunca devolve o original
  /// fingindo sucesso.
  Uint8List process(Uint8List rgb, int w, int h, {required int scale, double strength = 1}) {
    final ow = w * scale, oh = h * scale;
    final inp = malloc<Uint8>(rgb.length);
    final out = malloc<Uint8>(ow * oh * 3);
    try {
      inp.asTypedList(rgb.length).setAll(0, rgb);
      final rc = _process(_engine, inp, w, h, scale, strength, out, cancel);
      if (rc == aeErrCancelled) throw StateError('Cancelado');
      if (rc != aeOk) throw StateError('A IA falhou neste quadro (código $rc)');
      return Uint8List.fromList(out.asTypedList(ow * oh * 3));
    } finally {
      malloc.free(inp);
      malloc.free(out);
    }
  }

  /// PNG [input] -> PNG [output] (pode ser o mesmo arquivo: a troca so
  /// acontece no fim). A IA roda na escala [scale] e, com [fitW]/[fitH],
  /// a saida vai ao tamanho da composicao. Lanca StateError em falha.
  void processPng(
    String input,
    String output, {
    required int scale,
    double strength = 1,
    int fitW = 0,
    int fitH = 0,
    Pointer<Int32>? parar,
  }) {
    final fn = _lib.lookupFunction<
      Int32 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>, Int32, Float, Int32, Int32, Pointer<Int32>),
      int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>, int, double, int, int, Pointer<Int32>)
    >('ae_process_png');
    final pi = input.toNativeUtf8(), po = output.toNativeUtf8();
    try {
      // [parar]: a celula de cancelamento de quem chama (lida entre tiles).
      final rc = fn(_engine, pi, po, scale, strength, fitW, fitH, parar ?? cancel);
      if (rc == aeErrCancelled) throw StateError('Cancelado');
      if (rc == aeErrIo) throw StateError('Quadro ilegível ou sem espaço para gravar');
      if (rc != aeOk) throw StateError('A IA falhou neste quadro (código $rc)');
    } finally {
      calloc.free(pi);
      calloc.free(po);
    }
  }

  /// Redimensionamento convencional (para o "antes" na mesma resolução).
  Uint8List resize(Uint8List rgb, int w, int h, int ow, int oh) {
    final inp = malloc<Uint8>(rgb.length);
    final out = malloc<Uint8>(ow * oh * 3);
    try {
      inp.asTypedList(rgb.length).setAll(0, rgb);
      if (_resize(inp, w, h, out, ow, oh) != aeOk) throw StateError('Redimensionamento falhou');
      return Uint8List.fromList(out.asTypedList(ow * oh * 3));
    } finally {
      malloc.free(inp);
      malloc.free(out);
    }
  }

  void close() {
    if (_engine == nullptr) return;
    _destroy(_engine);
    _engine = nullptr;
    calloc.free(cancel);
  }
}
