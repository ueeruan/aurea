import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/presentation/am/am_widgets.dart';
import 'package:aurea/src/features/editor/presentation/am/param_sheet_shell.dart';
import 'package:aurea/src/features/editor/presentation/am/scene3d_sheet.dart';
import 'package:aurea/src/features/editor/presentation/am/model_animation_screen.dart';
import 'package:aurea/src/features/editor/presentation/widgets/scene3d_painter.dart';
import 'package:aurea/src/features/projects/domain/abyss_cinematic_template.dart';

class _Host extends ConsumerWidget {
  const _Host();
  @override
  Widget build(BuildContext context, WidgetRef ref) => Scaffold(
    body: TextButton(
      onPressed: () => showScene3DSheet(context, ref, 'abyss_scene'),
      child: const Text('Abrir cena de teste'),
    ),
  );
}

void main() {
  testWidgets('rig preview follows the active cinematic shot when scrubbing', (
    tester,
  ) async {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final project = buildAbyssCinematicTemplate();
    container.read(editorControllerProvider.notifier).openProject(project);
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          home: const ModelAnimationScreen(
            layerId: 'abyss_scene',
            nodeId: 'abyss_explorer',
          ),
        ),
      ),
    );
    tester
        .widget<AmTickRuler>(find.byKey(const ValueKey('model-time')))
        .onChanged(8);
    await tester.pumpAndSettle();
    final paint = tester.widget<CustomPaint>(
      find.byWidgetPredicate(
        (w) => w is CustomPaint && w.painter is Scene3DPainter,
      ),
    );
    final camera = (paint.painter! as Scene3DPainter).resolvedCamera!;
    final expected = (project.layers.single as Scene3DLayer).cameraAt(
      abyssTime(8),
    );
    expect((camera.position - expected.position).length, lessThan(1e-8));
    expect(tester.takeException(), isNull);
  });
  for (final width in [375.0, 430.0]) {
    testWidgets('ABISMO atmosphere controls open, mutate and close at $width', (
      tester,
    ) async {
      RecentSheets.instance.clear();
      addTearDown(RecentSheets.instance.clear);
      tester.view.physicalSize = Size(width, 900);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      final container = ProviderContainer();
      addTearDown(container.dispose);
      container
          .read(editorControllerProvider.notifier)
          .openProject(buildAbyssCinematicTemplate());
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: container,
          child: MaterialApp(
            theme: ThemeData(platform: TargetPlatform.iOS),
            home: const _Host(),
          ),
        ),
      );
      await tester.tap(find.text('Abrir cena de teste'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Ambiente'));
      await tester.pumpAndSettle();
      await tester.ensureVisible(find.text('Nevoa por distancia'));
      await tester.pumpAndSettle();
      final row = find
          .ancestor(
            of: find.text('Nevoa por distancia'),
            matching: find.byType(Row),
          )
          .first;
      await tester.tap(
        find.descendant(of: row, matching: find.byType(CupertinoSwitch)),
      );
      await tester.pumpAndSettle();
      final scene =
          (container.read(editorControllerProvider).layers.single
                  as Scene3DLayer)
              .scene;
      expect(scene.fogDensity, 0);
      expect(scene.nodes.length, greaterThan(10));
      expect(tester.takeException(), isNull);
      closeParamSheet(tester.element(find.byType(ParamSheetShell)));
      await tester.pumpAndSettle();
      expect(find.text('Abrir cena de teste'), findsOneWidget);
      expect(tester.takeException(), isNull);
    });
  }
}
