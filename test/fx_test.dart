import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/fx.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';

void main() {
  group('PR-FX0 — fundacao', () {
    test('ruido e puro: mesma (seed, canal, t) -> mesmo valor', () {
      for (final t in [0.0, 0.37, 5.21, 199.9]) {
        expect(fxNoiseSigned(7, 2, t), fxNoiseSigned(7, 2, t));
      }
      expect(fxNoiseSigned(7, 2, 1.5), isNot(fxNoiseSigned(8, 2, 1.5)));
    });

    test('fase integrada: frequencia 2->8 acelera SEM salto', () {
      final freq = AnimatedDouble(2, [
        Keyframe(time: Duration.zero, value: 2),
        Keyframe(time: const Duration(seconds: 2), value: 8),
      ]);
      var prev = 0.0;
      var prevDelta = 0.0;
      const step = Duration(milliseconds: 20);
      for (var i = 1; i <= 100; i++) {
        final phase = integratedPhase(freq, step * i);
        final delta = phase - prev;
        // Monotonica e continua: cada passo avanca, delta limitado pela
        // frequencia maxima, e sem descontinuidade (salto de derivada).
        expect(delta, greaterThan(0));
        expect(delta, lessThan(8 * 0.02 * 1.5));
        if (i > 1) {
          expect((delta - prevDelta).abs(), lessThan(0.03),
              reason: 'salto de derivada no passo $i');
        }
        prev = phase;
        prevDelta = delta;
      }
      // Total: integral de 2..8 sobre 2s = 10 (media 5 * 2s).
      expect(integratedPhase(freq, const Duration(seconds: 2)),
          closeTo(10, 0.05));
    });

    test('fase integrada sem keyframe = caminho analitico', () {
      final freq = AnimatedDouble(8);
      expect(integratedPhase(freq, const Duration(seconds: 3)),
          closeTo(24, 1e-9));
    });
  });

  group('Tremor (PR-FX3)', () {
    test('repetivel: mesma semente, mesmo resultado', () {
      final a = tremorSample(
          amplitudePx: 60, phase: 3.7, style: 0, seed: 5, zoom: 0.4);
      final b = tremorSample(
          amplitudePx: 60, phase: 3.7, style: 0, seed: 5, zoom: 0.4);
      expect(a.dx, b.dx);
      expect(a.dy, b.dy);
      expect(a.scale, b.scale);
      // Semente diferente -> resultado diferente.
      final c = tremorSample(
          amplitudePx: 60, phase: 3.7, style: 0, seed: 6, zoom: 0.4);
      expect(a.dx, isNot(c.dx));
    });

    test('neutro em zero (I2)', () {
      final s = tremorSample(
          amplitudePx: 0, phase: 2.2, style: 0, seed: 1);
      expect(s.isNeutral, true);
    });

    test('estilo nervoso tem pausas', () {
      var still = 0;
      for (var i = 0; i < 200; i++) {
        final s = tremorSample(
            amplitudePx: 100, phase: i * 0.11, style: 1, seed: 3);
        if (s.dx.abs() < 2) still++;
      }
      expect(still, greaterThan(20)); // periodos parados existem
    });
  });

  group('Glitch Modular (PR-FX4)', () {
    test('seekavel: estado no frame N direto == apos reproduzir', () {
      GlitchState at(double tau) => glitchState(
            master: 1,
            tau: tau,
            intervalSec: 0.5,
            seed: 9,
            slide: 0.8,
            colorAmt: 0.6,
            blurAmt: 0.5,
            rgbAmt: 0.5,
          );
      // "Reproduzir" = avaliar varios frames antes; nada acumula.
      for (var i = 0; i < 50; i++) {
        at(i * 0.033);
      }
      final direct = at(6.6);
      final replayed = at(6.6);
      expect(direct.dx, replayed.dx);
      expect(direct.hueDeg, replayed.hueDeg);
      expect(direct.blurSigma, replayed.blurSigma);
    });

    test('mestre em 0 nao muda um pixel (I2)', () {
      final st = glitchState(
          master: 0,
          tau: 4.2,
          intervalSec: 0.5,
          seed: 1,
          slide: 1,
          colorAmt: 1);
      expect(st.isNeutral, true);
    });

    test('tiques acontecem com mestre alto', () {
      var active = 0;
      for (var i = 0; i < 100; i++) {
        final st = glitchState(
            master: 2,
            tau: i * 0.25,
            intervalSec: 0.5,
            seed: 4,
            slide: 1);
        if (!st.isNeutral) active++;
      }
      expect(active, greaterThan(10));
      expect(active, lessThan(100)); // e nao o tempo todo
    });
  });
}
