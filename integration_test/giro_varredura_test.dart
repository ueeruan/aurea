// VARREDURA DO GIRO: o texto 3D girado pela CAMADA, quadro a quadro, salvo em
// PNG para olhar. Usa o caminho SINCRONO do palco (`quadro`), que e onde o
// relato acontece.
import 'dart:io';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/motor3d_nativo.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/modelo_do_texto3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart'
    show estado3DDoQuadro;
import 'package:aurea_render/aurea_render.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();
  test('varredura do giro da camada', () async {
    Motor3D.preparar();
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final c = container.read(editorControllerProvider.notifier);
    c.openProject(VideoProject.empty('varredura'));
    await c.addTexto3D(Duration.zero, 'REGIS', EstiloDoTexto3D.cromo);
    final projeto = container.read(editorControllerProvider);
    final camada = projeto.layers.whereType<Scene3DLayer>().single;
    final motor = Motor3DNativo.instance;
    final pasta = Directory('/sdcard/Download/giro')..createSync(recursive: true);
    var i = 0;
    for (var y = -180; y <= 180; y += 20) {
      final l = camada.copyLayer(
        rotationX: AnimatedDouble(42.3), rotationY: AnimatedDouble(y.toDouble()));
      final e = estado3DDoQuadro(project: projeto, l: l, local: Duration.zero,
          global: Duration.zero, largura: 360, altura: 360);
      motor.montar(cena: e.cena, camera: e.camera, local: Duration.zero,
          largura: 360, altura: 360, aspectoDaComposicao: 1, sombra: 0, amostras: 1);
      final img = await motor.quadroEsperando(e.chave);
      final n = motor.numeros;
      // ignore: avoid_print
      print('GIRO y=$y desenhadas=${n.desenhadas} fora=${n.foraDoCampo} tri=${n.triangulos} '
          'cam=(${e.camera?.position.x.toStringAsFixed(0)},${e.camera?.position.y.toStringAsFixed(0)},${e.camera?.position.z.toStringAsFixed(0)}) '
          'up=(${e.camera?.up.x.toStringAsFixed(2)},${e.camera?.up.y.toStringAsFixed(2)},${e.camera?.up.z.toStringAsFixed(2)})');
      final png = await img!.toByteData(format: ui.ImageByteFormat.png);
      File('${pasta.path}/g${(i++).toString().padLeft(2, '0')}.png')
          .writeAsBytesSync(png!.buffer.asUint8List());
    }
  });
}
