// EDITAR O TEXTO 3D NAO PODE FECHAR O APLICATIVO.
//
// O relato: criar o texto funciona; EDITAR derruba o processo. Editar troca
// o modelo inteiro do no (outra geometria, outro numero de pecas) com a cena
// ja na placa — e o caminho em que alca reciclada, conjunto de recursos
// antigo e buffer solto se encontram. Aqui ele roda varias vezes seguidas.
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/motor3d_nativo.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/modelo_do_texto3d.dart';
import 'package:aurea/src/features/editor/domain/texto3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart'
    show estado3DDoQuadro;
import 'package:aurea_render/aurea_render.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  test('criar, editar varias vezes e desenhar a cada edicao', () async {
    Motor3D.preparar();
    expect(Motor3D.pronto, isTrue, reason: Motor3D.motivo);
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final c = container.read(editorControllerProvider.notifier);
    c.openProject(VideoProject.empty('editar texto 3d'));
    final noId = await c.addTexto3D(Duration.zero, 'AUREA', EstiloDoTexto3D.ouro);
    expect(noId, isNotNull);

    Future<int> desenhar(String rotulo) async {
      final projeto = container.read(editorControllerProvider);
      final camada = projeto.layers.whereType<Scene3DLayer>().single;
      // O PALCO DESENHA EM DOIS TAMANHOS (previa e cheio); os dois aqui.
      var pintados = 0;
      for (final lado in const [256, 512]) {
        final estado = estado3DDoQuadro(
          project: projeto, l: camada, local: Duration.zero,
          global: Duration.zero, largura: lado, altura: lado,
        );
        final motor = Motor3DNativo.instance;
        motor.montar(
          cena: estado.cena, camera: estado.camera, local: Duration.zero,
          largura: lado, altura: lado, aspectoDaComposicao: 1,
          sombra: lado == 512 ? 1 : 0, amostras: lado == 512 ? 4 : 1,
        );
        final imagem = await motor.quadroEsperando(
          '$rotulo-$lado-${DateTime.now().microsecondsSinceEpoch}');
        expect(imagem, isNotNull, reason: Motor3D.ultimoErro);
        final d = await imagem!.toByteData(format: ui.ImageByteFormat.rawRgba);
        final b = d!.buffer.asUint8List();
        for (var i = 3; i < b.length; i += 4) {
          if (b[i] > 8) pintados++;
        }
      }
      // ignore: avoid_print
      print('EDITAR[$rotulo] pintados=$pintados erro="${Motor3D.ultimoErro}"');
      return pintados;
    }

    expect(await desenhar('criado'), greaterThan(0));
    final camadaId = container.read(editorControllerProvider)
        .layers.whereType<Scene3DLayer>().single.id;
    const textos = ['A', 'AUREA MOTION', 'Oi', 'TEXTO BEM MAIS LONGO 123', 'X'];
    for (var i = 0; i < textos.length; i++) {
      final estilo = EstiloDoTexto3D.values[i % EstiloDoTexto3D.values.length];
      final ok = await c.editarTexto3D(
        camadaId, noId!, Texto3D(texto: textos[i]), estilo);
      expect(ok, isTrue, reason: 'editar "${textos[i]}" falhou');
      expect(await desenhar('edicao$i'), greaterThan(0));
    }
  });
}
