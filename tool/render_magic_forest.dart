// Native Android artifact entry point. Uses Aurea's current Scene3DGpu and
// PlatformEncoder unchanged; restores the ordinary beta APK after rendering.
import 'dart:convert';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:path_provider/path_provider.dart';
import 'package:aurea/src/features/editor/application/model_import_service.dart';
import 'package:aurea/src/features/editor/application/scene3d_gpu.dart';
import 'package:aurea/src/features/editor/application/qualidade3d_controller.dart';
import 'package:aurea/src/features/editor/application/preview_stats.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/template_pack.dart';
import 'package:aurea/src/features/editor/domain/orcamento_render.dart';
import 'package:aurea/src/features/export/application/platform_encoder.dart';
import 'package:aurea/src/features/projects/application/project_repository.dart';

import 'magic_forest_project.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const MaterialApp(home: ForestRender()));
}

class ForestRender extends StatefulWidget {
  const ForestRender({super.key});
  @override
  State<ForestRender> createState() => _ForestRenderState();
}

class _ForestRenderState extends State<ForestRender> {
  String status = 'Preparando LÚMEN';
  ui.Image? preview;
  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => render());
  }

  void report(String s) {
    debugPrint('FOREST $s');
    if (mounted) setState(() => status = s);
  }

  Future<void> render() async {
    final docs = await getApplicationDocumentsDirectory();
    final folder = Directory('${docs.path}/forest-render');
    await folder.create(recursive: true);
    final statusFile = File('${folder.path}/status.json');
    Future<void> log(Map<String, Object> data) =>
        statusFile.writeAsString(jsonEncode(data));
    try {
      final meta = jsonDecode(
        await File('${folder.path}/geometry.json').readAsString(),
      ) as Map;
      final configFile = File('${folder.path}/config.json');
      final config = configFile.existsSync()
          ? jsonDecode(await configFile.readAsString()) as Map
          : {};
      final full = config['full'] == true;
      await Scene3DGpu.preparar();
      if (!Scene3DGpu.pronto) {
        throw StateError('GPU unavailable: ${Scene3DGpu.motivo}');
      }
      await ControladorDeQualidade3D.instancia.sondar();
      report('Lendo árvores e vegetação');
      final model = await readModel3DFiles(['${folder.path}/FLORESTA.glb']);
      final project = magicForestProject(model, meta);
      final pack = jsonEncode(
        TemplatePack(
          name: project.name,
          author: 'Aurea',
          project: project,
          notes: '15 s, 5 tomadas, câmeras e vagalumes animados. Geometria original. Texturas CC0 Poly Haven bark_brown_02 e forest_leaves_02.',
        ).toJson(),
      );
      if (project.duration.inSeconds != 15 ||
          (project.layers.single as Scene3DLayer).allCameras.length != 5) {
        throw StateError('Invalid portable project');
      }
      await File('${folder.path}/LUMEN-Floresta-Magica.aurea')
          .writeAsString(pack);
      report('Preparando GPU • ${model.triangleCount} triângulos');
      await log({'state': 'preparing_gpu', 'triangles': model.triangleCount});
      final gpu = Scene3DGpu();
      final layer = project.layers.single as Scene3DLayer;
      const size = ui.Size(720, 1280);
      Future<ui.Image> capture(int frame) async {
        final t = ft(frame / 30);
        final camera = layer.allCameras[frame ~/ 90];
        gpu.sincronizar(layer.scene, t, receita: ReceitaDeQualidade.alta);
        gpu.configurarProfundidadeDeCampo(camera, t);
        final recorder = ui.PictureRecorder();
        final canvas = ui.Canvas(recorder);
        canvas.drawColor(layer.scene.background!, ui.BlendMode.src);
        gpu.desenhar(
          canvas,
          ui.Offset.zero & size,
          gpu.camera(layer.cameraAt(t), size),
          exporting: true,
        );
        final pic = recorder.endRecording();
        final img = await pic.toImage(720, 1280);
        pic.dispose();
        return img;
      }

      // Real GPU warm-up allows asynchronous texture/environment uploads to finish.
      for (var i = 0; i < 8; i++) {
        final img = await capture(0);
        img.dispose();
        await Future<void>.delayed(const Duration(milliseconds: 600));
      }
      final frames = full
          ? [for (var i = 0; i < 450; i++) i]
          : [
              0,
              45,
              89,
              90,
              135,
              179,
              180,
              225,
              269,
              270,
              315,
              359,
              360,
              405,
              449,
            ];
      if (full) {
        await PlatformEncoder.start(
          path: '${folder.path}/LUMEN-Floresta-Magica.mp4',
          width: 720,
          height: 1280,
          fps: 30,
          bitrate: 12000000,
        );
      }
      for (final f in frames) {
        final img = await capture(f);
        if (full) {
          final rgba = await img.toByteData(format: ui.ImageByteFormat.rawRgba);
          await PlatformEncoder.frameRgba(
            rgba!.buffer.asUint8List(),
            720,
            1280,
          );
        }
        if (!full || f % 90 == 45 || f == 0 || f == 449) {
          final png = await img.toByteData(format: ui.ImageByteFormat.png);
          await File('${folder.path}/frame-${f.toString().padLeft(3, '0')}.png')
              .writeAsBytes(png!.buffer.asUint8List());
        }
        if (f % 15 == 0 || !full || f == 449) {
          report('${forestShots[f ~/ 90]} • ${f + 1}/450');
          await log({'state': 'rendering', 'frame': f, 'full': full});
          final old = preview;
          setState(() => preview = img.clone());
          old?.dispose();
          await Future<void>.delayed(const Duration(milliseconds: 10));
        }
        img.dispose();
      }
      if (full && !await PlatformEncoder.finish()) {
        throw StateError('Encoder did not finish');
      }
      gpu.descartar();
      if (full) {
        await ProjectRepository(installBundledExamples: false).save(project);
      }
      await log({
        'state': 'done',
        'full': full,
        'frames': frames.length,
        'durationSeconds': 15,
        'fps': 30,
        'width': 720,
        'height': 1280,
        'triangles': model.triangleCount,
        'cameras': 5,
        'cutsSeconds': [0, 3, 6, 9, 12],
        'renderer': 'Aurea Scene3DGpu / Flutter Scene / Android Impeller',
        'quality': ControladorDeQualidade3D.instancia.nivel.value.name,
        'renderWidth': PreviewStats.cena3d.value?.larguraPx ?? 0,
        'renderHeight': PreviewStats.cena3d.value?.alturaPx ?? 0,
        'warnings': model.warnings,
      });
      report(
        full
            ? 'Vídeo exportado • 15 segundos'
            : 'Cinco tomadas prontas para revisão',
      );
    } catch (e, st) {
      await PlatformEncoder.cancel();
      await log({'state': 'error', 'error': '$e', 'stack': '$st'});
      report('ERRO: $e');
    }
  }

  @override
  Widget build(BuildContext context) => Scaffold(
    backgroundColor: const Color(0xff071411),
    body: SafeArea(
      child: Column(
        children: [
          Padding(
            padding: const EdgeInsets.all(20),
            child: Text(
              status,
              style: const TextStyle(color: Colors.white, fontSize: 18),
            ),
          ),
          if (preview != null)
            Expanded(
              child: RawImage(image: preview, fit: BoxFit.contain),
            ),
        ],
      ),
    ),
  );
}
