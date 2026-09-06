import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/font_service.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:aurea/src/features/projects/domain/pindown_motion_template.dart';

/// RENDER DE MESA: os quadros do modelo, pintados pelo MESMO motor de
/// composicao do app, salvos em PNG. Nao e teste de verdade (nao
/// afirma nada); e a bancada para comparar a recriacao com a
/// referencia sem passar pelo aparelho. So roda com AUREA_RENDER=1.
///
///   AUREA_RENDER=1 flutter test test/render_pindown_test.dart
///
/// Os quadros saem em build/render/pindown/fNNNN.png; o ffmpeg monta o
/// video.
void main() {
  testWidgets('renderiza a recriacao Pindown em quadros', (tester) async {
    if (Platform.environment['AUREA_RENDER'] != '1') return;

    // Fonte de verdade: sem ela o texto sai como blocos (Ahem).
    final base = Platform.environment['FLUTTER_ROOT'] ??
        r'C:\Users\SnyX\.aurea\flutter';
    final pasta = '$base/bin/cache/artifacts/material_fonts';
    final loader = FontLoader('Roboto');
    var carregadas = 0;
    for (final nome in ['roboto-regular.ttf', 'roboto-bold.ttf']) {
      final f = File('$pasta/$nome');
      if (f.existsSync()) {
        final bytes = f.readAsBytesSync();
        loader.addFont(Future.value(ByteData.sublistView(bytes)));
        carregadas++;
      }
    }
    await loader.load();
    // O texto so usa a familia se o servico de fontes a conhece.
    FontService.instance.registrarSemArquivo('Roboto');
    // ignore: avoid_print
    print('fontes carregadas: $carregadas de $pasta');

    final project = buildPindownMotionTemplate();
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
        // Material por cima de tudo: sem ele, todo texto do Flutter sai
        // com o sublinhado amarelo de "texto fora de Material".
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

    final outDir = Directory('build/render/pindown');
    if (outDir.existsSync()) outDir.deleteSync(recursive: true);
    outDir.createSync(recursive: true);

    final fps = project.fps;
    final total = (project.duration.inMicroseconds * fps / 1e6).round();
    final passo = int.tryParse(Platform.environment['AUREA_RENDER_STEP'] ?? '1') ?? 1;

    await tester.runAsync(() async {
      for (var i = 0; i < total; i += passo) {
        time.value = Duration(microseconds: (i * 1000000 / fps).round());
        await tester.pump();
        await tester.pump();
        final obj = key.currentContext!.findRenderObject() as RenderRepaintBoundary;
        final img = await obj.toImage(pixelRatio: 1);
        final data = await img.toByteData(format: ui.ImageByteFormat.png);
        img.dispose();
        File('${outDir.path}/f${i.toString().padLeft(4, '0')}.png')
            .writeAsBytesSync(data!.buffer.asUint8List());
      }
    });
    // ignore: avoid_print
    print('quadros: $total em ${outDir.path}');
  }, timeout: const Timeout(Duration(minutes: 30)));
}
