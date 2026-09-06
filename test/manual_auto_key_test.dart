import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';

void main() {
  test(
    'auto-key uses layer-local time, preserves start, and undo restores pose',
    () {
      final ref = ProviderContainer();
      addTearDown(ref.dispose);
      final editor = ref.read(editorControllerProvider.notifier);
      editor.openProject(
        ref
            .read(editorControllerProvider)
            .copyWith(
              layers: [
                ShapeLayer(
                  id: 'shape',
                  name: 'Motion',
                  startTime: const Duration(seconds: 3),
                  duration: const Duration(seconds: 5),
                  position: AnimatedOffset(const Offset(10, 20)),
                ),
              ],
            ),
      );
      ref.read(autoKeyframeProvider.notifier).state = true;
      editor.editPosition(
        'shape',
        const Duration(seconds: 5),
        const Offset(50, 60),
      );
      final layer = ref.read(editorControllerProvider).layerById('shape')!;
      expect(layer.position.valueAt(Duration.zero), const Offset(10, 20));
      expect(
        layer.position.valueAt(const Duration(seconds: 2)),
        const Offset(50, 60),
      );
      expect(layer.position.keyframes.length, 2);
      editor.undo();
      expect(
        ref
            .read(editorControllerProvider)
            .layerById('shape')!
            .position
            .isAnimated,
        isFalse,
      );
    },
  );
  test('auto-key rotation records all axes and preserves initial scale', () {
    final ref = ProviderContainer();
    addTearDown(ref.dispose);
    final editor = ref.read(editorControllerProvider.notifier);
    editor.addShapeLayer(Duration.zero);
    final id = ref.read(editorControllerProvider).layers.first.id;
    ref.read(autoKeyframeProvider.notifier).state = true;
    const time = Duration(seconds: 1);
    editor.editRotation(id, time, 90);
    editor.editScaleUniform(id, time, 2);
    final layer = ref.read(editorControllerProvider).layerById(id)!;
    expect(layer.rotation.valueAt(Duration.zero), 0);
    expect(layer.rotation.valueAt(time), 90);
    expect(layer.rotationX.hasKeyframeAt(time), isTrue);
    expect(layer.rotationY.hasKeyframeAt(time), isTrue);
    expect(layer.scaleX.valueAt(Duration.zero), 1);
    expect(layer.scaleX.valueAt(time), 2);
  });
}
