import 'dart:math' as math;

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/grid_rig.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';

void main() {
  group('PR-G0 — segmentAt', () {
    test('informa valor anterior, seguinte e fracao com easing', () {
      final track = AnimatedDouble(1, [
        Keyframe(time: Duration.zero, value: 1),
        Keyframe(time: const Duration(seconds: 2), value: 4),
      ]);
      final seg = track.segmentAt(const Duration(seconds: 1))!;
      expect(seg.from, 1);
      expect(seg.to, 4);
      expect(seg.fraction, closeTo(0.5, 1e-9));
      // Fora de segmento: null.
      expect(track.segmentAt(const Duration(seconds: 3)), isNull);
      expect(AnimatedDouble(2).segmentAt(Duration.zero), isNull);
    });
  });

  group('PR-G1 — modos contra a formula', () {
    test('retangular: 12 assets em 4 colunas = 3 linhas centradas', () {
      final rig = GridRig(
          transition: AnimatedDouble(1),
          columns: 4,
          spacingX: AnimatedDouble(100),
          spacingY: AnimatedDouble(80));
      // i=0: col 0, row 0 -> x = -150, y = -80.
      final p0 = gridPlacementAt(rig, 0, 12, Duration.zero);
      expect(p0.pos.dx, closeTo(-150, 1e-9));
      expect(p0.pos.dy, closeTo(-80, 1e-9));
      // i=11: col 3, row 2 -> x = +150, y = +80.
      final p11 = gridPlacementAt(rig, 11, 12, Duration.zero);
      expect(p11.pos.dx, closeTo(150, 1e-9));
      expect(p11.pos.dy, closeTo(80, 1e-9));
    });

    test('radial: 8 assets, raio 300 — angulos de 45 em 45 do topo', () {
      final rig =
          GridRig(transition: AnimatedDouble(2), radius: AnimatedDouble(300));
      final p0 = gridPlacementAt(rig, 0, 8, Duration.zero);
      expect(p0.pos.dx, closeTo(0, 1e-6));
      expect(p0.pos.dy, closeTo(-300, 1e-6)); // topo
      final p2 = gridPlacementAt(rig, 2, 8, Duration.zero);
      expect(p2.pos.dx, closeTo(300, 1e-6)); // 90 graus horario
      expect(p2.pos.dy, closeTo(0, 1e-6));
    });

    test('esferico: espiral de Fibonacci sem acumulo nos polos', () {
      final rig =
          GridRig(transition: AnimatedDouble(3), radius: AnimatedDouble(200));
      final pts = [
        for (var i = 0; i < 100; i++)
          () {
            final p = gridPlacementAt(rig, i, 100, Duration.zero);
            return (p.pos.dx, p.pos.dy, p.z);
          }(),
      ];
      // Distancia minima ao vizinho mais proximo: dentro de uma faixa
      // razoavel da media (prova da distribuicao uniforme).
      final mins = <double>[];
      for (var i = 0; i < pts.length; i++) {
        var best = double.infinity;
        for (var j = 0; j < pts.length; j++) {
          if (i == j) continue;
          final dx = pts[i].$1 - pts[j].$1;
          final dy = pts[i].$2 - pts[j].$2;
          final dz = pts[i].$3 - pts[j].$3;
          final d = math.sqrt(dx * dx + dy * dy + dz * dz);
          if (d < best) best = d;
        }
        mins.add(best);
      }
      final avg = mins.reduce((a, b) => a + b) / mins.length;
      for (final m in mins) {
        expect(m, greaterThan(avg * 0.5));
        expect(m, lessThan(avg * 1.8));
      }
    });
  });

  group('PR-G2 — parametros comuns', () {
    test('stagger: primeiro em 0, ultimo exatamente no valor', () {
      final rig = GridRig(
          transition: AnimatedDouble(1), staggerDeg: AnimatedDouble(90), columns: 5);
      expect(gridPlacementAt(rig, 0, 5, Duration.zero).rotationDeg, 0);
      expect(gridPlacementAt(rig, 4, 5, Duration.zero).rotationDeg,
          closeTo(90, 1e-9));
    });

    test('shuffle e deterministico e e permutacao', () {
      final rig = GridRig(
          transition: AnimatedDouble(1), shuffle: true, seed: 42);
      final a = [for (var i = 0; i < 10; i++) gridEffectiveIndex(rig, i, 10)];
      final b = [for (var i = 0; i < 10; i++) gridEffectiveIndex(rig, i, 10)];
      expect(a, b);
      expect(a.toSet().length, 10);
      final other = GridRig(
          transition: AnimatedDouble(1), shuffle: true, seed: 43);
      final c =
          [for (var i = 0; i < 10; i++) gridEffectiveIndex(other, i, 10)];
      expect(a, isNot(c));
    });
  });

  group('PR-G5 — morph pelo caminho mais curto', () {
    test('transition 1 -> 3 no meio = media dos layouts 1 e 3, nunca o 2',
        () {
      final rig = GridRig(
        columns: 3,
        spacingX: AnimatedDouble(200),
        spacingY: AnimatedDouble(200),
        radius: AnimatedDouble(300),
        transition: AnimatedDouble(1, [
          Keyframe(time: Duration.zero, value: 1),
          Keyframe(time: const Duration(seconds: 2), value: 3),
        ]),
      );
      const mid = Duration(seconds: 1);
      final p = gridPlacementAt(rig, 0, 9, mid);

      final rect =
          gridPlacementAt(rig.copyWith(transition: AnimatedDouble(1)), 0,
              9, Duration.zero);
      final sphere =
          gridPlacementAt(rig.copyWith(transition: AnimatedDouble(3)), 0,
              9, Duration.zero);
      final radial =
          gridPlacementAt(rig.copyWith(transition: AnimatedDouble(2)), 0,
              9, Duration.zero);

      expect(p.pos.dx,
          closeTo((rect.pos.dx + sphere.pos.dx) / 2, 1e-6));
      expect(p.pos.dy,
          closeTo((rect.pos.dy + sphere.pos.dy) / 2, 1e-6));
      // E NAO passa pelo layout radial (indice 2).
      expect((p.pos - radial.pos).distance, greaterThan(40));
    });
  });

  group('parametros da grade ANIMAM (cada um com trilha propria)', () {
    test('raio com keyframes muda a posicao do asset ao longo do tempo',
        () {
      final rig = GridRig(
        transition: AnimatedDouble(2), // radial
        radius: AnimatedDouble(100, [
          Keyframe(time: Duration.zero, value: 100),
          Keyframe(time: const Duration(seconds: 2), value: 400),
        ]),
      );
      final p0 = gridPlacementAt(rig, 0, 4, Duration.zero);
      final pMid = gridPlacementAt(rig, 0, 4, const Duration(seconds: 1));
      final p1 = gridPlacementAt(rig, 0, 4, const Duration(seconds: 2));
      expect(p0.pos.dy, closeTo(-100, 1e-6));
      expect(pMid.pos.dy, closeTo(-250, 1e-6));
      expect(p1.pos.dy, closeTo(-400, 1e-6));
    });

    test('stagger animado gira progressivamente', () {
      final rig = GridRig(
        transition: AnimatedDouble(1),
        staggerDeg: AnimatedDouble(0, [
          Keyframe(time: Duration.zero, value: 0),
          Keyframe(time: const Duration(seconds: 1), value: 180),
        ]),
      );
      final r = gridPlacementAt(
              rig, 2, 3, const Duration(milliseconds: 500))
          .rotationDeg;
      expect(r, closeTo(90, 1e-6));
    });
  });

  group('keyframes de modulo aparecem na timeline', () {
    test('keyframe do morph da GRADE entra no keyframeTimes do nulo', () {
      final nl = NullLayer(
        name: 'n',
        startTime: Duration.zero,
        duration: const Duration(seconds: 5),
        grid: GridRig(
          transition: AnimatedDouble(1, [
            Keyframe(time: const Duration(seconds: 1), value: 1),
            Keyframe(time: const Duration(seconds: 3), value: 3),
          ]),
        ),
      );
      expect(nl.hasAnimation, true);
      expect(nl.keyframeTimes,
          [const Duration(seconds: 1), const Duration(seconds: 3)]);
    });

    test('keyframe do morph de FORMA entra no keyframeTimes da camada',
        () {
      final sl = ShapeLayer(
        name: 's',
        startTime: Duration.zero,
        duration: const Duration(seconds: 5),
        contents: [
          ShapeMorph(
            from: ShapePath(primitive: ShapePrimitive.ellipse),
            to: ShapePath(primitive: ShapePrimitive.star),
            progress: AnimatedDouble(0, [
              Keyframe(time: Duration.zero, value: 0),
              Keyframe(time: const Duration(seconds: 2), value: 1),
            ]),
          ),
          ShapeFill(),
        ],
      );
      expect(sl.hasAnimation, true);
      expect(sl.keyframeTimes.length, 2);
    });
  });

  group('PR-G6 — proximidade esferica 3D', () {
    GridRig rigWithProx() => GridRig(
          transition: AnimatedDouble(1),
          columns: 1,
          spacingX: AnimatedDouble(0),
          spacingY: AnimatedDouble(0),
          zDepth: AnimatedDouble(150),
          proximity: ProximityGroup(
            effector: AnimatedOffset(Offset.zero),
            radius: AnimatedDouble(200),
            falloff: AnimatedDouble(1),
            scaleMin: 1,
            scaleMax: 2,
          ),
        );

    test('asset em Z=150 com raio 200 E afetado; em Z=250 nao e', () {
      final rig = rigWithProx();
      // indice 1 -> z = 150 (dentro da esfera).
      final inside = gridPlacementAt(rig, 1, 3, Duration.zero);
      expect(inside.scale, closeTo(2, 0.05));
      // Com zDepth 250: indice 1 -> z = 250 (fora).
      final rigFar = rig.copyWith(zDepth: AnimatedDouble(250));
      final outside = gridPlacementAt(rigFar, 1, 3, Duration.zero);
      expect(outside.scale, closeTo(1, 0.05));
    });

    test('atrair move o asset na direcao do effector', () {
      final rig = GridRig(
        transition: AnimatedDouble(2),
        radius: AnimatedDouble(300),
        proximity: ProximityGroup(
          effector: AnimatedOffset(Offset.zero),
          radius: AnimatedDouble(500),
          falloff: AnimatedDouble(100),
          attract: AnimatedDouble(100),
        ),
      );
      final p = gridPlacementAt(rig, 0, 8, Duration.zero);
      // Sem atrair a posicao seria (0, -300); com atrair aproxima do 0.
      expect(p.pos.dy, greaterThan(-300));
      expect(p.pos.dy, lessThan(-150));
    });
  });
}
