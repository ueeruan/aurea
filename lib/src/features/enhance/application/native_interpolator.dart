// PONTE FFI PARA O RIFE (native/enhance/aurea_rife.cpp, na mesma
// libaurea_enhance do aprimoramento).
//
// Estado verdadeiro, como no aprimoramento: biblioteca, modelo ou GPU que
// nao carregam viram StateError com o motivo — quem chama volta para o
// FFmpeg, nunca exporta quadro inventado com lixo.
import 'dart:ffi';
import 'dart:typed_data';

import 'package:ffi/ffi.dart';

import 'native_enhancer.dart';

final class _ArInfo extends Struct {
  @Int32()
  external int gpu;
  @Int32()
  external int fp16;
  @Int32()
  external int int8;
  @Int32()
  external int reservado;
  @Int64()
  external int heapBudgetMb;
  @Array(128)
  external Array<Uint8> device;
}

const arOk = 0;
const arErrArgs = -1;
const arErrInference = -2;
const arErrModel = -4;
const arErrIo = -5;

class InterpolatorInfo {
  const InterpolatorInfo({
    required this.gpu,
    required this.fp16,
    required this.int8,
    required this.heapBudgetMb,
    required this.device,
  });
  final bool gpu, fp16, int8;
  final int heapBudgetMb;
  final String device;
}

/// Um motor RIFE carregado. Bloqueia: use num isolate de trabalho.
class NativeInterpolator {
  NativeInterpolator._(this._lib, this._engine)
    : _destroy = _lib.lookupFunction<Void Function(Pointer<Void>), void Function(Pointer<Void>)>('ar_destroy'),
      _interpolate = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, Int32, Int32, Float, Pointer<Uint8>),
        int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, int, int, double, Pointer<Uint8>)
      >('ar_interpolate'),
      _interpolatePng = _lib.lookupFunction<
        Int32 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>, Float, Pointer<Utf8>),
        int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>, double, Pointer<Utf8>)
      >('ar_interpolate_png');

  final DynamicLibrary _lib;
  Pointer<Void> _engine;
  final void Function(Pointer<Void>) _destroy;
  final int Function(Pointer<Void>, Pointer<Uint8>, Pointer<Uint8>, int, int, double, Pointer<Uint8>) _interpolate;
  final int Function(Pointer<Void>, Pointer<Utf8>, Pointer<Utf8>, double, Pointer<Utf8>) _interpolatePng;

  /// A biblioteca carrega E tem o RIFE (uma build antiga so tinha o
  /// aprimoramento).
  static bool get libraryAvailable {
    final lib = abrirBibliotecaDeIa();
    return lib != null && lib.providesSymbol('ar_create');
  }

  /// Carrega flownet.param/flownet.bin de [modelDir]. Com [gpu], falta de
  /// Vulkan e erro (a CPU do celular levaria segundos por quadro).
  static NativeInterpolator open(String modelDir, {bool gpu = true}) {
    final lib = abrirBibliotecaDeIa();
    if (lib == null || !lib.providesSymbol('ar_create')) {
      throw StateError('Interpolação por IA indisponível neste aparelho');
    }
    final create = lib.lookupFunction<
      Pointer<Void> Function(Pointer<Utf8>, Int32, Pointer<Utf8>, Int32),
      Pointer<Void> Function(Pointer<Utf8>, int, Pointer<Utf8>, int)
    >('ar_create');
    final dir = modelDir.toNativeUtf8();
    final err = calloc<Uint8>(256).cast<Utf8>();
    try {
      final engine = create(dir, gpu ? 1 : 0, err, 256);
      if (engine == nullptr) {
        throw StateError('RIFE não carregou: ${err.toDartString()}');
      }
      return NativeInterpolator._(lib, engine);
    } finally {
      calloc.free(dir);
      calloc.free(err);
    }
  }

  InterpolatorInfo get info {
    final get = _lib.lookupFunction<
      Int32 Function(Pointer<Void>, Pointer<_ArInfo>),
      int Function(Pointer<Void>, Pointer<_ArInfo>)
    >('ar_info_get');
    final p = calloc<_ArInfo>();
    try {
      get(_engine, p);
      final bytes = <int>[];
      for (var i = 0; i < 128 && p.ref.device[i] != 0; i++) {
        bytes.add(p.ref.device[i]);
      }
      return InterpolatorInfo(
        gpu: p.ref.gpu == 1,
        fp16: p.ref.fp16 == 1,
        int8: p.ref.int8 == 1,
        heapBudgetMb: p.ref.heapBudgetMb,
        device: String.fromCharCodes(bytes),
      );
    } finally {
      calloc.free(p);
    }
  }

  /// RGB24 [a] e [b] (w x h) -> o quadro do instante [t].
  Uint8List interpolate(Uint8List a, Uint8List b, int w, int h, double t) {
    final n = w * h * 3;
    if (a.length != n || b.length != n) {
      throw ArgumentError('quadros com tamanho diferente de ${w}x$h');
    }
    final pa = malloc<Uint8>(n), pb = malloc<Uint8>(n), out = malloc<Uint8>(n);
    try {
      pa.asTypedList(n).setAll(0, a);
      pb.asTypedList(n).setAll(0, b);
      final rc = _interpolate(_engine, pa, pb, w, h, t, out);
      if (rc != arOk) throw StateError(_motivo(rc));
      return Uint8List.fromList(out.asTypedList(n));
    } finally {
      malloc.free(pa);
      malloc.free(pb);
      malloc.free(out);
    }
  }

  /// PNG [a] e [b] -> PNG [out] no instante [t]. Escrita atomica.
  void interpolatePng(String a, String b, double t, String out) {
    final pa = a.toNativeUtf8(), pb = b.toNativeUtf8(), po = out.toNativeUtf8();
    try {
      final rc = _interpolatePng(_engine, pa, pb, t, po);
      if (rc != arOk) throw StateError(_motivo(rc));
    } finally {
      calloc.free(pa);
      calloc.free(pb);
      calloc.free(po);
    }
  }

  static String _motivo(int rc) => switch (rc) {
    arErrArgs => 'RIFE: argumentos inválidos',
    arErrInference => 'RIFE falhou neste quadro (memória da GPU?)',
    arErrIo => 'RIFE: quadro ilegível ou sem espaço para gravar',
    _ => 'RIFE falhou (código $rc)',
  };

  void close() {
    if (_engine == nullptr) return;
    _destroy(_engine);
    _engine = nullptr;
  }
}
