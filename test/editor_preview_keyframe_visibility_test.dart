import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/camera3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';

// A previa que nao muda com selecao e paineis esta em
// test/editor_responsive_frame_test.dart; os losangos da timeline nova
// (tocar leva o cabecote, arrastar move) em test/ui/timeline/timeline_test.dart.
void main() {
  test('scene focus and lighting keys appear in timeline times', () {
    final layer = Scene3DLayer(
      name: 'Cena',
      startTime: Duration.zero,
      duration: const Duration(seconds: 5),
      camera: Camera3D(
        dof: DepthOfField(
          focusDistance: AnimatedDouble(900)
              .withKeyframe(const Duration(seconds: 1), 500),
          highlightGain: AnimatedDouble(0)
              .withKeyframe(const Duration(seconds: 2), 1),
        ),
      ),
      scene: Scene3D(
        lights: [
          Light3D(
            intensity: AnimatedDouble(1)
                .withKeyframe(const Duration(seconds: 3), 2),
          ),
        ],
      ),
    );
    expect(
      layer.keyframeTimes,
      containsAll([
        const Duration(seconds: 1),
        const Duration(seconds: 2),
        const Duration(seconds: 3),
      ]),
    );
  });
}
