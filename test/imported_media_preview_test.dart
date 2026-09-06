import 'dart:io';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/dither_layer.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:aurea/src/features/media/application/media_import_service.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:image_picker/image_picker.dart';

void main() {
  testWidgets(
    'imported photo preserves distinct pixels after decoding and switching projects',
    (tester) async {
      final container = ProviderContainer();
      final videos = VideoLayerManager();
      final playback = PlaybackController(
        vsync: TestVSync(),
        durationOf: () => const Duration(seconds: 5),
      );
      final editor = container.read(editorControllerProvider.notifier);
      late Directory folder;
      late XFile file;
      await tester.runAsync(() async {
        folder = await Directory.systemTemp.createTemp('aurea-preview-');
        final recorder = ui.PictureRecorder();
        final canvas = ui.Canvas(recorder);
        canvas.drawRect(
          const Rect.fromLTWH(0, 0, 100, 50),
          Paint()..color = Colors.red,
        );
        canvas.drawRect(
          const Rect.fromLTWH(0, 50, 100, 50),
          Paint()..color = Colors.blue,
        );
        final picture = recorder.endRecording();
        final image = await picture.toImage(100, 100);
        final data = await image.toByteData(format: ui.ImageByteFormat.png);
        final source = await File('${folder.path}/original.png')
            .writeAsBytes(data!.buffer.asUint8List());
        file = await MediaImportService(
          null,
          () async => folder,
        ).persist(XFile(source.path), image: true);
        image.dispose();
        picture.dispose();
        await source.delete();
      });
      await DitherLayer.warmUp();
      addTearDown(() async {
        playback.dispose();
        videos.dispose();
        container.dispose();
        await folder.delete(recursive: true);
      });
      editor.openProject(VideoProject.empty('Before').copyWith(aspectRatio: 1));
      editor.addShapeLayer(Duration.zero);
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: container,
          child: MaterialApp(
            home: Center(
              child: SizedBox(
                width: 240,
                height: 240,
                child: RepaintBoundary(
                  key: const ValueKey('photo-preview'),
                  child: PreviewStage(playback: playback, videos: videos),
                ),
              ),
            ),
          ),
        ),
      );
      await tester.pumpAndSettle();
      for (var n = 0; n < 3; n++) {
        editor.openProject(
          VideoProject.empty('Photo $n').copyWith(aspectRatio: 1),
        );
        editor.addImageLayer(Duration.zero, file.path, 'photo.png');
        if (n == 2) {
          editor.groupLayers([
            container.read(editorControllerProvider).layers.single.id,
          ]);
        }
        container.read(selectedLayerProvider.notifier).state = null;
        await tester.pumpAndSettle();
        for (var frame = 0; frame < 10; frame++) {
          await tester.runAsync(
            () => Future<void>.delayed(const Duration(milliseconds: 20)),
          );
          await tester.pump();
        }
        await tester.pumpAndSettle();
        await tester.runAsync(
          () => Future<void>.delayed(const Duration(milliseconds: 50)),
        );
        await tester.pumpAndSettle();
        await tester.runAsync(() async {
          final boundary = tester.renderObject<RenderRepaintBoundary>(
            find.byKey(const ValueKey('photo-preview')),
          );
          final image = await boundary.toImage();
          final bytes = (await image.toByteData(
            format: ui.ImageByteFormat.rawRgba,
          ))!.buffer.asUint8List();
          final top = (60 * image.width + 120) * 4;
          final bottom = (180 * image.width + 120) * 4;
          expect(
            bytes[top],
            greaterThan(200),
            reason: 'red top must survive import',
          );
          expect(bytes[top + 2], lessThan(80));
          expect(bytes[bottom], lessThan(80));
          expect(
            bytes[bottom + 2],
            greaterThan(200),
            reason: 'blue bottom must survive import',
          );
          image.dispose();
        });
        expect(tester.takeException(), isNull);
      }
    },
  );
}
