import 'dart:math' as math;
import 'dart:ui';

import 'package:flutter/rendering.dart' show MatrixUtils;

import 'gizmo3d.dart';
import 'layer.dart';
import 'scene3d.dart';
import 'selection_geometry.dart';
import 'video_project.dart';

/// O GIZMO DO OBJETO DA CENA 3D — onde o pivo do no cai no palco e para
/// que lado cada eixo dele aponta.
///
/// ====================== POR QUE ELE E MEDIDO, E NAO CALCULADO ==========
///
/// A pose do no passa por tres espacos antes de virar pixel: a cadeia de
/// pais DENTRO da cena, a camera do motor (que a propria camada orbita) e
/// o transform 2D da camada na composicao. Escrever a direcao do eixo X
/// "na mao" exigiria repetir os tres — e cada um deles muda por conta
/// propria (um pai girado, uma tomada de camera, a camada escalada).
///
/// Aqui vale a MESMA regra do gizmo da camada ([gizmoDaCamada]): pergunta-se
/// onde o no cairia com UMA UNIDADE a mais em cada eixo e a diferenca
/// entre as duas respostas E o eixo. O passo sai no espaco do PAI, que e
/// exatamente o espaco em que `PropDoNo.x/y/z` e escrito — entao arrastar
/// o braco X muda `x` e mais nada, mesmo com o pai girado 90 graus em Y
/// (ali o objeto anda em Z do mundo, e e isso que se ve).
///
/// CONVENCAO DA CENA (a do motor): +Y e PARA CIMA e a camera olha -Z.
/// Na tela o Y do no sobe, e nao desce — e a convencao de qualquer
/// ferramenta 3D, e a que o motor desenha.

/// UM PONTO DO MUNDO DA CENA EM PIXELS DA IMAGEM DA CENA.
///
/// A imagem da cena tem o tamanho da composicao ([comp]) e e o que o motor
/// desenha; a conta e a mesma do pintor de CPU (`renderScene`) e usa o
/// MESMO campo de visao que a ponte manda para o motor — por isso o gizmo
/// cai onde o objeto aparece, e nao perto dele.
///
/// Nulo quando o ponto esta ATRAS da camera: nao ha tela onde por.
Offset? projetarNaImagemDaCena(RenderCamera cam, Vec3 mundo, Size comp) {
  final base = cameraBasis(cam);
  final rel = mundo - cam.position;
  final zc = rel.dot(base.forward);
  if (!cam.orthographic && zc <= cam.near) return null;
  // O FOCAL EM PIXELS SAI DA LARGURA: `fovRadians` e o angulo HORIZONTAL
  // (filme 36 mm sobre a focal), e e dele que a ponte deriva o vertical
  // que o motor recebe. Usar a altura aqui daria outra lente.
  final focalPx = (comp.width / 2) / math.tan(cam.fovRadians / 2);
  // ORTOGRAFICA: o motor le `orthoScale` como ALTURA do mundo no quadro.
  final k = cam.orthographic
      ? comp.height / math.max(1e-6, cam.orthoScale)
      : focalPx / zc;
  return Offset(
    comp.width / 2 + rel.dot(base.right) * k,
    // MENOS: a cena e Y para cima e a imagem e Y para baixo.
    comp.height / 2 - rel.dot(base.up) * k,
  );
}

/// OS OBJETOS QUE SE PEGA NO PALCO: o que tem geometria. Nulo e pai (nao
/// desenha), e invisivel nao se arrasta — alca sobre o que nao aparece
/// promete um gesto sem resposta na tela.
List<SceneNode> objetosDaCena(Scene3D cena) => [
  for (final n in cena.nodes)
    if (!n.isNull && n.visible) n,
];

/// O OBJETO ESCOLHIDO SOZINHO: cena com um objeto so nao precisa de
/// escolha — e o unico que existe.
String? noPadraoDaCena(Scene3D cena) {
  final objetos = objetosDaCena(cena);
  return objetos.length == 1 ? objetos.first.id : objetos.firstOrNull?.id;
}

/// A CAMERA DO MOTOR PARA ESTA CAMADA, sem o nulo da composicao.
///
/// O palco tem a versao completa (`cameraDaCena`, que resolve o nulo da
/// composicao) e passa ela pronta; esta aqui existe para o teste no PC e
/// para o caso simples, e ja inclui a ORBITA do giro X/Y da camada, que e
/// o que a camada de cena faz com esses dois angulos.
RenderCamera cameraDoGizmo(Scene3DLayer camada, Duration local) =>
    orbitarCamera(
      camada.cameraAt(local),
      -camada.rotationX.valueAt(local),
      -camada.rotationY.valueAt(local),
    );

/// O PIVO DO NO EM PIXELS DA COMPOSICAO, ou nulo se ele nao aparece.
Offset? pivoDoNoNaComposicao(
  VideoProject projeto,
  Scene3DLayer camada,
  SceneNode no,
  Duration tempo,
  Size tamanhoDoPalco, {
  Scene3D? cena,
  RenderCamera? camera,
}) {
  final c = cena ?? camada.scene;
  final local = camada.localTime(tempo);
  final cam = camera ?? cameraDoGizmo(camada, local);
  final imagem = projetarNaImagemDaCena(
    cam,
    resolveNodeTransform(c, no, local).position,
    tamanhoDoPalco,
  );
  if (imagem == null) return null;
  return _paraComposicao(projeto, camada, tempo, tamanhoDoPalco, imagem);
}

/// O NO MAIS PROXIMO DO DEDO, pelo PIVO PROJETADO.
///
/// NAO se usa o pintor de CPU para escolher: desenhar a cena inteira em
/// Dart custa milissegundos por mil triangulos (e um modelo importado tem
/// dezenas de milhares) — um toque nao pode pagar um quadro inteiro so
/// para saber em que objeto encostou.
String? noMaisPertoDoDedo(
  VideoProject projeto,
  Scene3DLayer camada,
  Duration tempo,
  Size tamanhoDoPalco,
  Offset dedo, {
  Scene3D? cena,
  RenderCamera? camera,
  double raio = 60,
}) {
  final c = cena ?? camada.scene;
  String? melhorId;
  var melhor = raio;
  for (final no in objetosDaCena(c)) {
    final p = pivoDoNoNaComposicao(
      projeto,
      camada,
      no,
      tempo,
      tamanhoDoPalco,
      cena: c,
      camera: camera,
    );
    if (p == null) continue;
    final d = (p - dedo).distance;
    if (d <= melhor) {
      melhor = d;
      melhorId = no.id;
    }
  }
  return melhorId;
}

/// O GIZMO DE UM NO DA CENA, em pixels da COMPOSICAO.
///
/// [tamanhoDoPalco] e a caixa da IMAGEM da cena — que e o tamanho da
/// composicao, porque e assim que a camada de cena e desenhada.
///
/// [cena] e [camera] chegam prontos do palco (com o nulo da composicao ja
/// resolvido); sem eles vale a cena da propria camada, que e o caso comum
/// e o que os testes no PC usam.
GizmoNaTela? gizmoDoNo(
  VideoProject projeto,
  Scene3DLayer camada,
  String noId,
  Duration tempo,
  Size tamanhoDoPalco, {
  Scene3D? cena,
  RenderCamera? camera,
}) {
  final c = cena ?? camada.scene;
  final no = c.nodeById(noId);
  if (no == null) return null;
  final local = camada.localTime(tempo);
  final cam = camera ?? cameraDoGizmo(camada, local);

  // O PAI RESOLVIDO UMA VEZ SO: o passo de prova e somado a posicao LOCAL
  // do no, e composto de novo com o mesmo pai. E por isso que o avanco que
  // sai do arrasto ja esta no espaco em que `PropDoNo.x` e gravado.
  final paiNo = no.parentId == null ? null : c.nodeById(no.parentId!);
  final pai = paiNo == null
      ? NodeTransform.identity
      : resolveNodeTransform(c, paiNo, local);
  final posLocal = no.positionAt(local);
  final rotX = no.rotX.valueAt(local);
  final rotY = no.rotY.valueAt(local);
  final rotZ = no.rotZ.valueAt(local);
  final escala = no.scale.valueAt(local);

  Offset? tela(Vec3 passo) {
    final xf = composeTransforms(
      pai,
      NodeTransform(
        position: posLocal + passo,
        rotX: rotX,
        rotY: rotY,
        rotZ: rotZ,
        scale: escala,
      ),
    );
    final imagem = projetarNaImagemDaCena(cam, xf.position, tamanhoDoPalco);
    if (imagem == null) return null;
    return _paraComposicao(projeto, camada, tempo, tamanhoDoPalco, imagem);
  }

  final origem = tela(Vec3.zero);
  if (origem == null) return null;
  final px = tela(const Vec3(1, 0, 0));
  final py = tela(const Vec3(0, 1, 0));
  final pz = tela(const Vec3(0, 0, 1));
  // UM PASSO QUE SAI DO CAMPO DE VISAO VIRA EIXO DE COMPRIMENTO ZERO, e
  // nao um vetor absurdo: `eixoVisivel` recusa o gesto em vez de dar um
  // salto de sensibilidade.
  final x = (px ?? origem) - origem;
  final y = (py ?? origem) - origem;
  final z = (pz ?? origem) - origem;
  return GizmoNaTela(
    origem: origem,
    x: x,
    y: y,
    z: z,
    // PIXELS DE COMPOSICAO POR UNIDADE DA CENA, pelo maior dos tres: e a
    // grandeza que diz o quanto um passo de mundo anda na tela.
    escala: [x.distance, y.distance, z.distance].reduce(math.max),
  );
}

/// A IMAGEM DA CENA -> A COMPOSICAO.
///
/// A imagem ocupa uma caixa do tamanho da composicao CENTRADA na posicao
/// da camada; `selectionTransform` e a mesma cadeia que a moldura de
/// selecao usa, entao gizmo e moldura nunca discordam.
Offset _paraComposicao(
  VideoProject projeto,
  Scene3DLayer camada,
  Duration tempo,
  Size tamanhoDoPalco,
  Offset naImagem,
) {
  final m = selectionTransform(projeto, camada, tempo);
  return MatrixUtils.transformPoint(
    m,
    naImagem - Offset(tamanhoDoPalco.width / 2, tamanhoDoPalco.height / 2),
  );
}

