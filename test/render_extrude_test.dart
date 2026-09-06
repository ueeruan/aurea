import 'dart:io';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart' show RenderRepaintBoundary;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';

/// Bancada do EXTRUDE 3D: uma forma girada em Y com espessura, pintada
/// pelo motor de composicao. Salva o quadro em build/render/extrude.png
/// quando AUREA_RENDER=1; sempre confere que a fatia de tras aparece
/// (pixels escuros do lado da forma).
void main() {
  for (final comTexto in [false, true]) {
  testWidgets('extrude 3D pinta as fatias atras da ${comTexto ? 'texto' : 'forma'}', (tester) async {
    final Layer forma = comTexto ? TextLayer(
      name: 'texto',
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
      text: 'AUREA',
      fontSize: 140,
      color: const Color(0xFFFFFFFF),
      position: AnimatedOffset(const Offset(300, 300)),
      // A mesma configuracao vista no aparelho: Z, X e Y girados.
      rotation: AnimatedDouble(-57),
      rotationX: AnimatedDouble(21),
      rotationY: AnimatedDouble(45),
      is3D: true,
    ) : ShapeLayer(
      name: 'quadrado',
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
      contents: [
        ShapeParametric(
            kind: ParamShapeKind.rect,
            sizeX: AnimatedDouble(240),
            sizeY: AnimatedDouble(240)),
        ShapeFill(color: const Color(0xFFFFFFFF)),
      ],
      position: AnimatedOffset(const Offset(300, 300)),
      rotationY: AnimatedDouble(50),
      is3D: true,
    );
    var project = VideoProject(
      name: 'extrude',
      createdAt: DateTime(2026, 1, 1),
      aspectRatio: 1,
      resolutionHeight: 600,
      layers: [forma],
    );
    project = project.copyWith(meta: {
      forma.id: project.metaOf(forma.id).copyWith(extrude: 120),
    });

    final container = ProviderContainer();
    addTearDown(container.dispose);
    container.read(editorControllerProvider.notifier).openProject(project);
    final time = ValueNotifier<Duration>(const Duration(milliseconds: 500));
    final videos = VideoLayerManager();
    final key = GlobalKey();
    tester.view.physicalSize = const Size(600, 600);
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
                width: 600,
                height: 600,
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
    await tester.pump();
    await tester.pump();

    late ui.Image img;
    late ByteData data;
    await tester.runAsync(() async {
      final obj = key.currentContext!.findRenderObject() as RenderRepaintBoundary;
      img = await obj.toImage(pixelRatio: 1);
      data = (await img.toByteData(format: ui.ImageByteFormat.rawRgba))!;
      if (Platform.environment['AUREA_RENDER'] == '1') {
        final png = await img.toByteData(format: ui.ImageByteFormat.png);
        Directory('build/render').createSync(recursive: true);
        File('build/render/extrude${comTexto ? '_texto' : ''}.png').writeAsBytesSync(png!.buffer.asUint8List());
      }
    });

    // Conta pixels brancos (frente) e cinzas (fatias escurecidas).
    var brancos = 0, cinzas = 0;
    for (var i = 0; i < data.lengthInBytes; i += 4) {
      final r = data.getUint8(i), g = data.getUint8(i + 1), b = data.getUint8(i + 2);
      if (r > 240 && g > 240 && b > 240) brancos++;
      if (r > 60 && r < 200 && (r - g).abs() < 8 && (r - b).abs() < 8) cinzas++;
    }
    // ignore: avoid_print
    print('brancos=$brancos cinzas=$cinzas');
    expect(brancos, greaterThan(2000));
    expect(cinzas, greaterThan(500), reason: 'as fatias do extrude nao apareceram');
    img.dispose();
  });
  }
}
