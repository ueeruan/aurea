/// Bindings FFI do nucleo temporal (src/timecore.h). Tipos crus: quem usa
/// (o app) converte de/para AnimatedDouble.
library;

import 'dart:ffi';

import 'package:ffi/ffi.dart';

final class AtcKeyframe extends Struct {
  @Int64()
  external int timeUs;
  @Double()
  external double value;
  @Int32()
  external int type;
  @Int32()
  external int count;
  @Double()
  external double x1;
  @Double()
  external double y1;
  @Double()
  external double x2;
  @Double()
  external double y2;
  @Double()
  external double smooth;
  @Double()
  external double intensity;
  @Double()
  external double response;
  @Double()
  external double damping;
  @Double()
  external double velocity;
}

final class AtcLayer extends Struct {
  @Int64()
  external int sourceOffsetUs;
  @Int64()
  external int durationUs;
  @Double()
  external double speed;
  @Int32()
  external int reverse;
  @Int32()
  external int reserved;
  external Pointer<Void> curve;
}

final class AtcBracket extends Struct {
  @Int32()
  external int a;
  @Int32()
  external int b;
  @Int32()
  external int kind;
  @Int32()
  external int reserved;
  @Int64()
  external int ptsAUs;
  @Int64()
  external int ptsBUs;
  @Double()
  external double alpha;
}

const atcExtrapHold = 0;
const atcExtrapLinear = 1;
const atcBracketExact = 0;
const atcBracketInterpolate = 1;
const atcBracketBefore = 2;
const atcBracketAfter = 3;

@Native<Int32 Function()>(symbol: 'atc_version', isLeaf: true)
external int atcVersion();
@Native<Int32 Function()>(symbol: 'atc_sizeof_keyframe', isLeaf: true)
external int atcSizeofKeyframe();
@Native<Int32 Function()>(symbol: 'atc_sizeof_layer', isLeaf: true)
external int atcSizeofLayer();
@Native<Int32 Function()>(symbol: 'atc_sizeof_bracket', isLeaf: true)
external int atcSizeofBracket();

@Native<Double Function(Pointer<AtcKeyframe>, Double)>(symbol: 'atc_ease_transform', isLeaf: true)
external double atcEaseTransform(Pointer<AtcKeyframe> ease, double t);
@Native<Double Function(Pointer<AtcKeyframe>, Double)>(symbol: 'atc_ease_derivative', isLeaf: true)
external double atcEaseDerivative(Pointer<AtcKeyframe> ease, double t);

@Native<Pointer<Void> Function(Double, Pointer<AtcKeyframe>, Int32, Int32, Int32, Int32)>(
  symbol: 'atc_curve_create',
  isLeaf: true,
)
external Pointer<Void> atcCurveCreate(
  double base,
  Pointer<AtcKeyframe> keyframes,
  int count,
  int loopMode,
  int loopWhen,
  int loopCount,
);
@Native<Void Function(Pointer<Void>)>(symbol: 'atc_curve_destroy', isLeaf: true)
external void atcCurveDestroy(Pointer<Void> curve);
@Native<Double Function(Pointer<Void>, Int64, Int32)>(symbol: 'atc_curve_value', isLeaf: true)
external double atcCurveValue(Pointer<Void> curve, int tUs, int extrap);
@Native<Double Function(Pointer<Void>, Int64, Int32)>(symbol: 'atc_curve_slope', isLeaf: true)
external double atcCurveSlope(Pointer<Void> curve, int tUs, int extrap);
@Native<Int32 Function(Pointer<Void>, Pointer<Double>, Pointer<Double>)>(
  symbol: 'atc_curve_range',
  isLeaf: true,
)
external int atcCurveRange(Pointer<Void> curve, Pointer<Double> lo, Pointer<Double> hi);

@Native<Int64 Function(Pointer<AtcLayer>)>(symbol: 'atc_layer_span_us', isLeaf: true)
external int atcLayerSpanUs(Pointer<AtcLayer> layer);
@Native<Int64 Function(Pointer<AtcLayer>, Int64)>(symbol: 'atc_layer_source_us', isLeaf: true)
external int atcLayerSourceUs(Pointer<AtcLayer> layer, int localUs);
@Native<Int64 Function(Pointer<AtcLayer>, Int64)>(
  symbol: 'atc_layer_absolute_source_us',
  isLeaf: true,
)
external int atcLayerAbsoluteSourceUs(Pointer<AtcLayer> layer, int localUs);
@Native<Double Function(Pointer<AtcLayer>, Int64)>(symbol: 'atc_layer_rate', isLeaf: true)
external double atcLayerRate(Pointer<AtcLayer> layer, int localUs);
@Native<Int32 Function(Pointer<AtcLayer>, Int64, Int64, Int64, Pointer<AtcKeyframe>, Int32, Pointer<Double>)>(
  symbol: 'atc_layer_slice',
  isLeaf: true,
)
external int atcLayerSlice(
  Pointer<AtcLayer> layer,
  int fromUs,
  int toUs,
  int toleranceUs,
  Pointer<AtcKeyframe> out,
  int capacity,
  Pointer<Double> minValue,
);
@Native<Int32 Function(Pointer<AtcKeyframe>, Int32, Double, Int64, Pointer<AtcKeyframe>, Int32)>(
  symbol: 'atc_speed_to_value',
  isLeaf: true,
)
external int atcSpeedToValue(
  Pointer<AtcKeyframe> speed,
  int count,
  double startValue,
  int toleranceUs,
  Pointer<AtcKeyframe> out,
  int capacity,
);

@Native<Pointer<Void> Function(Pointer<Int64>, Int32, Int64)>(
  symbol: 'atc_frame_index_create',
  isLeaf: true,
)
external Pointer<Void> atcFrameIndexCreate(Pointer<Int64> pts, int n, int lastDurationUs);
@Native<Void Function(Pointer<Void>)>(symbol: 'atc_frame_index_destroy', isLeaf: true)
external void atcFrameIndexDestroy(Pointer<Void> index);
@Native<Int32 Function(Pointer<Void>)>(symbol: 'atc_frame_index_count', isLeaf: true)
external int atcFrameIndexCount(Pointer<Void> index);
@Native<Int32 Function(Pointer<Void>, Int64)>(symbol: 'atc_frame_index_floor', isLeaf: true)
external int atcFrameIndexFloor(Pointer<Void> index, int sourceUs);
@Native<Int32 Function(Pointer<Void>, Int64, Int64, Double, Pointer<AtcBracket>)>(
  symbol: 'atc_frame_bracket',
  isLeaf: true,
)
external int atcFrameBracket(
  Pointer<Void> index,
  int sourceUs,
  int snapUs,
  double snapAlpha,
  Pointer<AtcBracket> out,
);

/// Libera curvas e tabelas quando o objeto Dart some.
final NativeFinalizer atcCurveFinalizer = NativeFinalizer(
  Native.addressOf<NativeFunction<Void Function(Pointer<Void>)>>(atcCurveDestroy).cast(),
);
final NativeFinalizer atcFrameIndexFinalizer = NativeFinalizer(
  Native.addressOf<NativeFunction<Void Function(Pointer<Void>)>>(atcFrameIndexDestroy).cast(),
);

/// Um buffer de keyframes reutilizavel (evita malloc por avaliacao).
Pointer<AtcKeyframe> atcAllocKeyframes(int n) => calloc<AtcKeyframe>(n < 1 ? 1 : n);
