import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/template_pack.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:aurea/src/features/projects/domain/dnyx_remix_template.dart';

void main() {
  testWidgets('render Dnyx pelo compositor real do app', (tester) async {
    if (Platform.environment['AUREA_DNYX_RENDER'] != '1') return;
    final font = FontLoader('Aurea Motion Sans')
      ..addFont(rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'));
    await font.load();
    final paths = {
      for (final n in dnyxAssetNames)
        n: File('assets/templates/dnyx/$n').absolute.path,
    };
    final p = buildDnyxRemixTemplate(paths);
    final container = ProviderContainer();
    addTearDown(container.dispose);
    container.read(editorControllerProvider.notifier).openProject(p);
    final time = ValueNotifier(Duration.zero);
    addTearDown(time.dispose);
    final videos = VideoLayerManager();
    final key = GlobalKey();
    tester.view.physicalSize = const Size(576, 576);
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
    await tester.runAsync(() async {
      for (final n in dnyxAssetNames.where((n) => n.endsWith('.png'))) {
        await precacheImage(FileImage(File(paths[n]!)), key.currentContext!);
      }
    });
    final pass = Platform.environment['AUREA_DNYX_PASS'] ?? '01';
    if (!RegExp(r'^[a-zA-Z0-9_-]+$').hasMatch(pass)) {
      throw ArgumentError('pass');
    }
    final folder = Directory('build/render/dnyx/$pass')
      ..createSync(recursive: true);
    File('${folder.path}/Aurea-App-RMK-Dnyx.json').writeAsStringSync(
      TemplatePack(
        name: p.name,
        project: p,
        author: 'Codex',
        notes: 'Recriacao nativa editavel. Fotos isoladas da referencia; assinatura alterada. Midias em caminhos locais.',
      ).encode(),
    );
    final frames =
        Platform.environment['AUREA_DNYX_FRAMES']
            ?.split(',')
            .map(int.parse)
            .toList() ??
        List.generate(309, (i) => i);
    await tester.runAsync(() async {
      for (final q in frames) {
        time.value = dnyxFrame(q);
        await tester.pump();
        await tester.pump();
        expect(tester.takeException(), isNull, reason: 'frame $q');
        final image =
            await (key.currentContext!.findRenderObject()
                    as RenderRepaintBoundary)
                .toImage();
        final data = await image.toByteData(format: ui.ImageByteFormat.png);
        image.dispose();
        File('${folder.path}/frame-${q.toString().padLeft(3, '0')}.png')
            .writeAsBytesSync(data!.buffer.asUint8List());
        // ignore: avoid_print
        print('frame $q');
      }
    });
  }, timeout: const Timeout(Duration(minutes: 30)));
}
