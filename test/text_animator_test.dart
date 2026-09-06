import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/text_animator.dart';

RangeSelector rs({
  double start = 0,
  double end = 0.5,
  SelectorShape shape = SelectorShape.square,
  double smoothness = 0,
  double amount = 100,
  double easeHigh = 0,
  double easeLow = 0,
  SelectorMode mode = SelectorMode.add,
  bool randomize = false,
  int seed = 7,
}) {
  return RangeSelector(
    mode: mode,
    start: AnimatedDouble(start),
    end: AnimatedDouble(end),
    shape: shape,
    smoothness: AnimatedDouble(smoothness),
    amount: AnimatedDouble(amount),
    easeHigh: AnimatedDouble(easeHigh),
    easeLow: AnimatedDouble(easeLow),
    randomizeOrder: randomize,
    randomSeed: seed,
  );
}

List<double> coverages(TextSelector s, int n) => [
      for (var i = 0; i < n; i++) s.coverageAt(i, n, Duration.zero),
    ];

void main() {
  group('RangeSelector — tabela N=10, start=0, end=0.5', () {
    // p_i = 0.05, 0.15, ..., 0.95; unidades 0..4 dentro do intervalo.
    test('RAMP_UP', () {
      final c = coverages(rs(shape: SelectorShape.rampUp), 10);
      final expected = [0.1, 0.3, 0.5, 0.7, 0.9, 0, 0, 0, 0, 0];
      for (var i = 0; i < 10; i++) {
        expect(c[i], closeTo(expected[i], 1e-9), reason: 'unidade $i');
      }
    });

    test('RAMP_DOWN', () {
      final c = coverages(rs(shape: SelectorShape.rampDown), 10);
      final expected = [0.9, 0.7, 0.5, 0.3, 0.1, 0, 0, 0, 0, 0];
      for (var i = 0; i < 10; i++) {
        expect(c[i], closeTo(expected[i], 1e-9), reason: 'unidade $i');
      }
    });

    test('SQUARE sem smoothness', () {
      final c = coverages(rs(), 10);
      final expected = [1, 1, 1, 1, 1, 0, 0, 0, 0, 0];
      for (var i = 0; i < 10; i++) {
        expect(c[i], closeTo(expected[i].toDouble(), 1e-9),
            reason: 'unidade $i');
      }
    });

    test('TRIANGLE', () {
      final c = coverages(rs(shape: SelectorShape.triangle), 10);
      final expected = [0.2, 0.6, 1.0, 0.6, 0.2, 0, 0, 0, 0, 0];
      for (var i = 0; i < 10; i++) {
        expect(c[i], closeTo(expected[i], 1e-9), reason: 'unidade $i');
      }
    });

    test('ROUND nos pontos tabelados', () {
      final c = coverages(rs(shape: SelectorShape.round), 10);
      // c = sqrt(1 - (2t-1)^2), t = 0.1, 0.3, 0.5, 0.7, 0.9
      expect(c[0], closeTo(0.6, 1e-9));
      expect(c[2], closeTo(1.0, 1e-9));
      expect(c[4], closeTo(0.6, 1e-9));
      expect(c[5], 0);
    });

    test('SMOOTH nos pontos tabelados', () {
      final c = coverages(rs(shape: SelectorShape.smooth), 10);
      // 0.5 - 0.5*cos(2*pi*t)
      expect(c[2], closeTo(1.0, 1e-9)); // t = 0.5
      expect(c[0], closeTo(0.5 - 0.5 * 0.8090169944, 1e-6)); // t=0.1
      expect(c[5], 0);
    });

    test('amount negativo inverte a contribuicao', () {
      final c = coverages(rs(amount: -100), 10);
      expect(c[0], closeTo(-1, 1e-9));
      expect(c[9], 0);
    });
  });

  group('Combinacao de seletores (primeiro define a base)', () {
    // A: metade esquerda; B: terco central [0.35..0.65] — sobrepoe em 0.35..0.5.
    final a = rs(); // 0..0.5 square
    final b = rs(start: 0.35, end: 0.65);

    double combo(SelectorMode mode, int i) {
      final second = rs(start: 0.35, end: 0.65, mode: mode);
      return combinedCoverage([a, second], i, 10, Duration.zero);
    }

    test('ADD soma (com clamp)', () {
      expect(combo(SelectorMode.add, 1), 1); // 1 + 0
      expect(combo(SelectorMode.add, 4), 1); // 1 + 1 -> clamp
      expect(combo(SelectorMode.add, 5), 1); // 0 + 1
      expect(combo(SelectorMode.add, 8), 0);
    });

    test('SUBTRACT subtrai', () {
      expect(combo(SelectorMode.subtract, 1), 1);
      expect(combo(SelectorMode.subtract, 4), 0); // 1 - 1
      expect(combo(SelectorMode.subtract, 5), 0); // 0 - 1 -> clamp
    });

    test('INTERSECT multiplica', () {
      expect(combo(SelectorMode.intersect, 1), 0);
      expect(combo(SelectorMode.intersect, 4), 1);
      expect(combo(SelectorMode.intersect, 5), 0);
    });

    test('MIN / MAX', () {
      expect(combo(SelectorMode.min, 4), 1);
      expect(combo(SelectorMode.min, 1), 0);
      expect(combo(SelectorMode.max, 5), 1);
      expect(combo(SelectorMode.max, 8), 0);
    });

    test('DIFFERENCE = abs', () {
      expect(combo(SelectorMode.difference, 4), 0); // |1-1|
      expect(combo(SelectorMode.difference, 5), 1); // |0-1|
      expect(combo(SelectorMode.difference, 1), 1); // |1-0|
    });

    test('primeiro seletor INTERSECT ainda define a base (desvio do AE)', () {
      final first = rs(mode: SelectorMode.intersect);
      final c = combinedCoverage([first], 1, 10, Duration.zero);
      expect(c, 1); // no AE seria 0
    });

    test('b sozinho cobre o esperado', () {
      final c = coverages(b, 10);
      expect(c[2], 0); // p=0.25, fora
      expect(c[3], 1); // p=0.35, borda inclusiva (t=0)
      expect(c[4], 1); // p=0.45
      expect(c[6], 1); // p=0.65, borda inclusiva (t=1)
      expect(c[7], 0); // p=0.75, fora
    });
  });

  group('Ease high/low', () {
    test('extremos preservam 0 e 1', () {
      for (final ease in [-100.0, 0.0, 100.0]) {
        final s = rs(shape: SelectorShape.rampUp, easeLow: ease);
        expect(s.coverageAt(9, 10, Duration.zero), 0);
        final full = rs(easeHigh: ease); // square: c=1 dentro
        expect(full.coverageAt(0, 10, Duration.zero), closeTo(1, 1e-9));
      }
    });

    test('easeLow=100 achata a saida do zero', () {
      final plain = rs(shape: SelectorShape.rampUp);
      final eased = rs(shape: SelectorShape.rampUp, easeLow: 100);
      // Unidade 0 tem c=0.1: com easeLow=100 deve ficar menor.
      expect(eased.coverageAt(0, 10, Duration.zero),
          lessThan(plain.coverageAt(0, 10, Duration.zero)));
    });

    test('easeLow=-100 salta do zero', () {
      final plain = rs(shape: SelectorShape.rampUp);
      final eased = rs(shape: SelectorShape.rampUp, easeLow: -100);
      expect(eased.coverageAt(0, 10, Duration.zero),
          greaterThan(plain.coverageAt(0, 10, Duration.zero)));
    });
  });

  group('Random order deterministico', () {
    test('mesma permutacao em 100 execucoes', () {
      final first = seededPermutation(42, 12);
      for (var k = 0; k < 100; k++) {
        expect(seededPermutation(42, 12), first);
      }
    });

    test('seeds diferentes dao permutacoes diferentes', () {
      expect(seededPermutation(1, 12), isNot(seededPermutation(2, 12)));
    });

    test('cobertura com randomizeOrder e estavel', () {
      final s = rs(randomize: true, seed: 42);
      final a = coverages(s, 10);
      final b = coverages(s, 10);
      expect(a, b);
    });
  });

  group('Wiggly — invariante I1 (ruido puro)', () {
    test('frame 137 direto == recalculado', () {
      final w = WigglySelector(randomSeed: 9);
      const t = Duration(microseconds: 137 * 33333); // ~frame 137 a 30fps
      final direct = w.coverageAt(3, 10, t);
      // "Reproduzir do zero": avalia varios frames antes e o mesmo depois.
      for (var f = 0; f < 137; f++) {
        w.coverageAt(3, 10, Duration(microseconds: f * 33333));
      }
      expect(w.coverageAt(3, 10, t), direct);
    });

    test('correlation=100 -> todas as unidades iguais', () {
      final w = WigglySelector(
        correlation: AnimatedDouble(100),
        randomSeed: 5,
      );
      const t = Duration(milliseconds: 500);
      final c0 = w.coverageAt(0, 10, t);
      for (var i = 1; i < 10; i++) {
        expect(w.coverageAt(i, 10, t), closeTo(c0, 1e-12));
      }
    });

    test('correlation=0 -> unidades diferentes', () {
      final w = WigglySelector(
        correlation: AnimatedDouble(0),
        randomSeed: 5,
      );
      const t = Duration(milliseconds: 500);
      expect(w.coverageAt(0, 10, t),
          isNot(closeTo(w.coverageAt(5, 10, t), 1e-9)));
    });

    test('valueNoise01 fica em [0,1] e e continuo', () {
      for (var i = 0; i < 200; i++) {
        final v = valueNoise01(3, i * 0.37, i * 0.11);
        expect(v, inInclusiveRange(0, 1));
      }
      final a = valueNoise01(3, 1.5, 2.0);
      final b = valueNoise01(3, 1.5001, 2.0);
      expect((a - b).abs(), lessThan(0.01));
    });
  });

  group('Invariante I2 — neutralidade', () {
    test('animador sem propriedades tem cobertura mas nao efeito', () {
      final anim = TextAnimator(selectors: [rs()]);
      expect(anim.properties, isEmpty);
    });

    test('propriedade neutra nao altera o valor base', () {
      const t = Duration.zero;
      for (final type in TextAnimProp.values) {
        final p = AnimatorProperty(type: type);
        expect(p.apply(37.5, t, 1.0), closeTo(37.5, 1e-9),
            reason: 'propriedade $type');
      }
    });

    test('cobertura zero anula qualquer propriedade', () {
      const t = Duration.zero;
      final scale = AnimatorProperty(
          type: TextAnimProp.scale, value: AnimatedDouble(300));
      final pos = AnimatorProperty(
          type: TextAnimProp.positionX, value: AnimatedDouble(500));
      expect(scale.apply(100, t, 0), closeTo(100, 1e-9));
      expect(pos.apply(10, t, 0), closeTo(10, 1e-9));
    });

    test('escala e multiplicativa, posicao e aditiva', () {
      const t = Duration.zero;
      final scale = AnimatorProperty(
          type: TextAnimProp.scale, value: AnimatedDouble(200));
      expect(scale.apply(100, t, 1), closeTo(200, 1e-9));
      expect(scale.apply(100, t, 0.5), closeTo(150, 1e-9));
      final pos = AnimatorProperty(
          type: TextAnimProp.positionX, value: AnimatedDouble(40));
      expect(pos.apply(10, t, 0.5), closeTo(30, 1e-9));
    });

    test('animador desligado tem cobertura 0', () {
      final anim = TextAnimator(selectors: [rs()], enabled: false);
      expect(anim.coverageAt(0, 10, Duration.zero), 0);
    });
  });
}
