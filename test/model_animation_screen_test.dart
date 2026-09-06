import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/model_animation_screen.dart';
import 'package:aurea/src/features/editor/presentation/am/am_widgets.dart';

import 'model3d_engine_test.dart' show rigDocument, readFixture;

void main() {
  for (final size in [const Size(375, 812), const Size(430, 932)]) {
    testWidgets('rig acessivel, pose e timeline em ${size.width}', (
      tester,
    ) async {
      tester.view.physicalSize = size;
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);
      final container = ProviderContainer();
      addTearDown(container.dispose);
      final node = SceneNode(
        id: 'model',
        name: 'Rig',
        modelAsset: readFixture(rigDocument()),
      );
      container
          .read(editorControllerProvider.notifier)
          .openProject(
            VideoProject.empty('Rig').copyWith(
              layers: [
                Scene3DLayer(
                  id: 'scene',
                  name: 'Cena',
                  startTime: Duration.zero,
                  duration: const Duration(seconds: 2),
                  scene: Scene3D(nodes: [node]),
                ),
              ],
            ),
          );
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: container,
          child: MaterialApp(
            theme: ThemeData(platform: TargetPlatform.iOS),
            home: const ModelAnimationScreen(layerId: 'scene', nodeId: 'model'),
          ),
        ),
      );
      await tester.pumpAndSettle();
      expect(find.text('Clipe importado'), findsOneWidget);
      final clock = tester.widget<AmTickRuler>(
        find.byKey(const ValueKey('model-time')),
      );
      clock.onChanged(1);
      await tester.pumpAndSettle();
      final angle = find.byKey(const ValueKey('Rotacao X'));
      await tester.ensureVisible(angle);
      await tester.pumpAndSettle();
      tester.widget<AmTickRuler>(angle).onChanged(45);
      await tester.pumpAndSettle();
      final layer =
          container.read(editorControllerProvider).layerById('scene')!
              as Scene3DLayer;
      expect(layer.scene.nodes.single.modelMotion.keys.map((k) => k.seconds), [
        0,
        1,
      ]);
      expect(
        layer.scene.nodes.single.modelMotion.poseAt(1)[0]!.rotation[0],
        greaterThan(.3),
      );
      expect(tester.takeException(), isNull);
      await tester.pumpWidget(const SizedBox());
    });
  }
}
