import 'dart:ui';

import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('TAMANHO != ESCALA (o teste que aprova o PR)', () {
    test('animar Tamanho 100->400 mantem o traco em 10 px', () {
      final rect = ShapeParametric(
        kind: ParamShapeKind.rect,
        sizeX: AnimatedDouble(100)
            .withKeyframe(Duration.zero, 100)
            .withKeyframe(const Duration(seconds: 1), 400),
        sizeY: AnimatedDouble(100)
            .withKeyframe(Duration.zero, 100)
            .withKeyframe(const Duration(seconds: 1), 400),
      );
      final items = [rect, ShapeStroke(width: AnimatedDouble(10), dashLength: AnimatedDouble(0))];

      final at0 = evaluateShape(items, Duration.zero);
      final at1 = evaluateShape(items, const Duration(seconds: 1));

      // Geometria cresceu 100 -> 400...
      expect(at0.single.path.getBounds().width, closeTo(100, 0.5));
      expect(at1.single.path.getBounds().width, closeTo(400, 0.5));
      // ...e o traco continua exatamente 10 px (parametro do caminho,
      // avaliado ANTES da pintura; a Escala vive na transform da camada
      // e multiplica o desenho inteiro DEPOIS — engordando o traco).
      expect(at0.single.paint.strokeWidth, 10);
      expect(at1.single.paint.strokeWidth, 10);
    });
  });

  group('arredondamento (PR-F2)', () {
    Path rectAt(double size, double round, bool percent) =>
        ShapeParametric(
          kind: ParamShapeKind.rect,
          sizeX: AnimatedDouble(size),
          sizeY: AnimatedDouble(size),
          roundness: AnimatedDouble(round),
          roundnessPercent: percent,
        ).buildAt(Duration.zero);

    test('px fixo nao acompanha o tamanho; % acompanha', () {
      // Caixa 400: canto em (200,200). Ponto de sonda (185,185).
      // 30 px  -> centro do arco (170,170), dist 21 < 30  -> DENTRO.
      // 30 %   -> raio 60, centro (140,140), dist 64 > 60 -> FORA.
      expect(rectAt(400, 30, false).contains(const Offset(185, -185)),
          isTrue);
      expect(rectAt(400, 30, true).contains(const Offset(185, -185)),
          isFalse);
    });

    test('satura em metade do menor lado (capsula), sem artefato', () {
      final p = rectAt(100, 9999, false);
      final b = p.getBounds();
      expect(b.width, closeTo(100, 0.5));
      expect(b.height, closeTo(100, 0.5));
      // Raio saturado em 50: o canto (49,-49) fica fora do circulo.
      expect(p.contains(const Offset(49, -49)), isFalse);
      expect(p.contains(Offset.zero), isTrue);
    });
  });

  group('polystar (PR-F5)', () {
    Path star(double points, {double roundOut = 0, double roundIn = 0}) =>
        ShapeParametric(
          kind: ParamShapeKind.star,
          points: AnimatedDouble(points),
          outerRadius: AnimatedDouble(100),
          innerRadius: AnimatedDouble(50),
          outerRoundness: AnimatedDouble(roundOut),
          innerRoundness: AnimatedDouble(roundIn),
        ).buildAt(Duration.zero);

    test('pontas fracionarias: 3,5 e forma intermediaria estavel', () {
      final b = star(3.5).getBounds();
      expect(b.isFinite, isTrue);
      expect(b.width, greaterThan(100));
      expect(b.width, lessThan(260));
      // Continuidade: entre 3 e 4 pontas nada explode.
      for (var p = 3.0; p <= 4.01; p += 0.1) {
        expect(star(p).getBounds().isFinite, isTrue);
      }
    });

    test('arredondamento -100 e 200 renderiza sem estourar', () {
      for (final r in const [-100.0, 200.0]) {
        final b = star(5, roundOut: r, roundIn: r).getBounds();
        expect(b.isFinite, isTrue);
        expect(b.width, greaterThan(0));
        // Alcas exageradas nunca passam de ~2x o raio da forma.
        expect(b.width, lessThan(500));
      }
    });
  });

  group('setor (PR-F5 §5.4)', () {
    ShapeParametric sector(
            {double sweep = 90, double inner = 0, double r = 100}) =>
        ShapeParametric(
          kind: ParamShapeKind.sector,
          outerRadius: AnimatedDouble(r),
          sectorInner: AnimatedDouble(inner),
          sweep: AnimatedDouble(sweep),
        );

    test('raio interno > 0 vira anel (centro vazio)', () {
      final ring = sector(sweep: 360, inner: 60).buildAt(Duration.zero);
      expect(ring.contains(Offset.zero), isFalse);
      expect(ring.contains(const Offset(80, 0)), isTrue);
    });

    test('varredura animavel e o anel de progresso', () {
      final s = ShapeParametric(
        kind: ParamShapeKind.sector,
        outerRadius: AnimatedDouble(100),
        sectorInner: AnimatedDouble(60),
        sweep: AnimatedDouble(0)
            .withKeyframe(Duration.zero, 0)
            .withKeyframe(const Duration(seconds: 1), 360),
      );
      expect(s.buildAt(Duration.zero).getBounds().isEmpty, isTrue);
      final half =
          s.buildAt(const Duration(milliseconds: 500)).getBounds();
      expect(half.isFinite, isTrue);
      final full = s.buildAt(const Duration(seconds: 1));
      expect(full.contains(const Offset(80, 0)), isTrue);
      expect(full.contains(Offset.zero), isFalse);
    });
  });

  group('pintura (PR-F3/F4)', () {
    test('regra par-impar chega ao Path do draw', () {
      final items = [
        ShapeParametric(kind: ParamShapeKind.ellipse),
        ShapeFill(evenOdd: true),
      ];
      final draws = evaluateShape(items, Duration.zero);
      expect(draws.single.path.fillType, PathFillType.evenOdd);
      final normal = evaluateShape(
          [ShapeParametric(kind: ParamShapeKind.ellipse), ShapeFill()],
          Duration.zero);
      expect(normal.single.path.fillType, PathFillType.nonZero);
    });

    test('cadeia de opacidade multiplica (camada x fill x stroke)', () {
      final items = [
        ShapeParametric(kind: ParamShapeKind.rect),
        ShapeFill(color: const Color(0xFFFFFFFF), opacity: 0.5),
        ShapeStroke(
            color: const Color(0xFFFFFFFF), opacity: AnimatedDouble(0.5), width: AnimatedDouble(4)),
      ];
      // "opacity" aqui e a da CAMADA repassada ao avaliador.
      final draws = evaluateShape(items, Duration.zero, opacity: 0.5);
      expect(draws[0].paint.color.a, closeTo(0.25, 0.01)); // fill
      expect(draws[1].paint.color.a, closeTo(0.25, 0.01)); // stroke
    });

    test('juncao/limite de miter chegam ao Paint', () {
      final draws = evaluateShape([
        ShapeParametric(kind: ParamShapeKind.rect),
        ShapeStroke(join: StrokeJoin.miter, miterLimit: 2, width: AnimatedDouble(6)),
      ], Duration.zero);
      expect(draws.single.paint.strokeJoin, StrokeJoin.miter);
      // O getter de strokeMiterLimit devolve o valor CODIFICADO do
      // engine; compara com um Paint de referencia em vez do numero cru.
      final reference = Paint()..strokeMiterLimit = 2;
      expect(draws.single.paint.strokeMiterLimit,
          reference.strokeMiterLimit);
    });
  });

  group('persistencia e timeline', () {
    test('ShapeParametric sobrevive ao roundtrip com keyframes', () {
      final layer = ShapeLayer(
        name: 'F',
        startTime: Duration.zero,
        duration: const Duration(seconds: 3),
        contents: [
          ShapeParametric(
            kind: ParamShapeKind.star,
            roundnessPercent: false,
            sizeX: AnimatedDouble(100)
                .withKeyframe(Duration.zero, 100)
                .withKeyframe(const Duration(seconds: 1), 400),
          ),
          ShapeFill(evenOdd: true),
          ShapeStroke(
              join: StrokeJoin.bevel, miterLimit: 7, opacity: AnimatedDouble(0.4)),
        ],
      );
      final back =
          layerFromJson(layerToJson(layer)) as ShapeLayer;
      final sp = back.contents[0] as ShapeParametric;
      expect(sp.kind, ParamShapeKind.star);
      expect(sp.roundnessPercent, isFalse);
      expect(sp.sizeX.keyframes.length, 2);
      expect(sp.sizeX.valueAt(const Duration(seconds: 1)), 400);
      expect((back.contents[1] as ShapeFill).evenOdd, isTrue);
      final st = back.contents[2] as ShapeStroke;
      expect(st.join, StrokeJoin.bevel);
      expect(st.miterLimit, 7);
      expect(st.opacity.base, 0.4);
    });

    test('keyframes da geometria aparecem na barra da camada', () {
      final layer = ShapeLayer(
        name: 'F',
        startTime: Duration.zero,
        duration: const Duration(seconds: 3),
        contents: [
          ShapeParametric(
            sweep: AnimatedDouble(0)
                .withKeyframe(Duration.zero, 0)
                .withKeyframe(const Duration(seconds: 2), 360),
          ),
        ],
      );
      expect(layer.hasAnimation, isTrue);
      expect(layer.keyframeTimes,
          contains(const Duration(seconds: 2)));
    });
  });
}
