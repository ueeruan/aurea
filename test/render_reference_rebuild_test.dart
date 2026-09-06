import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:aurea/src/features/editor/domain/template_pack.dart';
import 'package:aurea/src/features/projects/domain/reference_rebuild_template.dart';

void main() {
  testWidgets('render independente pelo motor real do AUREA', (tester) async {
    if (Platform.environment['AUREA_REBUILD_RENDER'] != '1') return;
    final p = buildReferenceRebuildTemplate();
    final container = ProviderContainer();
    addTearDown(container.dispose);
    container.read(editorControllerProvider.notifier).openProject(p);
    final time = ValueNotifier(Duration.zero);
    addTearDown(time.dispose);
    final videos = VideoLayerManager();
    final key = GlobalKey();
    tester.view.physicalSize = const Size(720, 1278);
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
    final pass = Platform.environment['AUREA_REBUILD_PASS'] ?? '01';
    if (!RegExp(r'^[a-zA-Z0-9_-]+$').hasMatch(pass)) {
      throw ArgumentError('invalid pass');
    }
    final out = Directory('build/render/reference-rebuild/$pass');
    out.createSync(recursive: true);
    final pack = TemplatePack(
      name: p.name,
      project: p,
      author: 'Codex',
      notes:
          'Nova reconstrucao vetorial editavel. Ainda nao e pixel-identica. '
          'A trilha esta empacotada no modelo do app, nao neste JSON.',
    );
    final encoded = pack.encode();
    expect(
      TemplatePack.decode(encoded)!.project.layers.length,
      p.layers.length,
    );
    File('${out.path}/AUREA-nova-recriacao.json').writeAsStringSync(encoded);
    final selected = Platform.environment['AUREA_REBUILD_FRAMES'];
    final frames = selected == null
        ? List.generate(280, (i) => i)
        : selected.split(',').map(int.parse).toList();
    await tester.runAsync(() async {
      for (final q in frames) {
        time.value = referenceFrame(q);
        await tester.pump();
        await tester.pump();
        expect(tester.takeException(), isNull, reason: 'frame $q');
        final image =
            await (key.currentContext!.findRenderObject()
                    as RenderRepaintBoundary)
                .toImage();
        final data = await image.toByteData(format: ui.ImageByteFormat.png);
        image.dispose();
        File('${out.path}/frame-${q.toString().padLeft(3, '0')}.png')
            .writeAsBytesSync(data!.buffer.asUint8List());
        // ignore: avoid_print
        print('frame $q');
      }
    });
  }, timeout: const Timeout(Duration(minutes: 30)));
}
