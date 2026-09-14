// A PONTE ENTRE O PROJETO E O NUCLEO TEMPORAL EM C++.
//
// O Time Remap tinha tres avaliacoes da mesma trilha (cut_ops extrapolava,
// o palco segurava, o editor usava valueAt) e a bezier do Flutter errava
// ate 1e-3 no progresso. Agora toda pergunta "que instante da fonte" passa
// por packages/aurea_timecore — o mesmo codigo que o motor do Android usa.
import 'dart:ffi';

import 'package:aurea_timecore/aurea_timecore.dart';
import 'package:ffi/ffi.dart';

import 'keyframe.dart';

/// Curva nativa ligada a UMA trilha imutavel.
final class NativeCurve implements Finalizable {
  NativeCurve._(this.pointer, this.bakedUntilUs);

  final Pointer<Void> pointer;

  /// Trilha com expressao e amostrada ate aqui (0 = sem expressao).
  final int bakedUntilUs;
}

final Expando<NativeCurve> _curves = Expando('aurea_timecore');

/// Passo da amostragem de trilhas com expressao: 240 amostras por segundo.
const _bakeStepUs = 1000000 ~/ 240;

void _fillKeyframe(Pointer<AtcKeyframe> p, int timeUs, double value, Easing e) {
  final k = p.ref;
  k.timeUs = timeUs;
  k.value = value;
  k.type = e.type.index;
  k.count = e.count;
  k.x1 = e.x1;
  k.y1 = e.y1;
  k.x2 = e.x2;
  k.y2 = e.y2;
  k.smooth = e.smooth;
  k.intensity = e.intensity;
  k.response = e.response;
  k.damping = e.damping;
  k.velocity = e.initialVelocity;
}

Easing _easingOf(AtcKeyframe k) => Easing(
  type: EasingType.values[k.type.clamp(0, EasingType.values.length - 1)],
  count: k.count,
  x1: k.x1,
  y1: k.y1,
  x2: k.x2,
  y2: k.y2,
  smooth: k.smooth,
  intensity: k.intensity,
  response: k.response,
  damping: k.damping,
  initialVelocity: k.velocity,
);

/// Curva nativa da trilha, criada uma vez por instancia. [bakeUntil]
/// so importa para trilha com expressao: a expressao roda no avaliador
/// Dart e vira amostras densas ate esse instante.
NativeCurve nativeCurveOf(AnimatedDouble track, {Duration? bakeUntil}) {
  final pronta = _curves[track];
  final wantBake = track.hasExpression
      ? (bakeUntil?.inMicroseconds ?? 0).clamp(1000000, 1 << 40)
      : 0;
  if (pronta != null && pronta.bakedUntilUs >= wantBake) return pronta;

  final Pointer<Void> ptr;
  if (track.hasExpression) {
    final n = wantBake ~/ _bakeStepUs + 2;
    final buf = calloc<AtcKeyframe>(n);
    try {
      for (var i = 0; i < n; i++) {
        final t = i * _bakeStepUs;
        _fillKeyframe(buf + i, t, track.valueAt(Duration(microseconds: t)), Easing.linear);
      }
      ptr = atcCurveCreate(track.base, buf, n, 0, 0, 0);
    } finally {
      calloc.free(buf);
    }
  } else {
    final ks = track.keyframes;
    final buf = calloc<AtcKeyframe>(ks.isEmpty ? 1 : ks.length);
    try {
      for (var i = 0; i < ks.length; i++) {
        _fillKeyframe(buf + i, ks[i].time.inMicroseconds, ks[i].value, ks[i].ease);
      }
      ptr = atcCurveCreate(
        track.base,
        buf,
        ks.length,
        track.loop.mode.index,
        track.loop.when.index,
        track.loop.count,
      );
    } finally {
      calloc.free(buf);
    }
  }
  if (ptr == nullptr) throw StateError('aurea_timecore: sem memoria para a curva');
  final curve = NativeCurve._(ptr, wantBake);
  atcCurveFinalizer.attach(curve, ptr, detach: curve);
  _curves[track] = curve;
  return curve;
}

/// Valor da trilha em [t]. [linear] = extrapola com a secante das pontas
/// (clipe de video); senao segura/aplica loop, como valueAt.
double coreValue(AnimatedDouble track, Duration t, {bool linear = false, Duration? bakeUntil}) {
  final c = nativeCurveOf(track, bakeUntil: bakeUntil);
  final v = atcCurveValue(c.pointer, t.inMicroseconds, linear ? atcExtrapLinear : atcExtrapHold);
  return v;
}

/// Derivada da trilha em valor por segundo.
double coreSlope(AnimatedDouble track, Duration t, {bool linear = false, Duration? bakeUntil}) {
  final c = nativeCurveOf(track, bakeUntil: bakeUntil);
  return atcCurveSlope(c.pointer, t.inMicroseconds, linear ? atcExtrapLinear : atcExtrapHold);
}

/// Parametros de uma camada para o mapeamento: um bloco nativo reaproveitado
/// (o isolate e unico e as chamadas sao folha).
final Pointer<AtcLayer> _layer = calloc<AtcLayer>();
final Pointer<Double> _lo = calloc<Double>();
final Pointer<Double> _hi = calloc<Double>();

Pointer<AtcLayer> _fill({
  required int sourceOffsetUs,
  required int durationUs,
  required double speed,
  required bool reverse,
  AnimatedDouble? track,
}) {
  final l = _layer.ref;
  l.sourceOffsetUs = sourceOffsetUs;
  l.durationUs = durationUs;
  l.speed = speed;
  l.reverse = reverse ? 1 : 0;
  l.reserved = 0;
  l.curve = track == null
      ? nullptr
      : nativeCurveOf(track, bakeUntil: Duration(microseconds: durationUs)).pointer;
  return _layer;
}

typedef CoreLayer = ({
  int sourceOffsetUs,
  int durationUs,
  double speed,
  bool reverse,
  AnimatedDouble? track,
});

int coreLayerSpanUs(CoreLayer l) => atcLayerSpanUs(_fillL(l));
int coreLayerSourceUs(CoreLayer l, int localUs) => atcLayerSourceUs(_fillL(l), localUs);
int coreLayerAbsoluteUs(CoreLayer l, int localUs) =>
    atcLayerAbsoluteSourceUs(_fillL(l), localUs);
double coreLayerRate(CoreLayer l, int localUs) => atcLayerRate(_fillL(l), localUs);

Pointer<AtcLayer> _fillL(CoreLayer l) {
  final p = _fill(
    sourceOffsetUs: l.sourceOffsetUs,
    durationUs: l.durationUs,
    speed: l.speed,
    reverse: l.reverse,
    track: l.track,
  );
  return p;
}

/// Recorte exato do mapeamento entre dois instantes locais. Os valores
/// saem relativos ao minimo real (que vira o novo sourceOffset).
({double minimum, AnimatedDouble track}) coreSlice(
  CoreLayer l,
  int fromUs,
  int toUs, {
  int toleranceUs = 250,
}) {
  final layer = _fillL(l);
  var capacity = 16 + (l.track?.keyframes.length ?? 0) * 2;
  final minimum = calloc<Double>();
  try {
    for (var tentativa = 0; tentativa < 3; tentativa++) {
      final buf = calloc<AtcKeyframe>(capacity);
      try {
        final n = atcLayerSlice(layer, fromUs, toUs, toleranceUs, buf, capacity, minimum);
        if (n < 0) {
          capacity = -n;
          continue;
        }
        final kfs = <Keyframe<double>>[
          for (var i = 0; i < n; i++)
            Keyframe(
              time: Duration(microseconds: (buf + i).ref.timeUs),
              value: (buf + i).ref.value,
              ease: _easingOf((buf + i).ref),
            ),
        ];
        final base = kfs.isEmpty ? 0.0 : kfs.first.value;
        return (minimum: minimum.value, track: _trackFromSorted(base, kfs));
      } finally {
        calloc.free(buf);
      }
    }
  } finally {
    calloc.free(minimum);
  }
  throw StateError('aurea_timecore: recorte nao coube');
}

/// Velocidade -> tempo: [speed] tem velocidades (1 = normal) nos keyframes;
/// devolve a trilha de TEMPO integrada.
AnimatedDouble coreSpeedToTrack(AnimatedDouble speed, {double start = 0, int toleranceUs = 250}) {
  final ks = speed.keyframes;
  final inBuf = calloc<AtcKeyframe>(ks.isEmpty ? 1 : ks.length);
  var capacity = 8 + ks.length * 4;
  try {
    for (var i = 0; i < ks.length; i++) {
      _fillKeyframe(inBuf + i, ks[i].time.inMicroseconds, ks[i].value, ks[i].ease);
    }
    for (var tentativa = 0; tentativa < 3; tentativa++) {
      final out = calloc<AtcKeyframe>(capacity);
      try {
        final n = atcSpeedToValue(inBuf, ks.length, start, toleranceUs, out, capacity);
        if (n < 0) {
          capacity = -n;
          continue;
        }
        final kfs = <Keyframe<double>>[
          for (var i = 0; i < n; i++)
            Keyframe(
              time: Duration(microseconds: (out + i).ref.timeUs),
              value: (out + i).ref.value,
              ease: _easingOf((out + i).ref),
            ),
        ];
        return _trackFromSorted(start, kfs);
      } finally {
        calloc.free(out);
      }
    }
  } finally {
    calloc.free(inBuf);
  }
  throw StateError('aurea_timecore: integracao nao coube');
}

/// Monta sem passar por withKeyframe: pedacos de Hermite podem ficar a
/// menos de 8 ms um do outro, e withKeyframe descartaria o vizinho.
AnimatedDouble _trackFromSorted(double base, List<Keyframe<double>> kfs) =>
    AnimatedDouble(base, kfs);

/// Faixa real (extremos com overshoot) da trilha.
(double, double) coreRange(AnimatedDouble track) {
  final c = nativeCurveOf(track);
  atcCurveRange(c.pointer, _lo, _hi);
  return (_lo.value, _hi.value);
}

/// Tabela de PTS da fonte (VFR, repetidos e fora de ordem tratados no C++).
final class CoreFrameIndex implements Finalizable {
  CoreFrameIndex(List<int> ptsUs, {int lastDurationUs = 0}) : _ptr = _create(ptsUs, lastDurationUs) {
    atcFrameIndexFinalizer.attach(this, _ptr, detach: this);
  }

  static Pointer<Void> _create(List<int> pts, int lastDurationUs) {
    final buf = calloc<Int64>(pts.isEmpty ? 1 : pts.length);
    try {
      for (var i = 0; i < pts.length; i++) {
        buf[i] = pts[i];
      }
      final p = atcFrameIndexCreate(buf, pts.length, lastDurationUs);
      if (p == nullptr) throw StateError('aurea_timecore: sem memoria para a tabela');
      return p;
    } finally {
      calloc.free(buf);
    }
  }

  final Pointer<Void> _ptr;
  static final Pointer<AtcBracket> _b = calloc<AtcBracket>();

  int get length => atcFrameIndexCount(_ptr);
  int floor(int sourceUs) => atcFrameIndexFloor(_ptr, sourceUs);

  /// (kind, a, b, ptsA, ptsB, alpha) — ver atc_frame_bracket.
  ({int kind, int a, int b, int ptsA, int ptsB, double alpha})? bracket(
    int sourceUs, {
    int snapUs = 1000,
    double snapAlpha = 0.01,
  }) {
    if (atcFrameBracket(_ptr, sourceUs, snapUs, snapAlpha, _b) != 0) return null;
    final r = _b.ref;
    return (kind: r.kind, a: r.a, b: r.b, ptsA: r.ptsAUs, ptsB: r.ptsBUs, alpha: r.alpha);
  }
}

/// Confere que o C++ e o Dart concordam no layout (chamado pelos testes).
bool coreAbiOk() =>
    atcVersion() == 1 &&
    atcSizeofKeyframe() == sizeOf<AtcKeyframe>() &&
    atcSizeofLayer() == sizeOf<AtcLayer>() &&
    atcSizeofBracket() == sizeOf<AtcBracket>();
