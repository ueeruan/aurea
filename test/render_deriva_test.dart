import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/texture_cache.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:aurea/src/features/projects/application/modelos_empacotados.dart';
import 'package:aurea/src/features/projects/domain/deriva_template.dart';

/// RENDER DE MESA da DERIVA: os quadros do modelo, pintados pelo mesmo
/// motor de composicao do app, salvos em PNG — a bancada para comparar
/// com a referencia sem passar pelo aparelho. So roda com
/// AUREA_RENDER=1; AUREA_RENDER_STEP pula quadros.
///
///   AUREA_RENDER=1 AUREA_RENDER_STEP=15 flutter test test/render_colina_test.dart
void main() {
  testWidgets('renderiza a Deriva em quadros', (tester) async {
    if (Platform.environment['AUREA_RENDER'] != '1') return;

    final astronauta = await tester.runAsync(
      () => carregarAstronautaDe('assets/models/monolito'),
    );
    final project = buildDerivaTemplate(astronauta: astronauta);
    // As texturas sao data URIs: precisam estar decodificadas antes do
    // primeiro quadro, senao as faces saem lisas.
    await tester.runAsync(() async {
      for (final layer in project.layers) {
        if (layer is! Scene3DLayer) continue;
        for (final n in layer.scene.nodes) {
          for (final m in n.modelAsset?.data['materials'] as List? ?? []) {
            final img = m['image'] as String?;
            if (img != null) await TextureCache.instance.prepare(img);
          }
        }
      }
    });

    final container = ProviderContainer();
    addTearDown(container.dispose);
    container.read(editorControllerProvider.notifier).openProject(project);

    final time = ValueNotifier<Duration>(Duration.zero);
    final videos = VideoLayerManager();
    final key = GlobalKey();
    final w = project.outputWidth.toDouble();
    final h = project.outputHeight.toDouble();

    tester.view.physicalSize = Size(w, h);
    tester.view.devicePixelRatio = 1.0;
    addTearDown(tester.view.reset);

    await tester.pumpWidget(UncontrolledProviderScope(
      container: container,
      child: MaterialApp(
        debugShowCheckedModeBanner: false,
        home: Material(
          color: Colors.black,
          child: Center(
            child: RepaintBoundary(
              key: key,
              child: SizedBox(
                width: w,
                height: h,
                child: ColoredBox(
                  color: Colors.black,
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
    ));

    final outDir = Directory('build/render/deriva');
    if (outDir.existsSync()) outDir.deleteSync(recursive: true);
    outDir.createSync(recursive: true);

    final fps = project.fps;
    final total = (project.duration.inMicroseconds * fps / 1e6).round();
    final passo =
        int.tryParse(Platform.environment['AUREA_RENDER_STEP'] ?? '1') ?? 1;

    await tester.runAsync(() async {
      for (var i = 0; i < total; i += passo) {
        time.value = Duration(microseconds: (i * 1000000 / fps).round());
        await tester.pump();
        await tester.pump();
        final obj =
            key.currentContext!.findRenderObject() as RenderRepaintBoundary;
        final img = await obj.toImage(pixelRatio: 1);
        final data = await img.toByteData(format: ui.ImageByteFormat.png);
        img.dispose();
        File('${outDir.path}/f${i.toString().padLeft(4, '0')}.png')
            .writeAsBytesSync(data!.buffer.asUint8List());
      }
    });
    // ignore: avoid_print
    print('quadros: $total em ${outDir.path} (passo $passo)');
  }, timeout: const Timeout(Duration(minutes: 30)));
}
