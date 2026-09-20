// A PONTE ENTRE A TIMELINE E O MOTOR 3D NATIVO — a traducao, provada no PC.
//
// O motor 3D nao e compilado para o PC (o Diligent deste repositorio e
// Vulkan/Metal), entao aqui nao se desenha pixel nenhum. O que se prova e
// o que o motor RECEBE: as camadas, os transforms, a camera, as luzes e a
// tinta. Era exatamente onde estavam os defeitos de campo — a camera com
// fov horizontal passada como vertical deformava tudo que nao fosse
// quadrado, e a tinta da camada pintava todas as faces com a cor da
// primeira.
//
// A CHAVE tambem e provada aqui: e ela que faz o preview e a exportacao
// desenharem o MESMO quadro (§33), porque os dois a montam pela mesma
// funcao.

import 'dart:math' as math;
import 'dart:ui' show Color, Offset;

import 'package:aurea/src/features/editor/application/motor3d_nativo.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/orcamento_render.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart'
    show estado3DDoQuadro, nivelDeSombra3D;
import 'package:flutter_test/flutter_test.dart';

void main() {
  final motor = Motor3DNativo.instance;

  VideoProject projetoCom(Scene3DLayer cena) => VideoProject(
    name: 'p',
    createdAt: DateTime(2026),
    aspectRatio: 16 / 9,
    resolutionHeight: 1080,
    layers: [cena],
  );

  Scene3DLayer cenaCom({
    List<SceneNode> nos = const [],
    List<Light3D> luzes = const [],
    double ambiente = 0.28,
  }) => Scene3DLayer(
    name: 'Cena',
    startTime: Duration.zero,
    duration: const Duration(seconds: 5),
    scene: Scene3D(nodes: nos, lights: luzes, ambient: ambiente),
  );

  /// A camera do projeto tem fov HORIZONTAL (36 mm sobre a largura) e o
  /// motor quer VERTICAL. Passar um pelo outro deforma a perspectiva em
  /// tudo que nao for quadrado.
  test('o fov horizontal da composicao vira o vertical do motor', () {
    final cena = cenaCom();
    final p = projetoCom(cena);
    motor.montar(
      cena: cena.scene,
      camera: cena.cameraAt(Duration.zero),
      local: Duration.zero,
      largura: 1280,
      altura: 720,
      aspectoDaComposicao: 16 / 9,
    );
    final horizontal = cena.cameraAt(Duration.zero).fovRadians;
    final esperado = _vertical(horizontal, 16 / 9);
    expect(motor.ultimaCena.camera.fovGraus, closeTo(esperado, 1e-6));
    // E o vertical e MENOR que o horizontal num filme largo: se a conta
    // estivesse invertida, o enquadramento abriria em vez de fechar.
    expect(motor.ultimaCena.camera.fovGraus, lessThan(horizontal * 180 / math.pi));
  });

  test('cada no vira uma camada, com posicao, rotacao e escala resolvidas', () {
    final no = SceneNode(
      name: 'Cubo',
      x: AnimatedDouble(120),
      y: AnimatedDouble(-40),
      z: AnimatedDouble(30),
      rotY: AnimatedDouble(45),
      scale: AnimatedDouble(2),
      size: 100,
    );
    final cena = cenaCom(nos: [no]);
    motor.montar(
      cena: cena.scene,
      camera: null,
      local: Duration.zero,
      largura: 640,
      altura: 360,
    );
    final camadas = motor.ultimaCena.camadas;
    expect(camadas, hasLength(1));
    expect(camadas.first.posicaoX, closeTo(120, 1e-6));
    expect(camadas.first.posicaoY, closeTo(-40, 1e-6));
    expect(camadas.first.posicaoZ, closeTo(30, 1e-6));
    expect(camadas.first.rotacaoY, closeTo(45, 1e-6));
    // A escala da camada e o tamanho do no vezes o transform dele.
    expect(camadas.first.escalaX, closeTo(200, 1e-6));
    expect(camadas.first.visivel, isTrue);
  });

  test('as copias de um no viram camadas proprias, com o deslocamento delas', () {
    final no = SceneNode(
      name: 'Cubo',
      x: AnimatedDouble(10),
      instances: const [Vec3(100, 0, 0), Vec3(-100, 0, 0)],
    );
    motor.montar(
      cena: Scene3D(nodes: [no]),
      camera: null,
      local: Duration.zero,
      largura: 640,
      altura: 360,
    );
    final camadas = motor.ultimaCena.camadas;
    expect(camadas, hasLength(2));
    expect(camadas[0].posicaoX, closeTo(110, 1e-6));
    expect(camadas[1].posicaoX, closeTo(-90, 1e-6));
  });

  test('a tinta da camada fica BRANCA e o material da camada desligado', () {
    // O material viaja na GEOMETRIA (uma malha por material). Se a camada
    // sobrescrevesse, todas as faces sairiam com a cor da primeira.
    final no = SceneNode(
      name: 'Cubo',
      material: const Material3D(baseColor: Color(0xFFFF0000)),
    );
    motor.montar(
      cena: Scene3D(nodes: [no]),
      camera: null,
      local: Duration.zero,
      largura: 640,
      altura: 360,
    );
    final camada = motor.ultimaCena.camadas.first;
    expect(camada.cor.r, 255);
    expect(camada.cor.g, 255);
    expect(camada.cor.b, 255);
    expect(camada.material.ligado, isFalse);
  });

  test('a opacidade do no chega na camada, presa entre 0 e 1', () {
    final no = SceneNode(
      name: 'Cubo',
      material: const Material3D(opacity: 0.4),
    );
    motor.montar(
      cena: Scene3D(nodes: [no]),
      camera: null,
      local: Duration.zero,
      largura: 640,
      altura: 360,
    );
    expect(motor.ultimaCena.camadas.first.opacidade, closeTo(0.4, 1e-6));
  });

  test('o ambiente vai para a cena e NAO vira uma quarta luz', () {
    final ambiente = Light3D(kind: Light3DKind.ambient);
    final direcional = Light3D(kind: Light3DKind.directional);
    motor.montar(
      cena: Scene3D(lights: [ambiente, direcional], ambient: 0.5),
      camera: null,
      local: Duration.zero,
      largura: 640,
      altura: 360,
    );
    final c = motor.ultimaCena;
    expect(c.ambienteR, closeTo(0.5, 1e-6));
    expect(c.luzes, hasLength(2));
    // Contar a ambiente duas vezes clarearia a cena inteira.
    expect(c.luzes[0].ligada, isFalse);
    expect(c.luzes[1].ligada, isTrue);
  });

  test('uma luz de forca zero nao esta acesa', () {
    motor.montar(
      cena: Scene3D(
        lights: [Light3D(intensity: AnimatedDouble(0))],
      ),
      camera: null,
      local: Duration.zero,
      largura: 640,
      altura: 360,
    );
    expect(motor.ultimaCena.luzes.first.ligada, isFalse);
  });

  test('a chave do estado e a MESMA para o mesmo quadro e muda com o tempo', () {
    final cena = cenaCom(
      nos: [SceneNode(name: 'Cubo', x: AnimatedDouble(0))],
    );
    final p = projetoCom(cena);

    String chave(Duration t) => estado3DDoQuadro(
      project: p,
      l: cena,
      local: t,
      global: t,
      largura: 640,
      altura: 360,
    ).chave;

    expect(chave(Duration.zero), chave(Duration.zero));
    expect(chave(Duration.zero), isNot(chave(const Duration(seconds: 1))));
  });

  test('a chave muda quando o tamanho do alvo muda', () {
    final cena = cenaCom(nos: [SceneNode(name: 'Cubo')]);
    final p = projetoCom(cena);
    final pequeno = estado3DDoQuadro(
      project: p,
      l: cena,
      local: Duration.zero,
      global: Duration.zero,
      largura: 320,
      altura: 180,
    ).chave;
    final grande = estado3DDoQuadro(
      project: p,
      l: cena,
      local: Duration.zero,
      global: Duration.zero,
      largura: 1920,
      altura: 1080,
    ).chave;
    expect(pequeno, isNot(grande));
  });

  test('a chave muda quando a camera se mexe', () {
    final cena = cenaCom(nos: [SceneNode(name: 'Cubo')]);
    final p = projetoCom(cena);
    String chave(Duration t) => estado3DDoQuadro(
      project: p,
      l: cena,
      local: t,
      global: t,
      largura: 640,
      altura: 360,
    ).chave;
    // A camera da cena tem keyframes de orbita; dois instantes diferentes
    // nao podem cair na mesma chave — senao o preview repetiria o quadro.
    expect(chave(Duration.zero), isNot(chave(const Duration(seconds: 2))));
  });

  test('o nivel de sombra segue a resolucao que o alvo aproveita', () {
    const receita = ReceitaDeQualidade.alta;
    expect(nivelDeSombra3D(receita, 400), 1);
    expect(nivelDeSombra3D(receita, 1080), 2);
    // A receita alta tem teto de 1024: um alvo maior NAO compra sombra
    // melhor, so memoria. Quem tem 2048 e a ultra.
    expect(nivelDeSombra3D(receita, 2160), 2);
    expect(nivelDeSombra3D(ReceitaDeQualidade.ultra, 2160), 3);
    // A emergencia nao tem sombra nenhuma.
    expect(nivelDeSombra3D(ReceitaDeQualidade.emergencia, 1080), 0);
  });

  test('sem geometria a cena e montada vazia, e nao com a anterior', () {
    motor.montar(
      cena: Scene3D(nodes: [SceneNode(name: 'Cubo')]),
      camera: null,
      local: Duration.zero,
      largura: 640,
      altura: 360,
    );
    expect(motor.ultimaCena.camadas, hasLength(1));
    motor.montar(
      cena: const Scene3D(),
      camera: null,
      local: Duration.zero,
      largura: 640,
      altura: 360,
    );
    expect(motor.ultimaCena.camadas, isEmpty);
    expect(motor.ultimaCena.luzes, isEmpty);
  });

  test('o mesmo no nao vira duas camadas no mesmo quadro', () {
    final no = SceneNode(name: 'Cubo');
    motor.montar(
      cena: Scene3D(nodes: [no]),
      camera: null,
      local: Duration.zero,
      largura: 640,
      altura: 360,
    );
    motor.montar(
      cena: Scene3D(nodes: [no]),
      camera: null,
      local: const Duration(seconds: 1),
      largura: 640,
      altura: 360,
    );
    expect(motor.ultimaCena.camadas, hasLength(1));
  });

  test('a lista de camadas nao cresce entre quadros com muitos nos', () {
    final nos = [
      for (var i = 0; i < 8; i++) SceneNode(name: 'Cubo $i'),
    ];
    for (var q = 0; q < 5; q++) {
      motor.montar(
        cena: Scene3D(nodes: nos),
        camera: null,
        local: Duration(milliseconds: q * 40),
        largura: 640,
        altura: 360,
      );
    }
    expect(motor.ultimaCena.camadas, hasLength(8));
  });

  test('o instante 0 do nulo nao desloca a cena (ancora do quadro)', () {
    // O bug de campo: usar o primeiro quadro como referencia dava um
    // deslocamento constante em tudo. A posicao e ABSOLUTA.
    final nulo = SceneNode(name: 'Nulo', isNull: true);
    final filho = SceneNode(
      name: 'Cubo',
      parentId: nulo.id,
      x: AnimatedDouble(0),
    );
    motor.montar(
      cena: Scene3D(nodes: [nulo, filho]),
      camera: null,
      local: Duration.zero,
      largura: 640,
      altura: 360,
    );
    for (final c in motor.ultimaCena.camadas) {
      expect(c.posicaoX, closeTo(0, 1e-6));
      expect(c.posicaoY, closeTo(0, 1e-6));
    }
  });

  test('um filho deslocado de um pai girado da a volta com ele', () {
    final nulo = SceneNode(
      name: 'Nulo',
      isNull: true,
      rotY: AnimatedDouble(90),
    );
    final filho = SceneNode(
      name: 'Cubo',
      parentId: nulo.id,
      x: AnimatedDouble(100),
    );
    motor.montar(
      cena: Scene3D(nodes: [nulo, filho]),
      camera: null,
      local: Duration.zero,
      largura: 640,
      altura: 360,
    );
    // 90 graus em Y leva o +X do filho para o -Z (a mesma convencao do
    // resolveNodeTransform). Se o motor recebesse o filho em coordenadas
    // absolutas, ele ficaria parado em +X.
    final desenhado = motor.ultimaCena.camadas.last;
    expect(desenhado.posicaoX.abs(), lessThan(1e-6));
    expect(desenhado.posicaoZ, closeTo(-100, 1e-6));
  });

  test('o no nulo nao entra como uma camada de geometria', () {
    final nulo = SceneNode(name: 'Nulo', isNull: true);
    motor.montar(
      cena: Scene3D(nodes: [nulo]),
      camera: null,
      local: Duration.zero,
      largura: 640,
      altura: 360,
    );
    // Ele existe na cena (e um pai), e nao desenha nada.
    expect(motor.ultimaCena.camadas, hasLength(1));
    expect(motor.ultimaCena.camadas.first.modelo, -1);
  });

  test('a cena montada nao guarda o tamanho do quadro anterior', () {
    motor.montar(
      cena: const Scene3D(),
      camera: null,
      local: Duration.zero,
      largura: 1920,
      altura: 1080,
      sombra: 3,
      amostras: 4,
    );
    expect(motor.ultimaCena.largura, 1920);
    expect(motor.ultimaCena.altura, 1080);
    expect(motor.ultimaCena.sombra, 3);
    expect(motor.ultimaCena.amostras, 4);
    motor.montar(
      cena: const Scene3D(),
      camera: null,
      local: Duration.zero,
      largura: 640,
      altura: 360,
    );
    expect(motor.ultimaCena.largura, 640);
    expect(motor.ultimaCena.sombra, 0);
    expect(motor.ultimaCena.amostras, 1);
  });

  test('a cena da exportacao e a MESMA do preview, para o mesmo instante', () {
    // §33: preview e exportacao montam o estado pela mesma funcao. A
    // unica diferenca e o TAMANHO DO ALVO — e por isso a exportacao pede
    // a resolucao da composicao e o preview, a que o orcamento permite.
    final cena = cenaCom(nos: [SceneNode(name: 'Cubo')]);
    final p = projetoCom(cena);
    final preview = estado3DDoQuadro(
      project: p,
      l: cena,
      local: const Duration(seconds: 1),
      global: const Duration(seconds: 1),
      largura: 960,
      altura: 540,
    );
    final exportacao = estado3DDoQuadro(
      project: p,
      l: cena,
      local: const Duration(seconds: 1),
      global: const Duration(seconds: 1),
      largura: 1920,
      altura: 1080,
    );
    expect(preview.cena.nodes, hasLength(exportacao.cena.nodes.length));
    expect(preview.camera!.position.x, closeTo(exportacao.camera!.position.x, 1e-9));
    expect(preview.camera!.position.z, closeTo(exportacao.camera!.position.z, 1e-9));
  });

  tearDownAll(motor.limpar);
}

/// A conta do fov vertical, escrita de novo aqui para o teste nao herdar a
/// formula do proprio codigo que ele confere.
double _vertical(double horizontal, double aspecto) {
  final t = math.tan(horizontal / 2);
  return 2 * math.atan(t / aspecto) * 180 / math.pi;
}
