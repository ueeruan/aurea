import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/template_pack.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:aurea/src/features/projects/domain/abyss_cinematic_template.dart';

void main() {
  testWidgets(
    'render ABISMO using the native app composition and active cameras',
    (tester) async {
      if (Platform.environment['AUREA_ABYSS_RENDER'] != '1') return;
      final project = buildAbyssCinematicTemplate();
      final container = ProviderContainer();
      addTearDown(container.dispose);
      container.read(editorControllerProvider.notifier).openProject(project);
      final time = ValueNotifier(Duration.zero);
      addTearDown(time.dispose);
      final videos = VideoLayerManager();
      addTearDown(videos.dispose);
      final key = GlobalKey();
      tester.view.physicalSize = const Size(1280, 536);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: container,
          child: MaterialApp(
            debugShowCheckedModeBanner: false,
            home: Material(
              color: Colors.black,
              child: RepaintBoundary(
                key: key,
                child: FittedBox(
                  child: SizedBox(
                    width: project.outputWidth.toDouble(),
                    height: project.outputHeight.toDouble(),
                    child: CompositionView(
                      time: time,
                      videos: videos,
                      selectedId: null,
                      exporting: true,
                    ),
                  ),
                ),
              ),
            ),
          ),
        ),
      );
      final folder = Directory('build/render/abyss')
        ..createSync(recursive: true);
      File('${folder.path}/ABISMO-Aurea.json').writeAsStringSync(
        TemplatePack(
          name: project.name,
          project: project,
          author: 'Codex',
          notes:
              'Cena 3D nativa; quatro cameras, explorador com rig de 17 ossos, '
              'poses e trajetoria editaveis. Sem video ou assets externos.',
        ).encode(),
      );
      final frames =
          Platform.environment['AUREA_ABYSS_FRAMES']
              ?.split(',')
              .map(int.parse)
              .toList() ??
          List.generate(14 * abyssFps, (i) => i);
      await tester.runAsync(() async {
        for (final frame in frames) {
          time.value = abyssTime(frame / abyssFps);
          await tester.pump();
          await tester.pump();
          expect(tester.takeException(), isNull, reason: 'frame $frame');
          final image =
              await (key.currentContext!.findRenderObject()
                      as RenderRepaintBoundary)
                  .toImage();
          final bytes = await image.toByteData(format: ui.ImageByteFormat.png);
          image.dispose();
          File('${folder.path}/frame-${frame.toString().padLeft(3, '0')}.png')
              .writeAsBytesSync(bytes!.buffer.asUint8List());
          if (frame % 24 == 0 || frames.length < 20) {
            // ignore: avoid_print
            print('ABISMO frame $frame');
          }
        }
      });
    },
    timeout: const Timeout(Duration(minutes: 30)),
  );
}
