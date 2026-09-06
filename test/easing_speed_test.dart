import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/keyframe.dart';

/// O grafico de velocidade desenha Easing.speedAt. Tres coisas o tornam
/// util: ser LISO (a diferenca finita em cima do solver da bezier
/// serrilhava), bater com a curva de valor (a area sob a velocidade e o
/// deslocamento, sempre 1) e nao explodir na alca degenerada.
void main() {
  group('Easing.speedAt', () {
    test('linear anda a 1 o tempo todo', () {
      const e = Easing(x1: 0, y1: 0, x2: 1, y2: 1);
      for (final x in [0.0, 0.3, 0.5, 0.9, 1.0]) {
        expect(e.speedAt(x), 1);
      }
    });

    test('ease-in-out parte parado, acelera no meio, chega parado', () {
      const e = Easing(x1: 0.42, y1: 0, x2: 0.58, y2: 1);
      expect(e.speedAt(0), closeTo(0, 1e-9));
      expect(e.speedAt(1), closeTo(0, 1e-9));
      expect(e.speedAt(0.5), greaterThan(1.2));
    });

    test('e liso: amostras vizinhas nao pulam', () {
      const e = Easing(x1: 0.42, y1: 0, x2: 0.58, y2: 1);
      const n = 200;
      var anterior = e.speedAt(0);
      for (var i = 1; i <= n; i++) {
        final v = e.speedAt(i / n);
        expect((v - anterior).abs(), lessThan(0.08),
            reason: 'salto em x=${i / n}');
        anterior = v;
      }
    });

    test('a area sob a velocidade e o deslocamento inteiro: 1', () {
      for (final e in const [
        Easing(x1: 0.42, y1: 0, x2: 0.58, y2: 1),
        Easing(x1: 0.25, y1: 0.1, x2: 0.25, y2: 1),
        Easing(x1: 0.1, y1: 0.9, x2: 0.9, y2: 0.1),
      ]) {
        const n = 4000;
        var area = 0.0;
        for (var i = 0; i < n; i++) {
          final x = (i + 0.5) / n;
          area += e.speedAt(x) / n;
        }
        expect(area, closeTo(1, 2e-3));
      }
    });

    test('alca em cima da ancora: velocidade inicial vem da outra alca', () {
      const e = Easing(x1: 0, y1: 0, x2: 0.58, y2: 1);
      expect(e.speedAt(0), closeTo(1 / 0.58, 1e-6));
      expect(e.speedAt(0).isFinite, isTrue);
    });

    test('alca vertical da velocidade infinita, nao NaN', () {
      const e = Easing(x1: 0, y1: 0.5, x2: 1, y2: 0.5);
      expect(e.speedAt(0), double.infinity);
      expect(e.speedAt(0.5).isFinite, isTrue);
    });
  });
}
