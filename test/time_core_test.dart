// NUCLEO TEMPORAL EM C++ (packages/aurea_timecore): paridade com a
// implementacao Dart que ele substituiu e os casos que ela errava.
import 'package:aurea/src/features/editor/domain/cut_ops.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/time_core.dart';
import 'package:flutter_test/flutter_test.dart';

VideoLayer _video({
  AnimatedDouble? track,
  double speed = 1,
  bool reverse = false,
  Duration duration = const Duration(seconds: 4),
  Duration offset = Duration.zero,
}) => VideoLayer(
  id: 'v',
  name: 'v',
  startTime: Duration.zero,
  duration: duration,
  sourcePath: '/x.mp4',
  sourceOffset: offset,
  speed: speed,
  reverse: reverse,
  effects: [
    if (track != null)
      EffectInstance(type: EffectType.timeRemap, params: {'tempo': track}),
  ],
);

// A implementacao Dart ANTIGA (cut_ops antes do nucleo), guardada so aqui
// para provar a paridade.
double _legacyExtended(AnimatedDouble track, Duration time) {
  final ks = track.keyframes;
  if (ks.isEmpty) return track.base;
  if (time >= ks.first.time && time <= ks.last.time) return track.valueAt(time);
  if (ks.length < 2) {
    return time < ks.first.time ? ks.first.value : ks.last.value;
  }
  final a = time < ks.first.time ? ks[0] : ks[ks.length - 2];
  final b = time < ks.first.time ? ks[1] : ks[ks.length - 1];
  final dt = (b.time - a.time).inMicroseconds;
  if (dt == 0) return time < ks.first.time ? ks.first.value : ks.last.value;
  final slope = (b.value - a.value) / dt;
  final anchor = time < ks.first.time ? a : b;
  return anchor.value + (time - anchor.time).inMicroseconds * slope;
}

AnimatedDouble _t(List<(int, double, Easing)> ps) {
  var t = AnimatedDouble(ps.first.$2);
  for (final p in ps) {
    t = t.withKeyframe(Duration(milliseconds: p.$1), p.$2, p.$3);
  }
  return t;
}

void main() {
  test('ABI: C++ e Dart concordam no layout', () {
    expect(coreAbiOk(), isTrue);
  });

  test('paridade com o Dart antigo: linear, hold, extrapolacao, loop e oscilantes', () {
    final curvas = [
      _t([(0, 0, Easing.linear), (2000, 1, Easing.linear), (4000, 3, Easing.linear)]),
      _t([
        (0, 0, const Easing(type: EasingType.hold)),
        (1000, 2, Easing.linear),
        (3000, 2.5, Easing.linear),
      ]),
      _t([
        (0, 0, Easing.bounce),
        (1500, 1.2, Easing.elastic),
        (4000, 2, const Easing(type: EasingType.spring)),
      ]),
      _t([
        (0, 0, const Easing(type: EasingType.cyclic, count: 3)),
        (2000, 1, const Easing(type: EasingType.steps, count: 5)),
        (4000, 2, const Easing(type: EasingType.random, intensity: .8)),
      ]),
      _t([
        (0, 0, const Easing(type: EasingType.elasticSteps, count: 4)),
        (4000, 3, Easing.linear),
      ]),
    ];
    for (final c in curvas) {
      for (var ms = -700; ms <= 4700; ms += 13) {
        final t = Duration(milliseconds: ms);
        expect(coreValue(c, t, linear: true), closeTo(_legacyExtended(c, t), 1e-9), reason: 't=$ms');
        expect(coreValue(c, t), closeTo(c.valueAt(t), 1e-9), reason: 'hold t=$ms');
      }
    }
    final loop = _t([(0, 0, Easing.linear), (1000, 1, Easing.linear)]).withLoop(
      const LoopSpec(mode: LoopMode.pingPong, when: LoopWhen.both),
    );
    for (var ms = -3500; ms <= 3500; ms += 37) {
      final t = Duration(milliseconds: ms);
      expect(coreValue(loop, t), closeTo(loop.valueAt(t), 1e-9), reason: 'loop t=$ms');
    }
  });

  test('bezier: o nucleo e exato onde o Cubic do Flutter erra ate 1e-3', () {
    final c = _t([(0, 0, Easing.easeInOut), (5000, 5, Easing.linear)]);
    for (var ms = 0; ms <= 5000; ms += 50) {
      final t = Duration(milliseconds: ms);
      expect(coreValue(c, t), closeTo(c.valueAt(t), 0.012));
    }
    double ref(double x) {
      double bx(double u) =>
          3 * (1 - u) * (1 - u) * u * .42 + 3 * (1 - u) * u * u * .58 + u * u * u;
      var lo = 0.0, hi = 1.0;
      for (var i = 0; i < 200; i++) {
        final m = (lo + hi) / 2;
        if (bx(m) < x) {
          lo = m;
        } else {
          hi = m;
        }
      }
      final u = (lo + hi) / 2;
      return 3 * (1 - u) * u * u + u * u * u;
    }

    for (var ms = 1; ms < 5000; ms += 97) {
      expect(coreValue(c, Duration(milliseconds: ms)), closeTo(5 * ref(ms / 5000), 1e-9));
    }
  });

  test('camada: velocidade, reverso, offset, congelado e taxa analitica', () {
    final v = _video(speed: 2, offset: const Duration(seconds: 1));
    expect(videoSourceTimeAt(v, const Duration(seconds: 1)), const Duration(seconds: 2));
    expect(videoAbsoluteSourceTimeAt(v, const Duration(seconds: 1)), const Duration(seconds: 3));
    expect(videoSourceSpan(v), const Duration(seconds: 8));
    final r = _video(speed: 2, reverse: true);
    expect(videoSourceTimeAt(r, Duration.zero), const Duration(seconds: 8));
    expect(videoPlaybackRateAt(r, const Duration(seconds: 1)), -2);

    final rampa = _t([(0, 0, Easing.linear), (2000, 1, Easing.linear), (4000, 4, Easing.linear)]);
    final vr = _video(track: rampa);
    expect(videoPlaybackRateAt(vr, const Duration(milliseconds: 500)), closeTo(0.5, 1e-9));
    expect(videoPlaybackRateAt(vr, const Duration(milliseconds: 3000)), closeTo(1.5, 1e-9));

    final hold = _t([(0, 1, const Easing(type: EasingType.hold)), (2000, 3, Easing.linear)]);
    final vh = _video(track: hold);
    expect(videoPlaybackRateAt(vh, const Duration(milliseconds: 900)), 0);
    expect(videoSourceTimeAt(vh, const Duration(milliseconds: 1900)), const Duration(seconds: 1));
  });

  test('faixa inclui o overshoot da bezier', () {
    final over = _t([(0, 0, Easing.overshoot), (2000, 2, Easing.linear)]);
    final (lo, hi) = coreRange(over);
    expect(lo, 0);
    expect(hi, greaterThan(2.0));
    expect(videoSourceSpan(_video(track: over)).inMicroseconds, greaterThan(2000000));
  });

  test('recorte exato: sub-bezier coincide com a curva original', () {
    final c = _t([(0, 0, Easing.easeInOut), (4000, 4, Easing.easeIn), (6000, 5, Easing.linear)]);
    final v = _video(track: c, duration: const Duration(seconds: 6));
    const from = Duration(milliseconds: 1300), to = Duration(milliseconds: 5100);
    final s = sliceVideoTrack(v, from, to);
    final cortado = _video(track: s.track, offset: s.sourceOffset, duration: to - from);
    for (var ms = 0; ms <= (to - from).inMilliseconds; ms += 11) {
      final local = Duration(milliseconds: ms);
      final esperado = videoAbsoluteSourceTimeAt(v, from + local);
      final obtido = videoAbsoluteSourceTimeAt(cortado, local);
      expect((obtido - esperado).inMicroseconds.abs(), lessThanOrEqualTo(2), reason: 'ms=$ms');
    }
  });

  test('recorte de trecho oscilante fica dentro da tolerancia', () {
    final c = _t([(0, 0, Easing.elastic), (3000, 3, Easing.linear)]);
    final v = _video(track: c, duration: const Duration(seconds: 3));
    const from = Duration(milliseconds: 400), to = Duration(milliseconds: 2600);
    final s = sliceVideoTrack(v, from, to);
    final cortado = _video(track: s.track, offset: s.sourceOffset, duration: to - from);
    for (var ms = 0; ms <= 2200; ms += 7) {
      final d = videoAbsoluteSourceTimeAt(cortado, Duration(milliseconds: ms)) -
          videoAbsoluteSourceTimeAt(v, from + Duration(milliseconds: ms));
      expect(d.inMicroseconds.abs(), lessThanOrEqualTo(300), reason: 'ms=$ms');
    }
  });

  test('velocidade -> tempo: rampa linear integrada e exata', () {
    final speed = AnimatedDouble(1)
        .withKeyframe(Duration.zero, 1)
        .withKeyframe(const Duration(seconds: 2), 0.25);
    final track = coreSpeedToTrack(speed);
    expect(track.keyframes, hasLength(2));
    expect(coreValue(track, const Duration(seconds: 2)), closeTo(1.25, 1e-9));
    for (var ms = 0; ms <= 2000; ms += 50) {
      final t = ms / 1000;
      final exato = t - 0.375 * t * t / 2;
      expect(coreValue(track, Duration(milliseconds: ms)), closeTo(exato, 1e-8));
      expect(coreSlope(track, Duration(milliseconds: ms)), closeTo(1 - 0.375 * t, 1e-5));
    }
    final eased = AnimatedDouble(2)
        .withKeyframe(Duration.zero, 2, Easing.easeInOut)
        .withKeyframe(const Duration(seconds: 3), 0.5);
    final te = coreSpeedToTrack(eased);
    double integral(double t) {
      const n = 4000;
      var s = 0.0;
      for (var i = 0; i < n; i++) {
        final f = (i + .5) / n * t / 3;
        s += (2 + (0.5 - 2) * coreValue(_t([(0, 0, Easing.easeInOut), (1000, 1, Easing.linear)]), Duration(microseconds: (f * 1e6).round()))) * t / n;
      }
      return s;
    }

    for (var ms = 0; ms <= 3000; ms += 250) {
      expect(coreValue(te, Duration(milliseconds: ms)), closeTo(integral(ms / 1000), 0.0012), reason: 'ms=$ms');
    }
  });

  test('tabela de quadros: VFR, repetidos, fora de ordem, limites e snap', () {
    final idx = CoreFrameIndex([66733, 0, 33367, 33367, 100100, 150000, -(1 << 62) - 5]);
    expect(idx.length, 5);
    expect(idx.bracket(-10)!.kind, 2);
    expect(idx.bracket(200000)!.kind, 3);
    final meio = idx.bracket(125050)!;
    expect(meio.kind, 1);
    expect(meio.ptsA, 100100);
    expect(meio.ptsB, 150000);
    expect(meio.alpha, closeTo((125050 - 100100) / (150000 - 100100), 1e-12));
    final exato = idx.bracket(33367 + 200)!;
    expect(exato.kind, 0);
    expect(exato.ptsA, 33367);
    expect(idx.bracket(66733 - 300)!.ptsA, 66733);
    expect(idx.floor(66732), 1);
  });

  test('split de clipe remapeado continua de onde a primeira metade parou', () {
    final c = _t([(0, 0, Easing.easeInOut), (4000, 4, Easing.linear)]);
    final v = _video(track: c);
    const corte = Duration(milliseconds: 1700);
    final b = sliceVideoTrack(v, corte, v.duration);
    expect(b.track.keyframes.first.time, Duration.zero);
    final segunda = _video(track: b.track, offset: b.sourceOffset, duration: v.duration - corte);
    final d = videoAbsoluteSourceTimeAt(segunda, Duration.zero) - videoAbsoluteSourceTimeAt(v, corte);
    expect(d.inMicroseconds.abs(), lessThanOrEqualTo(2));
  });
}
