import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/camera3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/editor/presentation/am/am_timeline.dart';
import 'package:aurea/src/features/editor/presentation/am/transform_panel.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';

class _Projects extends ProjectsController {
  @override
  List<VideoProject> build() => [];
  @override
  void upsert(VideoProject project) => state = [project];
}

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

  testWidgets('selecting an offscreen layer reveals its keyframes', (
    tester,
  ) async {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    container
        .read(editorControllerProvider.notifier)
        .openProject(
          VideoProject.empty('Many layers').copyWith(
            layers: [
              for (var i = 0; i < 200; i++)
                ShapeLayer(
                  id: 'layer-$i',
                  name: 'Layer $i',
                  startTime: Duration.zero,
                  duration: const Duration(seconds: 5),
                  rotation: AnimatedDouble(0).withKeyframe(Duration.zero, 0),
                ),
            ],
          ),
        );
    final playback = PlaybackController(
      vsync: TestVSync(),
      durationOf: () => const Duration(seconds: 5),
    );
    addTearDown(playback.dispose);
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          home: Scaffold(
            body: Center(child: AmTimeline(playback: playback, height: 140)),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();
    final glyph = find.byKey(const ValueKey('keyframe-glyph-layer-199-0'));
    expect(glyph, findsNothing);
    container.read(selectedLayerProvider.notifier).state = 'layer-199';
    await tester.pumpAndSettle();
    expect(glyph.hitTestable(), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  for (final size in [
    const Size(375, 667),
    const Size(390, 844),
    const Size(430, 932),
  ]) {
    testWidgets('preview stays the same through selection and tools at $size', (
      tester,
    ) async {
      tester.view.physicalSize = size;
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      final container = ProviderContainer(
        overrides: [projectsControllerProvider.overrideWith(_Projects.new)],
      );
      addTearDown(container.dispose);
      final editor = container.read(editorControllerProvider.notifier);
      editor.addShapeLayer(Duration.zero, name: 'Forma');
      final project = container.read(editorControllerProvider);
      editor.openProject(project.copyWith(aspectRatio: 9 / 16));
      final id = project.layers.first.id;
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: container,
          child: MaterialApp(
            theme: ThemeData(platform: TargetPlatform.iOS),
            home: const EditorScreen(),
          ),
        ),
      );
      await tester.pumpAndSettle();
      final before = tester.getRect(find.byType(PreviewStage));
      expect(before.height, greaterThan(size.height * .4));
      container.read(selectedLayerProvider.notifier).state = id;
      await tester.pumpAndSettle();
      expect(tester.getRect(find.byType(PreviewStage)), before);
      await tester.tap(find.text('Mover e\ntransf.'));
      await tester.pumpAndSettle();
      expect(find.byType(TransformPanel), findsOneWidget);
      expect(tester.getRect(find.byType(PreviewStage)), before);
      final pad = find.byKey(const ValueKey('position-drag-pad'));
      expect(pad.hitTestable(), findsOneWidget);
      final startPosition = container
          .read(editorControllerProvider)
          .layerById(id)!
          .position
          .valueAt(Duration.zero);
      final gesture = await tester.startGesture(tester.getCenter(pad));
      await gesture.moveBy(const Offset(20, 0));
      await tester.pump();
      await gesture.moveBy(const Offset(40, 0));
      await gesture.up();
      await tester.pumpAndSettle();
      expect(
        container
            .read(editorControllerProvider)
            .layerById(id)!
            .position
            .valueAt(Duration.zero),
        isNot(startPosition),
        reason:
            'pad=${tester.getRect(pad)}, scroll=${tester.widget<TransformPanel>(find.byType(TransformPanel)).playback.time.value}',
      );
      await tester.tap(find.byTooltip('Adicionar keyframe neste instante'));
      await tester.pumpAndSettle();
      final glyph = find.byKey(ValueKey('keyframe-glyph-$id-0'));
      expect(glyph, findsOneWidget);
      expect(tester.getSize(glyph), const Size(10, 10));
      await tester.tap(find.text('Girar').first);
      await tester.pumpAndSettle();
      expect(glyph, findsOneWidget, reason: 'other-property keys stay visible');
      expect(tester.getRect(find.byType(PreviewStage)), before);
      expect(tester.takeException(), isNull);
    });
  }

  testWidgets(
    'single keys have painted area and remain tappable over trim handles',
    (tester) async {
      final container = ProviderContainer();
      addTearDown(container.dispose);
      final editor = container.read(editorControllerProvider.notifier);
      editor.openProject(
        VideoProject.empty('Timeline').copyWith(
          layers: [
            ShapeLayer(
              id: 'keyed',
              name: 'Keyed',
              startTime: const Duration(seconds: 1),
              duration: const Duration(seconds: 2),
              rotation: AnimatedDouble(0)
                  .withKeyframe(Duration.zero, 0)
                  .withKeyframe(const Duration(seconds: 2), 90),
            ),
          ],
        ),
      );
      container.read(selectedLayerProvider.notifier).state = 'keyed';
      final playback = PlaybackController(
        vsync: TestVSync(),
        durationOf: () => const Duration(seconds: 5),
      );
      addTearDown(playback.dispose);
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: container,
          child: MaterialApp(
            home: Scaffold(body: AmTimeline(playback: playback)),
          ),
        ),
      );
      await tester.pumpAndSettle();
      for (final us in [0, 2000000]) {
        playback.seek(Duration(microseconds: 1000000 + us));
        await tester.pumpAndSettle();
        final glyph = find.byKey(ValueKey('keyframe-glyph-keyed-$us'));
        expect(tester.getSize(glyph), const Size(10, 10));
        playback.seek(Duration(microseconds: 1400000 + us));
        await tester.pumpAndSettle();
        await tester.tap(glyph);
        await tester.pumpAndSettle();
        expect(playback.time.value, Duration(microseconds: 1000000 + us));
      }
      expect(tester.takeException(), isNull);
    },
  );
}
