// A CAMERA ENQUADRA O QUE ACABOU DE ENTRAR.
//
// O RELATO ERA "importei e nao apareceu nada". Uma das causas era o motor nao
// desenhar (a releitura do quadro voltava vazia, e o enrolamento das faces);
// a outra era a camera: a autoral padrao fica a 800 unidades do alvo, e um
// modelo normalizado desenhado em 120 ocupa um quinto do quadro, no meio de
// um vazio preto. Nao aparecer e importar mal passam a ser a mesma tela.
//
// O QUE ESTE TESTE PRENDE: a conta do enquadramento e a distancia da camada
// que o aplicativo cria ao importar. Se alguem voltar a 800, cai aqui.
import 'dart:math' as math;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/camera3d.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/modelo_do_texto3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('a distancia de enquadramento cresce com o raio e encolhe com a lente', () {
    final perto = distanciaParaEnquadrar(
      raio: 100,
      fovGraus: 40,
      aspecto: 1,
    );
    final longe = distanciaParaEnquadrar(raio: 200, fovGraus: 40, aspecto: 1);
    expect(longe, closeTo(perto * 2, 1e-6));

    final angular = distanciaParaEnquadrar(
      raio: 100,
      fovGraus: 80,
      aspecto: 1,
    );
    expect(angular, lessThan(perto), reason: 'lente mais aberta aproxima');
  });

  test('o MENOR meio-angulo manda, e ele depende do formato', () {
    // O fov que chega e o HORIZONTAL. Num filme largo (aspecto maior que 1)
    // o vertical e o meio-angulo apertado, e enquadrar pelo horizontal
    // cortaria o objeto em cima e embaixo — entao a camera precisa ir mais
    // LONGE. Num retrato o apertado e o proprio horizontal, e a distancia e
    // a mesma do quadro quadrado.
    final quadrado = distanciaParaEnquadrar(raio: 100, fovGraus: 40, aspecto: 1);
    final retrato = distanciaParaEnquadrar(
      raio: 100,
      fovGraus: 40,
      aspecto: 9 / 16,
    );
    final paisagem = distanciaParaEnquadrar(
      raio: 100,
      fovGraus: 40,
      aspecto: 16 / 9,
    );
    expect(retrato, closeTo(quadrado, 1e-9));
    expect(paisagem, greaterThan(quadrado));
    // E a conta do filme largo e exatamente a do fov vertical.
    final tanH = math.tan(40 * math.pi / 180 / 2);
    expect(paisagem, closeTo(1.25 * 100 / (tanH / (16 / 9)), 1e-6));
  });

  test('a camera conta o quadro: o objeto cabe, com folga', () {
    const raio = 120.0;
    const fov = 40.0;
    const aspecto = 9 / 16.0;
    final d = distanciaParaEnquadrar(
      raio: raio,
      fovGraus: fov,
      aspecto: aspecto,
    );
    // O meio-angulo apertado e o horizontal: tan(fov/2) * d tem de cobrir o
    // raio — e o teste usa a MESMA conta da funcao, de proposito: o que se
    // cobra aqui e a folga, nao a trigonometria.
    final tanApertado = math.min(
      math.tan(fov * math.pi / 180 / 2),
      math.tan(fov * math.pi / 180 / 2) / aspecto,
    );
    expect(tanApertado * d, greaterThan(raio));
  });

  test('o texto 3D nasce com a camera que o enquadra, e nao a 800', () async {
    final container = ProviderContainer();
    try {
      final controller = container.read(editorControllerProvider.notifier);
      controller.openProject(VideoProject.empty('texto 3d'));
      final nodeId = await controller.addTexto3D(
        Duration.zero,
        'AUREA',
        EstiloDoTexto3D.ouro,
        familia: 'fonte que nao existe',
      );
      expect(nodeId, isNotNull);
      final project = container.read(editorControllerProvider);
      final cena = project.layers.whereType<Scene3DLayer>().single;
      final distancia = cena.camera.posZ.valueAt(Duration.zero);
      // A PADRAO FICA A 800: e o numero que fazia o texto sumir no vazio.
      expect(distancia, lessThan(800));
      expect(distancia, greaterThan(0));
      // E A DISTANCIA ENQUADRA MESMO o objeto: o raio do texto vezes 120
      // (o tamanho com que a camada o desenha) tem de caber no meio-angulo.
      final fonte = cena.camera.renderAt(Duration.zero);
      final tanMetade = math.tan(fonte.fovRadians / 2);
      final meioAngulo = math.min(tanMetade, tanMetade / project.aspectRatio);
      final alcanceVisivel = meioAngulo * distancia;
      // O texto e largo e baixo; a meia-largura normalizada e 1, e o no o
      // desenha em 120.
      expect(
        alcanceVisivel,
        greaterThan(60),
        reason: 'o texto nao caberia: alcance $alcanceVisivel',
      );
    } finally {
      container.dispose();
    }
  });
}
