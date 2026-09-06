import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

void main() {
  group('Motor de formas vetoriais', () {
    test('circulo + fill produz um draw preenchido', () {
      final draws = evaluateShape(ShapePresets.circle(), Duration.zero);
      expect(draws.length, 1);
      expect(draws.first.paint.style.name, 'fill');
      final b = shapeBounds(draws);
      expect(b.width, closeTo(420, 1));
      expect(b.height, closeTo(420, 1));
    });

    test('trim paths reduz o comprimento do caminho', () {
      double totalLength(List<ShapeDraw> draws) {
        var sum = 0.0;
        for (final d in draws) {
          for (final m in d.path.computeMetrics()) {
            sum += m.length;
          }
        }
        return sum;
      }

      final full = evaluateShape([
        ShapePath(primitive: ShapePrimitive.ellipse),
        ShapeStroke(),
      ], Duration.zero);

      final half = evaluateShape([
        ShapePath(primitive: ShapePrimitive.ellipse),
        TrimOperator(end: AnimatedDouble(0.5)),
        ShapeStroke(),
      ], Duration.zero);

      expect(totalLength(half),
          closeTo(totalLength(full) / 2, totalLength(full) * 0.03));
    });

    test('trim animado avalia no tempo', () {
      final trim = TrimOperator(
        end: AnimatedDouble(0)
            .withKeyframe(Duration.zero, 0, Easing.linear)
            .withKeyframe(const Duration(seconds: 2), 1),
      );
      final atStart = evaluateShape([
        ShapePath(primitive: ShapePrimitive.ellipse),
        trim,
        ShapeStroke(),
      ], const Duration(milliseconds: 1));
      final atEnd = evaluateShape([
        ShapePath(primitive: ShapePrimitive.ellipse),
        trim,
        ShapeStroke(),
      ], const Duration(seconds: 2));

      double len(List<ShapeDraw> d) => d
          .expand((x) => x.path.computeMetrics())
          .fold(0.0, (s, m) => s + m.length);
      expect(len(atStart), lessThan(len(atEnd) * 0.05));
    });

    test('repeater multiplica os draws', () {
      final draws = evaluateShape([
        ShapePath(primitive: ShapePrimitive.ellipse),
        RepeaterOperator(copies: 4, dx: 100),
        ShapeFill(),
      ], Duration.zero);
      expect(draws.length, 4);
      // Copias deslocadas: bounds total cresce ~3*dx.
      final b = shapeBounds(draws);
      expect(b.width, closeTo(420 + 300, 2));
    });

    test('estrela e onda constroem paths validos', () {
      for (final preset in [ShapePresets.star(), ShapePresets.wave()]) {
        final draws = evaluateShape(preset, Duration.zero);
        expect(draws, isNotEmpty);
        expect(shapeBounds(draws).isEmpty, false);
      }
    });
  });

  group('Ordenacao 3D (D2)', () {
    Layer shape(String name, {bool is3D = false, double z = 0}) =>
        ShapeLayer(
          name: name,
          startTime: Duration.zero,
          duration: const Duration(seconds: 5),
          is3D: is3D,
          positionZ: AnimatedDouble(z),
        );

    test('camadas 2D mantem a ordem; 3D ordena por Z no trecho', () {
      final a2d = shape('a');
      final b3dFar = shape('b', is3D: true, z: 500);
      final c3dNear = shape('c', is3D: true, z: -100);
      final d2d = shape('d');

      final sorted =
          depthSortPaintOrder([a2d, b3dFar, c3dNear, d2d], Duration.zero);
      expect(sorted.map((l) => l.name).toList(), ['a', 'b', 'c', 'd']);

      // Invertendo o par 3D: quem tem Z maior pinta primeiro (mais longe).
      final sorted2 =
          depthSortPaintOrder([a2d, c3dNear, b3dFar, d2d], Duration.zero);
      expect(sorted2.map((l) => l.name).toList(), ['a', 'b', 'c', 'd']);
    });

    test('camada 2D e barreira entre trechos 3D', () {
      final x = shape('x', is3D: true, z: 0);
      final barrier = shape('m');
      final y = shape('y', is3D: true, z: 999);
      final sorted =
          depthSortPaintOrder([x, barrier, y], Duration.zero);
      // y nao pode pular a barreira mesmo estando mais longe.
      expect(sorted.map((l) => l.name).toList(), ['x', 'm', 'y']);
    });
  });

  group('Pickwhip (D4)', () {
    test('linkFor acha o vinculo da propriedade', () {
      final a = ShapeLayer(
          name: 'a',
          startTime: Duration.zero,
          duration: const Duration(seconds: 5));
      final b = ShapeLayer(
          name: 'b',
          startTime: Duration.zero,
          duration: const Duration(seconds: 5));
      final project = VideoProject(
        name: 'p',
        createdAt: DateTime(2026),
        layers: [a, b],
        links: [
          PropertyLink(
            targetLayerId: a.id,
            targetProp: LayerProp.rotation,
            sourceLayerId: b.id,
            offsetX: 15,
          ),
        ],
      );
      expect(project.linkFor(a.id, LayerProp.rotation)?.offsetX, 15);
      expect(project.linkFor(a.id, LayerProp.position), null);
      expect(project.linkFor(b.id, LayerProp.rotation), null);
    });
  });
}
