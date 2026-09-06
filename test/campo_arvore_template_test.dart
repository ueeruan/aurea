import 'dart:convert';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/model_import_service.dart';
import 'package:aurea/src/features/editor/application/texture_cache.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/template_pack.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:aurea/src/features/projects/domain/campo_arvore_template.dart';
import 'package:aurea/src/features/projects/domain/colina_tv_template.dart'
    show colinaTriangles;

void main() {
  testWidgets(
    'Campo is a portable 25 second landscape with five native camera cuts',
    (tester) async {
      final sky =
          'data:image/png;base64,${base64Encode(File('assets/templates/campo-sky.png').readAsBytesSync())}';
      final scanned = await tester.runAsync(
        () => readModel3DFiles([
          'assets/models/monolito/arvore.obj',
          'assets/models/monolito/arvore.mtl',
          'assets/models/monolito/arvore.jpg',
        ]),
      );
      final project = buildCampoArvoreTemplate(
        skyTexture: sky,
        scannedTree: scanned,
      );
      final scene = project.layers.whereType<Scene3DLayer>().first;
      expect(project.duration, campoDuration);
      expect(scene.allCameras.length, 5);
      expect(scene.shots.map((s) => s.time.inSeconds), [0, 5, 10, 15, 20]);
      expect(
        scene.scene.nodes.where((n) => n.id.startsWith('campo_')).length,
        3,
      );
      expect(scene.scene.nodes.any((n) => n.id == 'colina_tv'), isFalse);
      expect(colinaTriangles(project), lessThan(70000));
      final pack = TemplatePack(
        name: project.name,
        project: project,
        author: 'Aurea',
        notes: 'Campo ao amanhecer. Cinco tomadas de 5 s; câmera, árvore, terreno e luzes editáveis.',
      );
      final encoded = jsonEncode(pack.toJson());
      final reopened = TemplatePack.decode(encoded)!.project;
      expect(
        reopened.layers.whereType<Scene3DLayer>().first.allCameras.length,
        5,
      );
      if (Platform.environment['AUREA_CAMPO_RENDER'] != '1') return;
      final folder = Directory('output/campo')..createSync(recursive: true);
      File('${folder.path}/CAMPO.aurea').writeAsStringSync(encoded);
      File('${folder.path}/campo-project.json')
          .writeAsStringSync(jsonEncode(projectToJson(project)));
      await tester.runAsync(() async {
        for (final layer in project.layers.whereType<Scene3DLayer>()) {
          for (final node in layer.scene.nodes) {
            for (final material
                in node.modelAsset?.data['materials'] as List? ?? []) {
              final texture = material['image'] as String?;
              if (texture != null) await TextureCache.instance.prepare(texture);
            }
          }
        }
      });
      final container = ProviderContainer();
      addTearDown(container.dispose);
      container.read(editorControllerProvider.notifier).openProject(project);
      final clock = ValueNotifier(Duration.zero);
      addTearDown(clock.dispose);
      final videos = VideoLayerManager();
      addTearDown(videos.dispose);
      final key = GlobalKey();
      tester.view.physicalSize = const Size(1280, 720);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: container,
          child: MaterialApp(
            home: Scaffold(
              backgroundColor: Colors.black,
              body: RepaintBoundary(
                key: key,
                child: SizedBox.expand(
                  child: CompositionView(
                    time: clock,
                    videos: videos,
                    selectedId: null,
                    exporting: true,
                  ),
                ),
              ),
            ),
          ),
        ),
      );
      final step =
          int.tryParse(Platform.environment['AUREA_CAMPO_STEP'] ?? '') ?? 150;
      final frames = Directory('${folder.path}/frames-step-$step')
        ..createSync(recursive: true);
      for (var i = 0; i < 750; i += step) {
        clock.value = Duration(microseconds: (i * 1e6 / 30).round());
        await tester.pump();
        await tester.pump();
        expect(tester.takeException(), isNull);
        await tester.runAsync(() async {
          final boundary =
              key.currentContext!.findRenderObject() as RenderRepaintBoundary;
          final image = await boundary.toImage();
          final bytes = await image.toByteData(format: ui.ImageByteFormat.png);
          image.dispose();
          File('${frames.path}/f${(i ~/ step).toString().padLeft(4, '0')}.png')
              .writeAsBytesSync(bytes!.buffer.asUint8List());
        });
      }
      // ignore: avoid_print
      print(
        'CAMPO: ${colinaTriangles(project)} triangles; ${encoded.length} JSON characters; ${frames.path}',
      );
    },
    timeout: const Timeout(Duration(minutes: 20)),
  );
}
