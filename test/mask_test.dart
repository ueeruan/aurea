import 'dart:convert';
import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/mask.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

void main() {
  group('BezierPath (PR-M1)', () {
    test('contagens iguais interpolam vertice a vertice', () {
      final a = BezierPath.rect(100, 100);
      final b = BezierPath.rect(100, 100, center: const Offset(50, 0));
      final mid = BezierPath.lerp(a, b, 0.5);
      expect(mid.vertices.length, 4);
      expect(mid.build().getBounds().center.dx, closeTo(25, 1e-6));
    });

    test('contagens diferentes: quadros inicial e final NAO deformam', () {
      final tri = BezierPath(vertices: [
        const PathVertex(p: Offset(0, -100)),
        const PathVertex(p: Offset(100, 100)),
        const PathVertex(p: Offset(-100, 100)),
      ]);
      final rect = BezierPath.rect(200, 200);
      final at0 = BezierPath.lerp(tri, rect, 0.0001).build().getBounds();
      final at1 = BezierPath.lerp(tri, rect, 0.9999).build().getBounds();
      expect(at0.width, closeTo(200, 1.0));
      expect(at0.height, closeTo(200, 1.0));
      expect(at1.width, closeTo(200, 1.0));
      expect(at1.height, closeTo(200, 1.0));
      // Meio do caminho: nao degenerado.
      final mid = BezierPath.lerp(tri, rect, 0.5).build().getBounds();
      expect(mid.width, greaterThan(100));
    });

    test('sentidos opostos sao detectados e corrigidos', () {
      final a = BezierPath.rect(100, 100);
      final b = BezierPath.rect(100, 100).reversed();
      expect(a.signedArea() * b.signedArea(), lessThan(0));
      // lerp nao explode nem degenera com sentidos opostos.
      final mid = BezierPath.lerp(a, b, 0.5).build().getBounds();
      expect(mid.width, closeTo(100, 5));
    });

    test('subdivisao preserva a forma (split em t=0,5)', () {
      final circle = BezierPath.ellipse(200, 200);
      final more = circle.withVertexCount(12);
      expect(more.vertices.length, 12);
      final b0 = circle.build().getBounds();
      final b1 = more.build().getBounds();
      expect(b1.width, closeTo(b0.width, 0.5));
      expect(b1.height, closeTo(b0.height, 0.5));
    });

    test('caminho animavel com easing por segmento', () {
      final anim = AnimatedPath(BezierPath.rect(100, 100))
          .withKeyframe(Duration.zero, BezierPath.rect(100, 100))
          .withKeyframe(const Duration(seconds: 2),
              BezierPath.rect(100, 100, center: const Offset(100, 0)));
      final mid = anim.valueAt(const Duration(seconds: 1));
      expect(mid.build().getBounds().center.dx, closeTo(50, 1e-6));
    });
  });

  group('Trim Paths (PR-M8)', () {
    Path line(double x0, double x1) =>
        Path()..moveTo(x0, 0)..lineTo(x1, 0);

    test('Individually apara cada caminho separadamente', () {
      final trim = TrimOperator(end: AnimatedDouble(0.5));
      final out = trim.apply([line(0, 100), line(0, 100)], Duration.zero);
      expect(out.length, 2);
      for (final p in out) {
        expect(p.getBounds().width, closeTo(50, 1));
      }
    });

    test('Simultaneously trata tudo como um comprimento so', () {
      final trim = TrimOperator(
          end: AnimatedDouble(0.5), individually: false);
      final out =
          trim.apply([line(0, 100), line(200, 300)], Duration.zero);
      // Metade do total (200) = so o primeiro caminho inteiro.
      var totalLen = 0.0;
      for (final p in out) {
        totalLen += p.getBounds().width;
      }
      expect(totalLen, closeTo(100, 2));
      expect(out.length, 1);
    });
  });

  group('Mascaras e matte (PR-M2/M5) — persistencia', () {
    test('mascaras e matte sobrevivem ao round-trip', () {
      final layer = ShapeLayer(
        name: 'S',
        startTime: Duration.zero,
        duration: const Duration(seconds: 5),
        masks: [
          LayerMask(
            name: 'Recorte',
            mode: MaskMode.subtract,
            inverted: true,
            path: AnimatedPath(BezierPath.ellipse(300, 300))
                .withKeyframe(Duration.zero, BezierPath.ellipse(300, 300))
                .withKeyframe(const Duration(seconds: 1),
                    BezierPath.rect(300, 300)),
            feather: AnimatedDouble(24),
            expansion: AnimatedDouble(-10),
          ),
        ],
        matteMode: MatteMode.luma,
        matteSourceId: 'fonte-x',
      );
      final project = VideoProject(
        name: 'p',
        createdAt: DateTime(2026),
        layers: [layer],
      );
      final back = projectFromJson(
          jsonDecode(jsonEncode(projectToJson(project)))
              as Map<String, dynamic>);
      final l = back.layers.single;
      expect(l.matteMode, MatteMode.luma);
      expect(l.matteSourceId, 'fonte-x');
      final m = l.masks.single;
      expect(m.mode, MaskMode.subtract);
      expect(m.inverted, true);
      expect(m.feather.base, 24);
      expect(m.expansion.base, -10);
      expect(m.path.keyframes.length, 2);
      // O caminho animado avalia igual apos o round-trip.
      final mid = m.path
          .valueAt(const Duration(milliseconds: 500))
          .build()
          .getBounds();
      final midOrig = layer.masks.single.path
          .valueAt(const Duration(milliseconds: 500))
          .build()
          .getBounds();
      expect(mid.width, closeTo(midOrig.width, 1e-6));
    });
  });

  group('Feather por eixo', () {
    test('por padrao os eixos estao ligados', () {
      final m = LayerMask(feather: AnimatedDouble(30));
      expect(m.featherLinked, isTrue);
      expect(m.featherY, isNull);
      expect(m.featherVertical.valueAt(Duration.zero), 30);
    });

    // Soltar nao pode mudar a imagem: o eixo Y comeca no valor que ja
    // estava valendo.
    test('soltar comeca no valor que ja valia', () {
      final m = LayerMask(feather: AnimatedDouble(30));
      final solto = m.copyWith(featherY: m.feather);
      expect(solto.featherLinked, isFalse);
      expect(solto.featherVertical.valueAt(Duration.zero), 30);
    });

    test('solto, os eixos andam separados', () {
      final m = LayerMask(
        feather: AnimatedDouble(10),
        featherY: AnimatedDouble(80),
      );
      expect(m.feather.valueAt(Duration.zero), 10);
      expect(m.featherVertical.valueAt(Duration.zero), 80);
    });

    test('religar apaga o eixo Y', () {
      final m = LayerMask(
        feather: AnimatedDouble(10),
        featherY: AnimatedDouble(80),
      );
      final ligado = m.copyWith(linkFeather: true);
      expect(ligado.featherY, isNull);
      expect(ligado.featherVertical.valueAt(Duration.zero), 10);
    });

    test('animar so o eixo Y ja conta como animacao', () {
      final m = LayerMask(
        feather: AnimatedDouble(10),
        featherY: AnimatedDouble(0)
            .withKeyframe(Duration.zero, 0)
            .withKeyframe(const Duration(seconds: 1), 60),
      );
      expect(m.hasAnimation, isTrue);
    });
  });
}
