import 'dart:ui';

import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/grid_rig.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  group('malhas dos elementos 3D', () {
    test('toda malha tem vertices e faces com indices validos', () {
      for (final kind in Element3DKind.values) {
        final mesh = element3DMesh(kind);
        expect(mesh.verts, isNotEmpty, reason: '$kind sem vertices');
        expect(mesh.faces, isNotEmpty, reason: '$kind sem faces');
        for (final face in mesh.faces) {
          expect(face.length, greaterThanOrEqualTo(3),
              reason: '$kind tem face degenerada');
          for (final i in face) {
            expect(i, inInclusiveRange(0, mesh.verts.length - 1),
                reason: '$kind tem indice fora da malha');
          }
        }
      }
    });

    test('malha e cacheada (mesma instancia por kind)', () {
      expect(identical(element3DMesh(Element3DKind.cube),
          element3DMesh(Element3DKind.cube)), isTrue);
    });
  });

  group('Element3DLayer', () {
    Element3DLayer make() => Element3DLayer(
          name: 'Cubo 1',
          startTime: Duration.zero,
          duration: const Duration(seconds: 5),
          kind: Element3DKind.diamond,
          size: 260,
          color: const Color(0xFFB8FF3D),
          edges: false,
        );

    test('serializa e volta identico', () {
      final layer = make();
      final back = layerFromJson(layerToJson(layer));
      expect(back, isA<Element3DLayer>());
      final e = back as Element3DLayer;
      expect(e.kind, Element3DKind.diamond);
      expect(e.size, 260);
      expect(e.color, const Color(0xFFB8FF3D));
      expect(e.edges, isFalse);
      expect(e.name, 'Cubo 1');
    });

    test('copyElement3D preserva id e troca so o pedido', () {
      final layer = make();
      final out = layer.copyElement3D(kind: Element3DKind.torus);
      expect(out.id, layer.id);
      expect(out.kind, Element3DKind.torus);
      expect(out.size, 260);
    });
  });

  group('nulo controlador da grade', () {
    test('modulacao neutra nao muda nada (I2)', () {
      final rig = GridRig(assets: const ['a', 'b', 'c', 'd'],
          transition: AnimatedDouble(2));
      final base = gridPlacementAt(rig, 0, 4, Duration.zero);
      final mod = gridPlacementAt(rig, 0, 4, Duration.zero,
          spacingMul: 1, rotationAdd: 0, twistAdd: 0);
      expect(mod.pos, base.pos);
      expect(mod.rotationDeg, base.rotationDeg);
    });

    test('escala do nulo multiplica o raio', () {
      final rig = GridRig(assets: const ['a', 'b', 'c', 'd'],
          transition: AnimatedDouble(2));
      final p = gridPlacementAt(rig, 0, 4, Duration.zero,
          spacingMul: 2);
      // Radial, i=0 fica no topo: raio 320 * 2 = 640.
      expect(p.pos.dy, closeTo(-640, 1e-6));
    });

    test('rotacao Z do nulo gira a disposicao; rotY soma no twist', () {
      final rig = GridRig(assets: const ['a', 'b', 'c', 'd'],
          transition: AnimatedDouble(2));
      final p = gridPlacementAt(rig, 0, 4, Duration.zero,
          rotationAdd: 90, twistAdd: 15);
      // Topo girado 90 graus (horario) vai parar na direita.
      expect(p.pos.dx, closeTo(320, 1e-6));
      expect(p.pos.dy.abs(), lessThan(1e-6));
      expect(p.rotationDeg, closeTo(15, 1e-6));
    });

    test('controllerId sobrevive ao roundtrip do projeto', () {
      final owner = NullLayer(
        name: 'Nulo 1',
        startTime: Duration.zero,
        duration: const Duration(seconds: 5),
        grid: GridRig(assets: const ['x'], controllerId: 'ctrl-9'),
      );
      final project = VideoProject(
          name: 'p', createdAt: DateTime(2026), layers: [owner]);
      final back = projectFromJson(projectToJson(project));
      final nl = back.layers.single as NullLayer;
      expect(nl.grid!.controllerId, 'ctrl-9');
    });

    test('gridTrackOf cobre o morph (curve editor por parametro)', () {
      final rig = GridRig(assets: const ['x']);
      expect(gridTrackOf(rig, 'transition'), same(rig.transition));
      final swapped =
          gridWithTrack(rig, 'transition', AnimatedDouble(3));
      expect(swapped.transition.valueAt(Duration.zero), 3);
    });
  });
}
