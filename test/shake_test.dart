import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/fx.dart';

void main() {
  group('Eixo do Shake', () {
    // A separacao entre aleatorio e onda e o que faz parecer camera na
    // mao: so ruido treme sem intencao, so senoide balanca como
    // metronomo.
    test('so onda e periodico', () {
      const eixo = ShakeAxis(
          randomAmplitude: 0, waveAmplitude: 1, waveFrequency: 1);
      final a = eixo.valueAt(1, 1, 0);
      final b = eixo.valueAt(1, 1, 1);
      expect(b, closeTo(a, 1e-6));
    });

    test('so ruido nao e periodico', () {
      const eixo = ShakeAxis(randomAmplitude: 1, waveAmplitude: 0);
      final a = eixo.valueAt(1, 1, 0);
      final b = eixo.valueAt(1, 1, 1);
      expect((b - a).abs(), greaterThan(1e-6));
    });

    test('eixo zerado nao produz nada', () {
      const eixo = ShakeAxis(randomAmplitude: 0, waveAmplitude: 0);
      expect(eixo.isNeutral, isTrue);
      expect(eixo.valueAt(1, 1, 0.7), 0);
    });

    test('a fase desloca a onda', () {
      const a = ShakeAxis(waveAmplitude: 1, waveFrequency: 1);
      const b = ShakeAxis(
          waveAmplitude: 1, waveFrequency: 1, phaseDeg: 180);
      expect(a.valueAt(1, 1, 0.25), isNot(closeTo(b.valueAt(1, 1, 0.25), 0.1)));
    });
  });

  group('Amostra do Shake', () {
    // NEUTRALIDADE: amplitude zero nao pode mexer um pixel.
    test('amplitude zero nao mexe nada', () {
      final s = tremorSample(
          amplitudePx: 0, phase: 0.3, style: 0, seed: 1);
      expect(s.isNeutral, isTrue);
    });

    test('sem eixo nenhum, nao mexe nada', () {
      final s = tremorSample(
        amplitudePx: 100,
        phase: 0.3,
        style: 0,
        seed: 1,
        x: const ShakeAxis(randomAmplitude: 0),
        y: const ShakeAxis(randomAmplitude: 0),
      );
      expect(s.isNeutral, isTrue);
    });

    // DETERMINISMO: mesma semente e mesma fase, mesmo resultado, sempre.
    test('mesma semente e fase dao o mesmo resultado', () {
      final a = tremorSample(
          amplitudePx: 50, phase: 1.234, style: 0, seed: 7);
      final b = tremorSample(
          amplitudePx: 50, phase: 1.234, style: 0, seed: 7);
      expect(a.dx, b.dx);
      expect(a.dy, b.dy);
    });

    test('sementes diferentes dao tremores diferentes', () {
      final a = tremorSample(
          amplitudePx: 50, phase: 1.0, style: 0, seed: 1);
      final b = tremorSample(
          amplitudePx: 50, phase: 1.0, style: 0, seed: 2);
      expect(a.dx, isNot(b.dx));
    });

    // Camera longe treme menos NA TELA para o mesmo tremor no espaco.
    test('distancia em Z reduz o deslocamento', () {
      final perto = tremorSample(
          amplitudePx: 100, phase: 0.5, style: 0, seed: 3, zDistance: 1);
      final longe = tremorSample(
          amplitudePx: 100, phase: 0.5, style: 0, seed: 3, zDistance: 4);
      expect(longe.dx.abs(), lessThan(perto.dx.abs()));
    });

    test('center bias segura a imagem perto do centro', () {
      final solto = tremorSample(
          amplitudePx: 100, phase: 0.5, style: 0, seed: 3, centerBias: 0);
      final preso = tremorSample(
          amplitudePx: 100, phase: 0.5, style: 0, seed: 3, centerBias: 1);
      expect(preso.dx.abs(), lessThan(solto.dx.abs()));
    });

    // O estilo nervoso fica QUIETO a maior parte do tempo.
    test('nervoso fica quieto na maior parte do tempo', () {
      var quietos = 0;
      for (var i = 0; i < 200; i++) {
        final s = tremorSample(
          amplitudePx: 100,
          phase: i / 10,
          style: 1,
          seed: 5,
          stillness: 0.7,
        );
        if (s.dx.abs() < 12) quietos++;
      }
      expect(quietos, greaterThan(100));
    });

    test('a ficha do Shake tem todos os parametros do documento', () {
      final p = effectSpecs[EffectType.tremor]!.params;
      for (final k in [
        'style', 'amplitude', 'frequency', 'phase', 'stillness',
        'twitch_frequency', 'drift', 'center_bias', 'z_distance',
        'motion_blur', 'blur_length', 'seed', 'edges',
        'x_random_amplitude', 'x_wave_frequency', 'x_phase',
        'y_random_amplitude', 'z_random_amplitude',
        'tilt_random_amplitude',
        'red_amplitude', 'green_phase', 'rgb_randomness', 'rgb_frequency',
      ]) {
        expect(p.containsKey(k), isTrue, reason: 'falta "$k"');
      }
    });

    test('os padroes sao os da ficha', () {
      final p = effectSpecs[EffectType.tremor]!.params;
      expect(p['frequency']!.initial, 8);
      expect(p['stillness']!.initial, 0.7);
      expect(p['twitch_frequency']!.initial, 2);
      expect(p['drift']!.initial, 0.3);
      expect(p['x_random_amplitude']!.initial, 0.2);
      expect(p['y_random_amplitude']!.initial, 0.1);
      expect(p['z_random_amplitude']!.initial, 0);
      expect(p['x_wave_frequency']!.initial, 0.5);
      expect(p['rgb_frequency']!.initial, 2);
    });

    // Renomear nao pode quebrar preset existente.
    test('as chaves antigas ainda resolvem', () {
      expect(resolveParamKey(EffectType.tremor, 'frequencia'), 'frequency');
      expect(resolveParamKey(EffectType.tremor, 'semente'), 'seed');
      expect(resolveParamKey(EffectType.motionTile, 'largura'),
          'tile_width');
      expect(resolveParamKey(EffectType.tremor, 'amplitude'), 'amplitude');
    });
  });
}
