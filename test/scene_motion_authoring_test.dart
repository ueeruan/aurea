import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/domain/camera3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/scene_motion.dart';
import 'package:aurea/src/features/editor/domain/preview_quality.dart';

void main() {
  const t = Duration(seconds: 2);
  test('first later edit anchors zero and interpolates', () {
    final track = editMotionValue(AnimatedDouble(10), t, 30);
    expect(track.valueAt(Duration.zero), 10);
    expect(track.valueAt(const Duration(seconds: 1)), 20);
    expect(track.valueAt(t), 30);
    final edited = editMotionValue(track, t, 50);
    expect(edited.keyframes.length, 2);
    expect(edited.valueAt(Duration.zero), 10);
  });
  test('manual base edit remains available with auto-key off', () {
    final track = editMotionValue(AnimatedDouble(10), t, 30, autoKey: false);
    expect(track.keyframes, isEmpty);
    expect(track.base, 30);
  });
  test('camera gestures preserve earlier and later keys', () {
    final camera = Camera3D(
      posX: AnimatedDouble(0)
          .withKeyframe(Duration.zero, 0)
          .withKeyframe(const Duration(seconds: 4), 40),
    );
    final changed = editCameraMotion(
      camera,
      t,
      (pose) => panCamera(pose, const Offset(-10, 0), t),
    );
    expect(changed.posX.valueAt(Duration.zero), 0);
    expect(changed.posX.valueAt(const Duration(seconds: 4)), 40);
    expect(changed.posX.valueAt(t), isNot(camera.posX.valueAt(t)));
    expect(changed.posX.hasKeyframeAt(t), isTrue);
  });
  test('screen movement is converted through rotated scaled parent', () {
    final parent = SceneNode(
      id: 'p',
      rotZ: AnimatedDouble(90),
      scale: AnimatedDouble(2),
    );
    final child = SceneNode(id: 'c', parentId: 'p');
    final scene = Scene3D(nodes: [parent, child]);
    final delta = sceneLocalDelta(scene, child, t, const Vec3(10, 0, 0));
    expect(delta.x, closeTo(0, 1e-8));
    expect(delta.y, closeTo(-5, 1e-8));
  });
  test('4K preview has bounded allocation; export keeps full resolution', () {
    expect(scenePreviewScale(2160, 3840, interacting: true) * 3840, 720);
    expect(scenePreviewScale(2160, 3840, interacting: false) * 3840, 1080);
    expect(
      scenePreviewScale(2160, 3840, interacting: true, exporting: true),
      1,
    );
    expect(scenePreviewScale(100, 100, interacting: true), 1);
  });
}
