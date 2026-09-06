import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/domain/camera3d.dart';
import 'package:aurea/src/features/editor/domain/camera_cuts.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/model_asset3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/template_pack.dart';
import 'package:aurea/src/features/projects/domain/abyss_cinematic_template.dart';

void main() {
  test(
    'four native shots, a weighted editable human and no movie dependency',
    () {
      final project = buildAbyssCinematicTemplate();
      final layer = project.layers.single as Scene3DLayer;
      expect(project.fps, 24);
      expect(layer.duration, abyssDuration);
      expect(layer.allCameras.length, 4);
      expect(layer.shots.map((s) => s.time.inMilliseconds), [
        0,
        3250,
        6500,
        10500,
      ]);
      final actor = layer.scene.nodeById('abyss_explorer')!;
      expect(actor.modelAsset!.joints.length, 17);
      expect(actor.modelMotion.keys.length, 29);
      expect(actor.modelAsset!.triangleCount, greaterThan(2000));
      expect(actor.modelAsset!.triangleCount, lessThan(6000));
      expect(
        layer.scene.nodes.every((n) => n.material.imagePath == null),
        isTrue,
      );
      final start = actor.modelAsset!.evaluate(
        Duration.zero,
        actor.modelMotion,
      );
      final pose = actor.modelAsset!.evaluate(abyssTime(5), actor.modelMotion);
      expect((pose.joints[8]! - start.joints[8]!).length, greaterThan(.1));
      expect(pose.mesh.verts.expand((v) => v).every((v) => v.isFinite), isTrue);
      final back = actor.modelAsset!.evaluate(Duration.zero, actor.modelMotion);
      expect(back.mesh.verts, start.mesh.verts);
    },
  );

  test(
    'the portable project preserves rig, cameras, keys and unique project IDs',
    () {
      final source = buildAbyssCinematicTemplate();
      final pack = TemplatePack.decode(
        TemplatePack(name: source.name, project: source).encode(),
      )!;
      final layer = pack.project.layers.single as Scene3DLayer;
      expect(
        layer.scene.nodeById('abyss_explorer')!.modelAsset!.joints.length,
        17,
      );
      expect(
        layer.scene.nodeById('abyss_explorer')!.modelMotion.keys.length,
        29,
      );
      for (final t in [1.0, 4.0, 8.0, 12.0]) {
        final expected = (source.layers.single as Scene3DLayer).cameraAt(
          abyssTime(t),
        );
        final actual = layer.cameraAt(abyssTime(t));
        expect((actual.position - expected.position).length, lessThan(1e-8));
        expect((actual.up - expected.up).length, lessThan(1e-8));
      }
      expect(source.comIdNovo().id, isNot(source.comIdNovo().id));
    },
  );

  test('each shot renders the actor and cliff without non-finite screen coordinates', () {
    final layer = buildAbyssCinematicTemplate().layers.single as Scene3DLayer;
    for (final seconds in [1.0, 3.3, 5.0, 6.6, 8.0, 10.6, 12.5]) {
      final t = abyssTime(seconds);
      final frame = renderScene(
        layer.scene,
        layer.cameraAt(t),
        const Size(1280, 536),
        t,
      );
      expect(
        frame.opaque.where((p) => p.nodeId == 'abyss_explorer'),
        isNotEmpty,
        reason: 'actor in shot at $seconds',
      );
      expect(frame.opaque.where((p) => p.nodeId == 'abyss_cliffs'), isNotEmpty);
      expect(
        frame.opaque.every(
          (p) => [p.a, p.b, p.c].every((v) => v.dx.isFinite && v.dy.isFinite),
        ),
        isTrue,
      );
      expect(shotAt(layer.shots, t), isNotNull);
    }
  });

  test('fall continues across cuts and accelerates', () {
    for (final t in [3.25, 6.5, 10.5]) {
      expect(
        (abyssActorPosition(t + .001) - abyssActorPosition(t - .001)).length,
        lessThan(2),
      );
    }
    final a = abyssActorPosition(4).y - abyssActorPosition(5).y;
    final b = abyssActorPosition(8).y - abyssActorPosition(9).y;
    expect(b, greaterThan(a));
  });

  test(
    'camera roll rotates local up and remains valid looking straight down',
    () {
      final camera = Camera3D(rotZ: AnimatedDouble(90));
      final basis = cameraBasis(camera.renderAt(Duration.zero));
      expect(basis.up.x, closeTo(1, 1e-8));
      expect(basis.up.y, closeTo(0, 1e-8));
      final down = camera.copyWith(
        posY: AnimatedDouble(800),
        posZ: AnimatedDouble(0),
      );
      final b = cameraBasis(down.renderAt(Duration.zero));
      expect(b.up.length, closeTo(1, 1e-8));
      expect(b.forward.dot(b.up), closeTo(0, 1e-8));
    },
  );

  test('static environment assets reuse evaluated geometry when seeking', () {
    final layer = buildAbyssCinematicTemplate().layers.single as Scene3DLayer;
    final model = layer.scene.nodes.first.modelAsset!;
    const motion = ModelMotion3D();
    expect(
      identical(
        model.evaluate(Duration.zero, motion),
        model.evaluate(abyssTime(8), motion),
      ),
      isTrue,
    );
  });
}
