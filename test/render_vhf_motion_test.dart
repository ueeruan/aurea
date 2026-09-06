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
import 'package:aurea/src/features/projects/domain/vhf_motion_template.dart';

void main() {
  testWidgets('render VHF through the native app compositor', (tester) async {
    if (Platform.environment['AUREA_VHF_RENDER'] != '1') return;
    final project = buildVhfMotionTemplate(
      audioPath: File('assets/templates/vhf/audio.m4a').absolute.path,
    );
    final container = ProviderContainer();
    addTearDown(container.dispose);
    container.read(editorControllerProvider.notifier).openProject(project);
    final time = ValueNotifier(Duration.zero);
    addTearDown(time.dispose);
    final videos = VideoLayerManager();
    addTearDown(videos.dispose);
    final key = GlobalKey();
    tester.view.physicalSize = const Size(720, 1280);
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
    );
    final pass = Platform.environment['AUREA_VHF_PASS'] ?? '01';
    if (!RegExp(r'^[a-zA-Z0-9_-]+$').hasMatch(pass)) {
      throw ArgumentError('pass');
    }
    final folder = Directory('build/render/vhf/$pass')
      ..createSync(recursive: true);
    File('${folder.path}/VHF-Neon-Orbit-Aurea.json').writeAsStringSync(
      TemplatePack(
        name: project.name,
        project: project,
        author: 'Codex',
        notes:
            'Recriacao vetorial editavel; nao usa quadros do MP4 como imagem.',
      ).encode(),
    );
    final frames =
        Platform.environment['AUREA_VHF_FRAMES']
            ?.split(',')
            .map(int.parse)
            .toList() ??
        List.generate(234, (i) => i);
    await tester.runAsync(() async {
      for (final q in frames) {
        time.value = vhfFrame(q);
        await tester.pump();
        await tester.pump();
        expect(tester.takeException(), isNull, reason: 'frame $q');
        final image =
            await (key.currentContext!.findRenderObject()
                    as RenderRepaintBoundary)
                .toImage();
        final bytes = await image.toByteData(format: ui.ImageByteFormat.png);
        image.dispose();
        File('${folder.path}/frame-${q.toString().padLeft(3, '0')}.png')
            .writeAsBytesSync(bytes!.buffer.asUint8List());
        // ignore: avoid_print
        print('frame $q');
      }
    });
  }, timeout: const Timeout(Duration(minutes: 30)));
}
