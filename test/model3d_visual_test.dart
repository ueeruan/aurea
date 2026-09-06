import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/application/model_import_service.dart';
import 'package:aurea/src/features/editor/application/texture_cache.dart';
import 'package:aurea/src/features/editor/domain/model_asset3d.dart';
import 'package:aurea/src/features/editor/domain/model_import3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/camera3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/presentation/widgets/scene3d_painter.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  test(
    'personagem Khronos: todos os clipes deformam e renderizam com textura',
    () async {
      final file = File('build/model3d-fixtures/Fox.glb');
      if (!file.existsSync()) return;
      final model = importGltf3D(file.readAsBytesSync());
      expect(model.clips.length, 3);
      expect(model.joints.length, greaterThan(10));
      for (var i = 0; i < model.clips.length; i++) {
        final motion = ModelMotion3D(clip: i, loop: false);
        final a = model.evaluate(Duration.zero, motion),
            b = model.evaluate(const Duration(milliseconds: 333), motion);
        expect(a.mesh.verts, isNot(b.mesh.verts));
        expect(b.mesh.verts.expand((v) => v).every((v) => v.isFinite), isTrue);
      }
      for (final material in model.data['materials'] as List) {
        if (material['image'] != null) {
          expect(
            await TextureCache.instance.prepare(material['image'] as String),
            isTrue,
          );
        }
      }
      if (Platform.environment['AUREA_3D_VISUAL'] != '1') return;
      final dir = Directory('build/model3d-fixtures/render')
        ..createSync(recursive: true);
      final node = SceneNode(
        id: 'fox',
        modelAsset: model,
        size: 110,
        modelMotion: const ModelMotion3D(clip: 2, loop: false),
      );
      final scene = Scene3D(
        nodes: [node],
        ambient: .5,
        lights: [Light3D(intensity: AnimatedDouble(2))],
        background: const Color(0xFF171B26),
      );
      for (final ms in [0, 333, 666]) {
        final recorder = ui.PictureRecorder(), canvas = Canvas(recorder);
        Scene3DPainter(
          scene: scene,
          camera: Camera3D(),
          view: SceneView.camera,
          time: Duration(milliseconds: ms),
          overrideCamera: const RenderCamera(
            position: Vec3(270, 100, 350),
            target: Vec3.zero,
          ),
          showModelRig: true,
          selectedNodeId: 'fox',
        ).paint(canvas, const Size(640, 480));
        final picture = recorder.endRecording(),
            image = await picture.toImage(640, 480);
        final bytes = await image.toByteData(format: ui.ImageByteFormat.png);
        File('${dir.path}/fox-$ms.png')
            .writeAsBytesSync(bytes!.buffer.asUint8List());
        image.dispose();
        picture.dispose();
      }
    },
  );
  test(
    'servico importa em isolate sem depender da sessao de Downloads',
    () async {
      final file = File('build/model3d-fixtures/Fox.glb');
      if (!file.existsSync()) return;
      final model = await readModel3DFiles([file.absolute.path]);
      expect(model.clips.length, 3);
      expect(model.data['materials'][0]['image'], startsWith('data:'));
    },
  );
}
