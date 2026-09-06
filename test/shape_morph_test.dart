import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';

void main() {
  group('ShapeMorph — morphing de formas', () {
    final morph = ShapeMorph(
      from: ShapePath(primitive: ShapePrimitive.ellipse, width: 400, height: 400),
      to: ShapePath(primitive: ShapePrimitive.star, width: 400, height: 400),
      progress: AnimatedDouble(0, [
        Keyframe(time: Duration.zero, value: 0),
        Keyframe(time: const Duration(seconds: 2), value: 1),
      ]),
    );

    test('progresso 0 = contorno da origem', () {
      final b = morph.build(Duration.zero).getBounds();
      final expected =
          ShapePath(primitive: ShapePrimitive.ellipse, width: 400, height: 400)
              .build()
              .getBounds();
      expect(b.width, closeTo(expected.width, 2));
      expect(b.height, closeTo(expected.height, 2));
    });

    test('progresso 1 = contorno do destino', () {
      final b = morph.build(const Duration(seconds: 2)).getBounds();
      final star =
          ShapePath(primitive: ShapePrimitive.star, width: 400, height: 400)
              .build()
              .getBounds();
      expect(b.width, closeTo(star.width, 6));
      expect(b.height, closeTo(star.height, 6));
    });

    test('meio do caminho fica entre as duas formas e e deterministico', () {
      const t = Duration(seconds: 1);
      final b1 = morph.build(t).getBounds();
      final b2 = morph.build(t).getBounds();
      // Deterministico: mesmo frame -> mesmo contorno.
      expect(b1, b2);
      // Nao degenerado.
      expect(b1.width, greaterThan(100));
      expect(b1.height, greaterThan(100));
    });

    test('avaliacao na arvore da forma produz desenho', () {
      final draws = evaluateShape(
        [morph, ShapeFill()],
        const Duration(seconds: 1),
      );
      expect(draws, isNotEmpty);
    });

    test('coracao constroi caminho valido', () {
      final b = ShapePath(primitive: ShapePrimitive.heart)
          .build()
          .getBounds();
      expect(b.width, greaterThan(100));
      expect(b.height, greaterThan(100));
    });
  });
}
