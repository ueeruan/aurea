import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/mask.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/svg_path.dart';

void main() {
  group('SVG -> nos bezier', () {
    test('retas viram cantos, e Z fecha', () {
      final b = svgPathToBezier('M0 0 L10 0 L10 10 Z');
      expect(b.closed, isTrue);
      expect(b.vertices.length, 3);
      expect(b.vertices[1].p, const Offset(10, 0));
      expect(b.vertices.every((v) => v.corner), isTrue);
    });

    test('cubica guarda as duas tangentes, relativas a ancora', () {
      final b = svgPathToBezier('M0 0 C 10 0, 20 10, 20 20');
      expect(b.vertices.length, 2);
      expect(b.vertices[0].outT, const Offset(10, 0));
      expect(b.vertices[1].inT, const Offset(0, -10));
      expect(b.vertices[1].corner, isFalse);
    });

    test('quadratica vira cubica por elevacao de grau', () {
      // Q com controle (10,10) de (0,0) a (20,0): controles cubicos a 2/3.
      final b = svgPathToBezier('M0 0 Q 10 10 20 0');
      expect(b.vertices[0].outT.dx, closeTo(20 / 3, 1e-9));
      expect(b.vertices[0].outT.dy, closeTo(20 / 3, 1e-9));
      expect(b.vertices[1].inT.dx, closeTo(-20 / 3, 1e-9));
    });

    test('comandos relativos acumulam', () {
      final b = svgPathToBezier('m5 5 l10 0 l0 10 h-10 z');
      expect(b.vertices.map((v) => v.p).toList(), [
        const Offset(5, 5),
        const Offset(15, 5),
        const Offset(15, 15),
        const Offset(5, 15),
      ]);
    });

    test('S reflete o controle anterior', () {
      final b = svgPathToBezier('M0 0 C 0 10, 10 10, 10 0 S 20 -10, 20 0');
      // O controle de saida do segundo no e o reflexo de (10,10) em (10,0).
      expect(b.vertices[1].outT, const Offset(0, -10));
    });

    test('ultimo no em cima do primeiro e fundido ao fechar', () {
      final b = svgPathToBezier('M0 0 L10 0 L10 10 L0 0 Z');
      expect(b.vertices.length, 3);
    });

    test('so o primeiro subcaminho entra', () {
      final b = svgPathToBezier('M0 0 L10 0 L10 10 Z M50 50 L60 50 Z');
      expect(b.vertices.length, 3);
    });
  });

  group('ShapeBezier', () {
    test('interpola de retangulo a elipse por keyframe', () {
      final item = ShapeBezier(
        path: AnimatedPath(BezierPath.rect(100, 100), [
          Keyframe(time: Duration.zero, value: BezierPath.rect(100, 100)),
          Keyframe(
              time: const Duration(seconds: 1),
              value: BezierPath.ellipse(100, 100)),
        ]),
      );
      final meio = item.path.valueAt(const Duration(milliseconds: 500));
      // Contagem igualada: o retangulo tem 4, a elipse tem 4.
      expect(meio.vertices.length, 4);
      // No meio do caminho os cantos ja ganharam tangente.
      expect(meio.vertices.any((v) => v.outT != Offset.zero), isTrue);
      // E os limites ficam entre os dois.
      final b = item.buildAt(const Duration(milliseconds: 500)).getBounds();
      expect(b.width, closeTo(100, 6));
    });

    test('entra na avaliacao da forma como caminho', () {
      final draws = evaluateShape([
        ShapeBezier(path: AnimatedPath(BezierPath.rect(80, 40))),
        ShapeFill(color: const Color(0xFFFFFFFF)),
      ], Duration.zero);
      expect(draws.length, 1);
      expect(draws.first.path.getBounds().width, closeTo(80, 1e-6));
      expect(draws.first.path.getBounds().height, closeTo(40, 1e-6));
    });
  });

  group('conversao de geometria em nos', () {
    test('retangulo e elipse viram nos exatos', () {
      final r = bezierOfShapeItem(
          ShapePath(primitive: ShapePrimitive.rectangle, width: 50, height: 30),
          Duration.zero)!;
      expect(r.vertices.length, 4);
      final e = bezierOfShapeItem(
          ShapePath(primitive: ShapePrimitive.ellipse, width: 50, height: 50),
          Duration.zero)!;
      expect(e.vertices.length, 4);
      expect(e.vertices.every((v) => !v.corner), isTrue);
    });

    test('poligono tem um no por ponta', () {
      final p = bezierOfShapeItem(
          ShapePath(primitive: ShapePrimitive.polygon, points: 6, width: 100),
          Duration.zero)!;
      expect(p.vertices.length, 6);
    });

    test('anel e amostrado, e continua fechado', () {
      final a = bezierOfShapeItem(
          ShapePath(primitive: ShapePrimitive.ring, width: 100, thickness: 20),
          Duration.zero)!;
      expect(a.vertices.length, greaterThan(8));
      expect(a.closed, isTrue);
    });

    test('svg entra pelos nos e cabe na caixa', () {
      final s = bezierOfShapeItem(
          ShapeSvgPath(pathData: 'M0 0 L100 0 L100 50 Z', size: 200),
          Duration.zero)!;
      expect(s.vertices.length, 3);
      final b = s.build().getBounds();
      expect(b.width, closeTo(200, 1e-6));
      // Centralizado: o centro fica na origem.
      expect(b.center.dx, closeTo(0, 1e-6));
      expect(b.center.dy, closeTo(0, 1e-6));
    });

    test('geometria parametrica vira nos exatos, nao amostras', () {
      final rect = bezierOfShapeItem(
          ShapeParametric(
              kind: ParamShapeKind.rect,
              sizeX: AnimatedDouble(120),
              sizeY: AnimatedDouble(60)),
          Duration.zero)!;
      expect(rect.vertices.length, 4);
      expect(rect.build().getBounds().width, closeTo(120, 1e-6));

      final elipse = bezierOfShapeItem(
          ShapeParametric(kind: ParamShapeKind.ellipse), Duration.zero)!;
      expect(elipse.vertices.length, 4);
      expect(elipse.vertices.every((v) => !v.corner), isTrue);

      final estrela = bezierOfShapeItem(
          ShapeParametric(
              kind: ParamShapeKind.star, points: AnimatedDouble(5)),
          Duration.zero)!;
      expect(estrela.vertices.length, 10);

      // Arredondado: um quarto de circulo por canto, dois nos por canto.
      final arredondado = bezierOfShapeItem(
          ShapeParametric(
              kind: ParamShapeKind.rect,
              sizeX: AnimatedDouble(120),
              sizeY: AnimatedDouble(60),
              roundness: AnimatedDouble(20)),
          Duration.zero)!;
      expect(arredondado.vertices.length, 8);
      final b = arredondado.build().getBounds();
      expect(b.width, closeTo(120, 1e-6));
      expect(b.height, closeTo(60, 1e-6));
      // Os nos do arco tem alca so de um lado; o lado reto nao tem alca.
      expect(arredondado.vertices.where((v) => v.inT == Offset.zero).length,
          4);

      // Capsula: o raio satura a altura, os lados verticais somem e os
      // nos coincidentes se fundem num no liso.
      final capsula = bezierOfShapeItem(
          ShapeParametric(
              kind: ParamShapeKind.rect,
              sizeX: AnimatedDouble(200),
              sizeY: AnimatedDouble(100),
              roundness: AnimatedDouble(100)),
          Duration.zero)!;
      expect(capsula.vertices.length, 6);
      expect(capsula.vertices.where((v) => !v.corner).length, 2);
      expect(capsula.build().getBounds().width, closeTo(200, 1e-6));
    });

    test('pintura nao e geometria', () {
      expect(
          bezierOfShapeItem(
              ShapeFill(color: const Color(0xFFFFFFFF)), Duration.zero),
          isNull);
    });
  });
}
