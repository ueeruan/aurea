import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/freehand_session.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_hierarchy_test.dart' show openEditor;

const _captureKey = ValueKey('freehand-preview-capture');

class _Preview {
  final container = ProviderContainer();
  final videos = VideoLayerManager();
  late final playback = PlaybackController(
    vsync: TestVSync(),
    durationOf: () => container.read(editorControllerProvider).duration,
  );
  EditorController get editor =>
      container.read(editorControllerProvider.notifier);

  Future<void> mount(WidgetTester tester) async {
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          home: Scaffold(
            body: Center(
              child: SizedBox(
                width: 360,
                height: 400,
                child: RepaintBoundary(
                  key: _captureKey,
                  child: PreviewStage(playback: playback, videos: videos),
                ),
              ),
            ),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();
  }

  Future<void> drawMode(WidgetTester tester) async {
    container.read(freehandRequestProvider.notifier).state = true;
    await tester.pumpAndSettle();
  }

  void dispose() {
    playback.dispose();
    videos.dispose();
    container.dispose();
  }
}

Future<List<int>> _centerPixel(WidgetTester tester) async {
  return (await tester.runAsync(() async {
    final boundary = tester.renderObject<RenderRepaintBoundary>(
      find.byKey(_captureKey),
    );
    final image = await boundary.toImage();
    try {
      final bytes = (await image.toByteData(
        format: ui.ImageByteFormat.rawRgba,
      ))!.buffer.asUint8List();
      final index = ((image.height ~/ 2) * image.width + image.width ~/ 2) * 4;
      return bytes.sublist(index, index + 4);
    } finally {
      image.dispose();
    }
  }))!;
}

void main() {
  testWidgets(
    'drawing does not tint the preview or leak into another project',
    (tester) async {
      final p = _Preview();
      addTearDown(p.dispose);
      p.editor.openProject(VideoProject.empty('A'));
      await p.mount(tester);
      final normal = await _centerPixel(tester);
      p.container.read(onionSkinProvider.notifier).state = 2;
      await p.drawMode(tester);
      expect(
        await _centerPixel(tester),
        normal,
        reason: 'no green/brown drawing veil',
      );
      expect(find.byTooltip('Cancelar desenho livre'), findsOneWidget);
      p.editor.openProject(VideoProject.empty('B'));
      await tester.pumpAndSettle();
      expect(p.container.read(freehandRequestProvider), isFalse);
      expect(
        p.container.read(onionSkinProvider),
        0,
        reason: 'ghost frames also belong only to the current session',
      );
      expect(find.byKey(const ValueKey('freehand-canvas')), findsNothing);
      expect(await _centerPixel(tester), normal);
      expect(tester.takeException(), isNull);
    },
  );

  for (final aspect in [16 / 9, 9 / 16, 1.0]) {
    testWidgets(
      'finishing a stroke creates one shape without moving selection at $aspect',
      (tester) async {
        final p = _Preview();
        addTearDown(p.dispose);
        p.editor.openProject(
          VideoProject.empty('Drawing').copyWith(aspectRatio: aspect),
        );
        p.editor.addShapeLayer(Duration.zero);
        final original = p.container
            .read(editorControllerProvider)
            .layers
            .single;
        await p.mount(tester);
        await p.drawMode(tester);
        final canvas = find.byKey(const ValueKey('freehand-canvas'));
        final gesture = await tester.startGesture(
          tester.getCenter(canvas) - const Offset(40, 0),
        );
        await gesture.moveBy(const Offset(30, 20));
        await tester.pump();
        await gesture.moveBy(const Offset(50, -10));
        await gesture.up();
        await tester.pumpAndSettle();
        final project = p.container.read(editorControllerProvider);
        expect(project.layers.length, 2);
        expect(project.layers.first, isA<ShapeLayer>());
        expect(project.layers.first.name, startsWith('Desenho livre'));
        expect(
          project.layerById(original.id)!.position.valueAt(Duration.zero),
          original.position.valueAt(Duration.zero),
        );
        expect(p.container.read(freehandRequestProvider), isFalse);
        expect(canvas, findsNothing);
        await p.drawMode(tester);
        p.editor.openProject(project);
        await tester.pumpAndSettle();
        expect(
          p.container.read(freehandRequestProvider),
          isFalse,
          reason: 'reopening the same project also ends the drawing session',
        );
        expect(tester.takeException(), isNull);
      },
    );
  }

  testWidgets(
    'cancelled and interrupted strokes never enter the next project',
    (tester) async {
      final p = _Preview();
      addTearDown(p.dispose);
      await p.mount(tester);
      await p.drawMode(tester);
      var gesture = await tester.startGesture(
        tester.getCenter(find.byKey(const ValueKey('freehand-canvas'))),
      );
      await gesture.moveBy(const Offset(60, 20));
      await tester.pump();
      await gesture.cancel();
      await tester.pumpAndSettle();
      expect(p.container.read(editorControllerProvider).layers, isEmpty);
      expect(p.container.read(freehandRequestProvider), isFalse);
      await p.drawMode(tester);
      gesture = await tester.startGesture(
        tester.getCenter(find.byKey(const ValueKey('freehand-canvas'))),
      );
      await gesture.moveBy(const Offset(60, 20));
      await tester.pump();
      p.editor.openProject(VideoProject.empty('Next'));
      await tester.pumpAndSettle();
      await gesture.up();
      await tester.pumpAndSettle();
      expect(p.container.read(editorControllerProvider).layers, isEmpty);
      expect(p.container.read(freehandRequestProvider), isFalse);
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets('cancel button and leaving the preview release drawing state', (
    tester,
  ) async {
    final p = _Preview();
    addTearDown(p.dispose);
    await p.mount(tester);
    await p.drawMode(tester);
    await tester.tap(find.byTooltip('Cancelar desenho livre'));
    await tester.pumpAndSettle();
    expect(p.container.read(freehandRequestProvider), isFalse);
    await p.drawMode(tester);
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: p.container,
        child: const MaterialApp(home: SizedBox()),
      ),
    );
    await tester.pumpAndSettle();
    await tester.pump(const Duration(milliseconds: 1));
    await p.mount(tester);
    expect(p.container.read(freehandRequestProvider), isFalse);
    expect(find.byKey(const ValueKey('freehand-canvas')), findsNothing);
    expect(tester.takeException(), isNull);
  });

  testWidgets('back cancels drawing before leaving the editor', (tester) async {
    final container = await openEditor(tester, size: const Size(375, 667));
    container.read(freehandRequestProvider.notifier).state = true;
    await tester.pumpAndSettle();
    await tester.binding.handlePopRoute();
    await tester.pumpAndSettle();
    expect(container.read(freehandRequestProvider), isFalse);
    expect(find.byType(EditorScreen), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}
