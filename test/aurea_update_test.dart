import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/svg_path.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

void main() {
  group('SVG path -> vetor editavel (PR-A9)', () {
    test('M/L/C/Z parseia e produz bounds corretos', () {
      final p = parseSvgPathData('M0 0 L10 0 L10 10 L0 10 Z');
      final b = p.getBounds();
      expect(b.width, closeTo(10, 1e-6));
      expect(b.height, closeTo(10, 1e-6));
    });

    test('comandos relativos e curvas', () {
      final p = parseSvgPathData('m5 5 l10 0 c5 0 5 10 0 10 z');
      expect(p.getBounds().left, closeTo(5, 1e-6));
      expect(p.getBounds().width, greaterThan(10));
    });

    test('fitPathToBox centraliza e escala', () {
      final p = fitPathToBox(
          parseSvgPathData('M0 0 L24 0 L24 24 L0 24 Z'), 420);
      final b = p.getBounds();
      expect(b.width, closeTo(420, 1e-6));
      expect(b.center.dx, closeTo(0, 1e-6));
      expect(b.center.dy, closeTo(0, 1e-6));
    });

    test('icone via ShapeSvgPath entra no motor e o Trim funciona', () {
      final draws = evaluateShape([
        ShapeSvgPath(pathData: 'M0 0 L100 0 L100 100 L0 100 Z'),
        TrimOperator(end: AnimatedDouble(0.5)),
        ShapeStroke(width: AnimatedDouble(8)),
      ], Duration.zero);
      expect(draws, isNotEmpty);
    });
  });

  group('Auditoria de formas (PR-A1) — gaps corrigidos', () {
    test('formiguinha: dash offset animado desloca o tracejado', () {
      final stroke = ShapeStroke(
        dashLength: AnimatedDouble(20),
        gapLength: AnimatedDouble(10),
        dashOffset: AnimatedDouble(0, [
          Keyframe(time: Duration.zero, value: 0),
          Keyframe(time: const Duration(seconds: 1), value: 30),
        ]),
      );
      final line = ShapePath(
          primitive: ShapePrimitive.wave, width: 300, amplitude: 0);
      final at0 = evaluateShape([line, stroke], Duration.zero);
      final at500 = evaluateShape(
          [line, stroke], const Duration(milliseconds: 500));
      // O tracejado se move: a borda final do conjunto de tracos muda
      // (o inicio e grampeado no comeco do caminho).
      expect(at0.first.path.getBounds().right,
          isNot(closeTo(at500.first.path.getBounds().right, 0.5)));
    });

    test('gradiente pinta com shader', () {
      final draws = evaluateShape([
        ShapePath(primitive: ShapePrimitive.ellipse),
        ShapeGradientFill(),
      ], Duration.zero);
      expect(draws.single.paint.shader, isNotNull);
    });

    test('novas primitivas constroem caminhos validos', () {
      for (final prim in [
        ShapePrimitive.gear,
        ShapePrimitive.arrow,
        ShapePrimitive.check,
        ShapePrimitive.plus,
        ShapePrimitive.drop,
        ShapePrimitive.flower,
        ShapePrimitive.sparkle,
      ]) {
        final b = ShapePath(primitive: prim).build().getBounds();
        expect(b.width, greaterThan(50), reason: '$prim');
        expect(b.height, greaterThan(50), reason: '$prim');
      }
    });
  });

  group('Triagem 3D (PR-A7)', () {
    test('neutralidade: 3D com rotacao zero == 2D', () {
      // Perspectiva com z=0 e fator 1; sem rotacao X/Y nao ha wrapper.
      const persp = 1200 / (1200 + 0);
      expect(persp, 1.0);
      final l = ShapeLayer(
        name: 's',
        startTime: Duration.zero,
        duration: const Duration(seconds: 5),
        is3D: true,
      );
      final p = VideoProject(
          name: 'p', createdAt: DateTime(2026), layers: [l]);
      final eff = effectiveTransform(p, l, Duration.zero);
      expect(eff.rot, 0);
      expect(eff.rotX, 0);
      expect(eff.rotY, 0);
      expect(eff.scale, 1);
      expect(eff.z, 0);
    });

    test('ordenacao 3D com empate e ESTAVEL (nao pisca)', () {
      Layer mk(String id) => ShapeLayer(
            id: id,
            name: id,
            startTime: Duration.zero,
            duration: const Duration(seconds: 5),
            is3D: true,
            positionZ: AnimatedDouble(100),
          );
      final layers = [mk('a'), mk('b'), mk('c'), mk('d')];
      final s1 = depthSortPaintOrder(layers, Duration.zero);
      final s2 = depthSortPaintOrder(layers, Duration.zero);
      expect([for (final l in s1) l.id], ['a', 'b', 'c', 'd']);
      expect([for (final l in s1) l.id], [for (final l in s2) l.id]);
    });
  });
}
