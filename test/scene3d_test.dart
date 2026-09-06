import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/camera3d.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';

const _viewport = Size(1080, 1920);
const _cam = RenderCamera(position: Vec3(0, 0, 900));

SceneFrame _render(Scene3D scene, {RenderCamera cam = _cam}) =>
    renderScene(scene, cam, _viewport, Duration.zero);

/// Quantos triangulos a malha do cubo tem, tirado DA MALHA.
///
/// Os testes de descarte comparavam com numeros escritos a mao (2, 6,
/// 12), que eram a topologia do cubo de oito vertices. Numero a mao
/// nesses testes nao verifica descarte: verifica que ninguem mexeu na
/// malha — e o dia em que a malha muda de proposito (chanfro), eles
/// quebram sem que nada esteja errado.
int get _triangulosDoCubo {
  final malha = element3DMesh(Element3DKind.cube);
  var total = 0;
  for (final face in malha.faces) {
    total += face.length - 2;
  }
  return total;
}

void main() {
  group('Cena 3D — conteiner', () {
    test('cena vazia nao desenha nada (fundo transparente)', () {
      final f = _render(const Scene3D());
      expect(f.opaque, isEmpty);
      expect(f.transparent, isEmpty);
      expect(f.triangles, 0);
      expect(f.drawCalls, 0);
    });

    test('no invisivel nao gera chamada de desenho', () {
      final f = _render(Scene3D(nodes: [
        SceneNode(name: 'A', size: 100, visible: false),
      ]));
      expect(f.drawCalls, 0);
      expect(f.triangles, 0);
    });

    // O TESTE QUE APROVA (spec §11): dois cubos que se cruzam precisam
    // se intercalar. A camada 3D antiga desenhava objeto inteiro por
    // objeto inteiro, entao um sempre ficava todo na frente do outro.
    test('dois cubos cruzados se intercalam — interpenetracao correta',
        () {
      // Girados: de frente, um cubo mostra UMA face e o descarte de
      // costas some com as outras cinco. Girado, mostra tres — que e o
      // caso em que a intercalacao pode ser observada.
      final a = SceneNode(
        name: 'A',
        kind: Element3DKind.cube,
        size: 140,
        x: AnimatedDouble(-60),
        z: AnimatedDouble(-60),
        rotY: AnimatedDouble(35),
        rotX: AnimatedDouble(20),
        material: const Material3D(baseColor: Color(0xFF7C62FF)),
      );
      final b = SceneNode(
        name: 'B',
        kind: Element3DKind.cube,
        size: 140,
        x: AnimatedDouble(60),
        z: AnimatedDouble(60),
        rotY: AnimatedDouble(-25),
        rotX: AnimatedDouble(15),
        material: const Material3D(baseColor: Color(0xFFB8FF3D)),
      );
      final f = _render(Scene3D(nodes: [a, b], lights: [Light3D()]));

      expect(f.opaque.length, greaterThan(4));

      // Ordem de pintura: do mais distante ao mais proximo.
      for (var i = 1; i < f.opaque.length; i++) {
        expect(f.opaque[i].depth,
            lessThanOrEqualTo(f.opaque[i - 1].depth + 1e-9));
      }

      // A prova da interpenetracao: existe triangulo de A depois de um
      // de B E triangulo de B depois de um de A. Se a ordenacao fosse
      // por objeto, so uma das duas seria verdadeira.
      var aAfterB = false, bAfterA = false;
      var seenA = false, seenB = false;
      for (final tri in f.opaque) {
        if (tri.nodeId == a.id) {
          seenA = true;
          if (seenB) aAfterB = true;
        } else if (tri.nodeId == b.id) {
          seenB = true;
          if (seenA) bAfterA = true;
        }
      }
      expect(aAfterB, isTrue, reason: 'nenhum triangulo de A depois de B');
      expect(bAfterA, isTrue, reason: 'nenhum triangulo de B depois de A');
    });

    test('200 instancias do mesmo no = 1 chamada de desenho', () {
      final node = SceneNode(
        name: 'Enxame',
        kind: Element3DKind.cube,
        size: 20,
        instances: [
          for (var i = 0; i < 200; i++)
            Vec3((i % 20) * 26.0 - 260, (i ~/ 20) * 26.0 - 130, 0),
        ],
      );
      final f = _render(Scene3D(nodes: [node], lights: [Light3D()]));
      expect(f.drawCalls, 1);
      // Uma chamada, mas os triangulos de todas as instancias. Com o
      // descarte de costas cada cubo entrega as faces viradas para a
      // camera — ao menos uma, ou seja dois triangulos por instancia.
      expect(f.triangles, greaterThanOrEqualTo(200 * 2));
    });

    // DESCARTE DE COSTAS: num solido fechado, a face virada para o outro
    // lado esta sempre escondida por outra do mesmo solido. Nao emitir
    // corta perto da metade dos triangulos.
    test('cubo de frente descarta o que esta de costas', () {
      final f = _render(Scene3D(nodes: [
        SceneNode(name: 'Cubo', size: 100),
      ], lights: [
        Light3D()
      ]));
      // De frente so se ve a frente: bem menos que a malha inteira, e
      // mais que nada. A fracao e o que o descarte promete; o numero
      // exato e topologia, e topologia pode mudar.
      expect(f.opaque, isNotEmpty);
      expect(f.opaque.length, lessThan(_triangulosDoCubo * 0.45));
      expect(f.triangles, f.opaque.length);
    });

    test('cubo girado mostra mais lados que de frente', () {
      final frente = _render(Scene3D(nodes: [
        SceneNode(name: 'Cubo', size: 100),
      ], lights: [
        Light3D()
      ]));
      final girado = _render(Scene3D(nodes: [
        SceneNode(
          name: 'Cubo',
          size: 100,
          rotY: AnimatedDouble(35),
          rotX: AnimatedDouble(25),
        ),
      ], lights: [
        Light3D()
      ]));
      // Girado em dois eixos aparecem tres lados; de frente, um. O que
      // se verifica e essa relacao, nao a contagem.
      expect(girado.opaque.length, greaterThan(frente.opaque.length));
      expect(girado.opaque.length, lessThan(_triangulosDoCubo));
    });

    test('vidro NAO descarta as costas — se ve o fundo por dentro', () {
      final f = _render(Scene3D(nodes: [
        SceneNode(
          name: 'Vidro',
          size: 100,
          material: const Material3D(opacity: 0.4),
        ),
      ], lights: [
        Light3D()
      ]));
      // A malha INTEIRA continua ali — nada foi descartado.
      expect(f.transparent.length, _triangulosDoCubo);
    });

    test('triangulo inteiramente fora da tela nao entra no quadro', () {
      // Perto da camera e muito para o lado: o volume envolvente ainda
      // passa, mas os triangulos caem fora da borda.
      final dentro = _render(Scene3D(nodes: [
        SceneNode(name: 'Centro', size: 60),
      ], lights: [
        Light3D()
      ]));
      final fora = _render(Scene3D(nodes: [
        SceneNode(name: 'Centro', size: 60),
        SceneNode(name: 'Beirada', size: 60, x: AnimatedDouble(2400)),
      ], lights: [
        Light3D()
      ]));
      expect(fora.triangles, dentro.triangles);
    });

    // A ordenacao por balde precisa dar a MESMA ordem de profundidade
    // que a ordenacao por comparacao — senao trocou correcao por
    // velocidade, que nao e negocio.
    test('ordenacao por balde ordena igual a por comparacao', () {
      final tris = <RenderTri>[
        for (var i = 0; i < 500; i++)
          RenderTri(
            a: Offset.zero,
            b: Offset.zero,
            c: Offset.zero,
            depth: ((i * 7919) % 1000) + (i % 13) * 0.001,
            color: const Color(0xFFFFFFFF),
            transparent: false,
          ),
      ];
      final esperado = [...tris]
        ..sort((x, y) => y.depth.compareTo(x.depth));
      final obtido = [...tris];
      depthSort(obtido);
      for (var i = 0; i < tris.length; i++) {
        expect(obtido[i].depth, closeTo(esperado[i].depth, 1e-9),
            reason: 'posicao $i');
      }
    });

    test('ordenacao por balde aguenta lista curta e profundidade igual',
        () {
      final iguais = <RenderTri>[
        for (var i = 0; i < 200; i++)
          RenderTri(
            a: Offset.zero,
            b: Offset.zero,
            c: Offset.zero,
            depth: 42,
            color: const Color(0xFFFFFFFF),
            transparent: false,
          ),
      ];
      depthSort(iguais);
      expect(iguais.length, 200);

      final curta = <RenderTri>[
        for (final d in [5.0, 1.0, 3.0])
          RenderTri(
            a: Offset.zero,
            b: Offset.zero,
            c: Offset.zero,
            depth: d,
            color: const Color(0xFFFFFFFF),
            transparent: false,
          ),
      ];
      depthSort(curta);
      expect(curta.map((t) => t.depth).toList(), [5.0, 3.0, 1.0]);
    });

    test('objeto fora do frustum e descartado por volume envolvente', () {
      final f = _render(Scene3D(nodes: [
        SceneNode(name: 'Dentro', size: 80),
        SceneNode(name: 'Fora lateral', size: 80, x: AnimatedDouble(40000)),
        SceneNode(name: 'Atras', size: 80, z: AnimatedDouble(4000)),
      ]));
      expect(f.culled, 2);
      expect(f.drawCalls, 1);
    });

    test('transparente sai em lista propria e nunca ordena com opaco',
        () {
      final f = _render(Scene3D(nodes: [
        SceneNode(name: 'Solido', size: 90, x: AnimatedDouble(-120)),
        SceneNode(
          name: 'Vidro',
          size: 90,
          x: AnimatedDouble(120),
          material: const Material3D(opacity: 0.4),
        ),
      ], lights: [
        Light3D()
      ]));
      expect(f.opaque, isNotEmpty);
      expect(f.transparent, isNotEmpty);
      expect(f.opaque.every((t) => !t.transparent), isTrue);
      expect(f.transparent.every((t) => t.transparent), isTrue);
      for (var i = 1; i < f.transparent.length; i++) {
        expect(f.transparent[i].depth,
            lessThanOrEqualTo(f.transparent[i - 1].depth + 1e-9));
      }
    });

    // PROFUNDIDADE EXPORTADA (§8): sem ela o compositor so consegue pôr
    // uma camada 2D toda na frente ou toda atras da cena. Com ela, da
    // para enfiar um texto ENTRE dois objetos.
    test('profundidade exportada situa uma camada 2D entre dois objetos',
        () {
      // Camera em z=900 olhando para a origem. O cubo de vertices +-1 e
      // escalado por `size`, entao a face da frente do cubo de z=+300
      // fica em z=400 (500 da camera) e a do outro em z=-200 (1100).
      final perto = SceneNode(
          name: 'Perto',
          size: 100,
          x: AnimatedDouble(-150),
          z: AnimatedDouble(300));
      final longe = SceneNode(
          name: 'Longe',
          size: 100,
          x: AnimatedDouble(150),
          z: AnimatedDouble(-300));
      final f = _render(
          Scene3D(nodes: [longe, perto], lights: [Light3D()]));

      final cy = _viewport.height / 2;
      // Ponto de tela no centro de cada cubo (o de tras projeta menor).
      final dPerto = depthAtPoint(f, Offset(90, cy));
      final dLonge = depthAtPoint(f, Offset(744, cy));
      expect(dPerto, isNotNull);
      expect(dLonge, isNotNull);
      expect(dPerto!, closeTo(500, 1));
      expect(dLonge!, closeTo(1100, 1));
      expect(dPerto, lessThan(dLonge));

      // Nada cobre o canto: a consulta devolve null e a camada 2D fica
      // visivel ali.
      expect(depthAtPoint(f, const Offset(6, 6)), isNull);

      // A decisao do compositor para uma camada 2D a 800 da camera:
      // atras do cubo da frente, na frente do de tras.
      const camada2D = 800.0;
      expect(camada2D > dPerto, isTrue);
      expect(camada2D < dLonge, isTrue);

      final range = sceneDepthRange(f);
      expect(range.near, lessThan(range.far));
      expect(range.near, lessThanOrEqualTo(dPerto));
      expect(range.far, greaterThanOrEqualTo(dLonge));
    });

    test('tocar na cena seleciona o objeto sob o dedo', () {
      final esq = SceneNode(
          name: 'Esq', size: 120, x: AnimatedDouble(-200));
      final dir = SceneNode(
          name: 'Dir', size: 120, x: AnimatedDouble(200));
      final f = _render(Scene3D(nodes: [esq, dir], lights: [Light3D()]));

      final cy = _viewport.height / 2;
      final leftHit = pickNodeAt(f, Offset(155, cy));
      final rightHit = pickNodeAt(f, Offset(925, cy));
      expect(leftHit, esq.id);
      expect(rightHit, dir.id);
      expect(pickNodeAt(f, const Offset(4, 4)), isNull);
    });

    test('luz fora de alcance nao ilumina (culling de luz por objeto)',
        () {
      const mat = Material3D(baseColor: Color(0xFFFFFFFF), roughness: 1);
      const normal = Vec3(0, 0, 1);
      final perto = shadeFace(
        scene: Scene3D(lights: [
          Light3D(
            kind: Light3DKind.point,
            position: const Vec3(0, 0, 200),
            range: 600,
          )
        ], ambient: 0),
        material: mat,
        normal: normal,
        point: Vec3.zero,
        t: Duration.zero,
      );
      final longe = shadeFace(
        scene: Scene3D(lights: [
          Light3D(
            kind: Light3DKind.point,
            position: const Vec3(0, 0, 5000),
            range: 600,
          )
        ], ambient: 0),
        material: mat,
        normal: normal,
        point: Vec3.zero,
        t: Duration.zero,
      );
      expect(perto.r, greaterThan(0.2));
      expect(longe.r, 0);
    });

    test('degradacao rebaixa na ordem da spec, sem apagar objetos', () {
      final scene = Scene3D(
        nodes: [SceneNode(name: 'A'), SceneNode(name: 'B')],
        lights: [
          Light3D(castsShadow: true),
          Light3D(castsShadow: true),
          Light3D(castsShadow: true),
        ],
      );
      final s2 = degradeScene(scene, 2);
      expect(s2.lights.where((l) => l.castsShadow).length, 1);
      expect(s2.msaa, isTrue);
      final s4 = degradeScene(scene, 4);
      expect(s4.msaa, isFalse);
      expect(s4.nodes.length, 2);
    });

    test('camada Cena 3D expoe os keyframes de nos e camera na barra',
        () {
      final node = SceneNode(
        name: 'A',
        x: AnimatedDouble(0)
            .withKeyframe(const Duration(milliseconds: 500), 100),
      );
      final layer = Scene3DLayer(
        name: 'Cena',
        startTime: Duration.zero,
        duration: const Duration(seconds: 4),
        scene: Scene3D(nodes: [node]),
        camera: Camera3D().copyWith(
          posZ: AnimatedDouble(900)
              .withKeyframe(const Duration(seconds: 2), 300),
        ),
      );
      expect(layer.moduleTimesUs, contains(500000));
      expect(layer.moduleTimesUs, contains(2000000));
    });
  });

  group('Cena 3D — camera', () {
    test('focal, angulo de visao e zoom sao a mesma grandeza', () {
      final focal = focalFromFov(54);
      expect(focal, closeTo(35.3, 0.4));
      final cam = RenderCamera(focalLength: focal);
      expect(cam.fovDegrees, closeTo(54, 0.01));
      final zoom = zoomFromFocal(focal, 1080);
      expect(focalFromZoom(zoom, 1080), closeTo(focal, 1e-6));
    });

    test('converter entre 1 e 2 nos preserva o enquadramento', () {
      final two = Camera3D(
        kind: CameraKind.twoNode,
        posX: AnimatedDouble(300),
        posY: AnimatedDouble(200),
        posZ: AnimatedDouble(700),
        poiX: AnimatedDouble(-50),
        poiY: AnimatedDouble(20),
        poiZ: AnimatedDouble(0),
      );
      final one = two.convertedTo(CameraKind.oneNode, Duration.zero);
      final f1 = two.forwardAt(Duration.zero);
      final f2 = one.forwardAt(Duration.zero);
      expect(f2.x, closeTo(f1.x, 1e-6));
      expect(f2.y, closeTo(f1.y, 1e-6));
      expect(f2.z, closeTo(f1.z, 1e-6));
      expect(one.positionAt(Duration.zero).x, closeTo(300, 1e-9));

      // E a volta tambem.
      final back = one.convertedTo(CameraKind.twoNode, Duration.zero);
      final f3 = back.forwardAt(Duration.zero);
      expect(f3.x, closeTo(f1.x, 1e-6));
      expect(f3.y, closeTo(f1.y, 1e-6));
      expect(f3.z, closeTo(f1.z, 1e-6));
    });

    test('pinca aproxima em Z e NAO mexe na distancia focal', () {
      final cam = Camera3D(posZ: AnimatedDouble(800));
      final after = dollyCamera(cam, 2, Duration.zero);
      // A lente fica exatamente a mesma: aproximar muda PERSPECTIVA.
      expect(after.focalLength.base, cam.focalLength.base);
      expect(after.fovAt(Duration.zero),
          closeTo(cam.fovAt(Duration.zero), 1e-9));
      expect(after.positionAt(Duration.zero).z,
          lessThan(cam.positionAt(Duration.zero).z));
    });

    test('orbita gira em torno do pivo fixo, mantendo o raio', () {
      final cam = Camera3D(
        kind: CameraKind.twoNode,
        posZ: AnimatedDouble(600),
      );
      const pivot = Vec3.zero;
      final d0 = (cam.positionAt(Duration.zero) - pivot).length;
      final after = orbitCamera(cam, pivot, 35, 12, Duration.zero);
      final d1 = (after.positionAt(Duration.zero) - pivot).length;
      expect(d1, closeTo(d0, 1e-6));
      expect(after.positionAt(Duration.zero).x,
          isNot(closeTo(cam.positionAt(Duration.zero).x, 1e-3)));

      // Trocar o pivo no meio do gesto e o que mais atrapalha: com o
      // pivo fixo, dois passos de 20 graus = um passo de 40.
      final a = orbitCamera(
          orbitCamera(cam, pivot, 20, 0, Duration.zero),
          pivot,
          20,
          0,
          Duration.zero);
      final b = orbitCamera(cam, pivot, 40, 0, Duration.zero);
      expect(a.positionAt(Duration.zero).x,
          closeTo(b.positionAt(Duration.zero).x, 1e-6));
      expect(a.positionAt(Duration.zero).z,
          closeTo(b.positionAt(Duration.zero).z, 1e-6));
    });

    test('enquadrar tudo poe a cena inteira dentro do quadro', () {
      final scene = Scene3D(nodes: [
        SceneNode(name: 'A', size: 100, x: AnimatedDouble(-400)),
        SceneNode(name: 'B', size: 100, x: AnimatedDouble(400)),
      ], lights: [
        Light3D()
      ]);
      final bounds = sceneBounds(scene, Duration.zero);
      final cam = frameBounds(
          Camera3D(kind: CameraKind.twoNode, posZ: AnimatedDouble(200)),
          bounds,
          Duration.zero);
      final f = renderScene(
          scene, cam.renderAt(Duration.zero), _viewport, Duration.zero);
      expect(f.culled, 0);
      expect(f.drawCalls, 2);
      for (final tri in f.opaque) {
        for (final p in [tri.a, tri.b, tri.c]) {
          expect(p.dx, inInclusiveRange(-1, _viewport.width + 1));
        }
      }
    });

    test('alinhar camera a vista assume o enquadramento da vista', () {
      final view = orthoViewCamera(SceneView.top);
      final cam = alignToView(Camera3D(kind: CameraKind.twoNode), view);
      final p = cam.positionAt(Duration.zero);
      expect(p.y, closeTo(view.position.y, 1e-6));
      expect(cam.pointOfInterestAt(Duration.zero).y, closeTo(0, 1e-6));
    });

    test('rigs geram keyframes REAIS, editaveis depois', () {
      final cam = Camera3D(posZ: AnimatedDouble(800));
      final orbita = applyCameraRig(cam, CameraRig.orbit,
          duration: const Duration(seconds: 4), radius: 700);
      expect(orbita.posX.keyframes.length, 9);
      expect(orbita.kind, CameraKind.twoNode);

      final vertigo = applyCameraRig(cam, CameraRig.dollyZoom,
          duration: const Duration(seconds: 3));
      expect(vertigo.focalLength.keyframes.length, 2);
      // Aproxima e abre o angulo: focal FINAL menor que a inicial.
      expect(vertigo.focalLength.valueAt(const Duration(seconds: 3)),
          lessThan(vertigo.focalLength.valueAt(Duration.zero)));
    });

    test('um dedo sobre a camada selecionada move a camada, nao a camera',
        () {
      expect(
        resolveTouch(
            onSelectedLayer: true,
            onOtherLayer: false,
            navigationMode: false),
        TouchIntent.moveLayer,
      );
      expect(
        resolveTouch(
            onSelectedLayer: false,
            onOtherLayer: true,
            navigationMode: false),
        TouchIntent.selectLayer,
      );
      // Area vazia: o dedo orbita.
      expect(
        resolveTouch(
            onSelectedLayer: false,
            onOtherLayer: false,
            navigationMode: false),
        TouchIntent.orbitCamera,
      );
      // Modo navegacao ligado: o dedo SEMPRE gira a camera, mesmo em
      // cima da camada selecionada.
      expect(
        resolveTouch(
            onSelectedLayer: true,
            onOtherLayer: false,
            navigationMode: true),
        TouchIntent.orbitCamera,
      );
    });

    // §9: camera com rotacao e orientacao zero produz O MESMO
    // enquadramento que a projecao padrao sem camera.
    test('camera neutra enquadra igual a projecao padrao', () {
      final scene = Scene3D(nodes: [
        SceneNode(name: 'A', size: 120, x: AnimatedDouble(140)),
      ], lights: [
        Light3D()
      ]);
      final semCamera = renderScene(
          scene, const RenderCamera(), _viewport, Duration.zero);
      final comCamera = renderScene(
          scene, Camera3D().renderAt(Duration.zero), _viewport,
          Duration.zero);
      expect(comCamera.opaque.length, semCamera.opaque.length);
      for (var i = 0; i < comCamera.opaque.length; i++) {
        expect(comCamera.opaque[i].a.dx,
            closeTo(semCamera.opaque[i].a.dx, 1e-9));
        expect(comCamera.opaque[i].a.dy,
            closeTo(semCamera.opaque[i].a.dy, 1e-9));
      }
    });

    // O teste que separa "lente" de "borrao cinza".
    test('iris hexagonal com ganho alto produz hexagonos brilhantes', () {
      final scene = Scene3D(nodes: [
        SceneNode(
          name: 'Luz',
          size: 40,
          z: AnimatedDouble(-900),
          material: const Material3D(
              baseColor: Color(0xFFFFFFFF),
              kind: MaterialKind.unlit),
        ),
      ], ambient: 0);
      final f = _render(scene);
      expect(f.opaque, isNotEmpty);

      final dof = DepthOfField(
        enabled: true,
        focusDistance: AnimatedDouble(600),
        aperture: AnimatedDouble(60),
        irisShape: IrisShape.hexagon,
        highlightGain: AnimatedDouble(80),
        highlightThreshold: AnimatedDouble(0.6),
      );
      final sprites = bokehSprites(f, dof, Duration.zero);
      expect(sprites, isNotEmpty,
          reason: 'ponto de luz fora de foco tem de virar bola');
      // Brilhante: o ganho levanta o valor acima do original.
      expect(sprites.first.color.r, greaterThan(0.9));
      expect(sprites.first.radius, greaterThan(5));

      // O formato e hexagonal de verdade, nao um circulo: com raio 20 o
      // hexagono mede 40 de ponta a ponta e 34,6 entre lados opostos —
      // um circulo mediria 40 nos dois.
      expect(irisSides(IrisShape.hexagon), 6);
      final hex = irisPath(IrisShape.hexagon, 20).getBounds();
      expect(hex.height, closeTo(40, 0.5));
      expect(hex.width, closeTo(34.64, 0.5));
      final circulo =
          irisPath(IrisShape.hexagon, 20, roundness: 100).getBounds();
      expect(circulo.width, closeTo(40, 0.5));
      expect(circulo.height, closeTo(40, 0.5));

      // Sem ganho, nada vira bola — fica so o desfoque.
      final semGanho = bokehSprites(
          f, dof.copyWith(highlightGain: AnimatedDouble(0)),
          Duration.zero);
      expect(semGanho, isEmpty);

      // Objeto escuro nao passa do limiar, mesmo fora de foco.
      final escura = _render(Scene3D(nodes: [
        SceneNode(
          name: 'Escuro',
          size: 40,
          z: AnimatedDouble(-900),
          material: const Material3D(
              baseColor: Color(0xFF101010), kind: MaterialKind.unlit),
        ),
      ], ambient: 0));
      expect(bokehSprites(escura, dof, Duration.zero), isEmpty);
    });

    test('vidro na frente de opaco nao esconde o opaco', () {
      final opaco = SceneNode(
          name: 'Opaco', size: 100, z: AnimatedDouble(-200));
      final vidro = SceneNode(
        name: 'Vidro',
        size: 140,
        z: AnimatedDouble(300),
        material: const Material3D(opacity: 0.35),
      );
      final f = _render(
          Scene3D(nodes: [vidro, opaco], lights: [Light3D()]));
      // O opaco continua na lista de opacos: o vidro nao "escreve
      // profundidade" e por isso nao o apaga.
      expect(f.opaque, isNotEmpty);
      expect(f.transparent, isNotEmpty);
      // E o vidro e desenhado DEPOIS, por estar noutra lista.
      expect(f.transparent.every((t) => t.transparent), isTrue);
    });

    test('dois vidros sobrepostos ordenam do mais distante ao proximo',
        () {
      final a = SceneNode(
        name: 'V1',
        size: 100,
        z: AnimatedDouble(200),
        material: const Material3D(opacity: 0.4),
      );
      final b = SceneNode(
        name: 'V2',
        size: 100,
        z: AnimatedDouble(-200),
        material: const Material3D(opacity: 0.4),
      );
      final f = _render(Scene3D(nodes: [a, b], lights: [Light3D()]));
      expect(f.opaque, isEmpty);
      expect(f.transparent.first.depth,
          greaterThanOrEqualTo(f.transparent.last.depth));
    });

    // §11: frame 200 direto e identico ao frame 200 depois de percorrer
    // do zero — o renderizador nao pode guardar estado escondido.
    test('frame 200 direto e igual ao frame 200 apos rodar do zero', () {
      final scene = Scene3D(nodes: [
        SceneNode(
          name: 'Girando',
          size: 110,
          rotY: AnimatedDouble(0)
              .withKeyframe(Duration.zero, 0)
              .withKeyframe(const Duration(seconds: 10), 720),
          x: AnimatedDouble(0)
              .withKeyframe(Duration.zero, -300)
              .withKeyframe(const Duration(seconds: 10), 300),
        ),
      ], lights: [
        Light3D()
      ]);
      const frame200 = Duration(microseconds: 200 * 1000000 ~/ 30);

      // NAO comparar o quadro 200 com o quadro 0: sao instantes
      // diferentes de uma coisa que gira e anda, e nao ha razao nenhuma
      // para terem a mesma contagem de triangulos. A igualdade que
      // existia era coincidencia da malha antiga, e o teste passava por
      // acidente. O determinismo se verifica abaixo, comparando o
      // quadro 200 com ele mesmo.
      final alvo = renderScene(scene, _cam, _viewport, frame200);

      for (var i = 0; i < 200; i++) {
        renderScene(scene, _cam, _viewport,
            Duration(microseconds: i * 1000000 ~/ 30));
      }
      final depois = renderScene(scene, _cam, _viewport, frame200);

      expect(depois.opaque.length, alvo.opaque.length);
      for (var i = 0; i < alvo.opaque.length; i++) {
        expect(depois.opaque[i].a.dx, alvo.opaque[i].a.dx);
        expect(depois.opaque[i].depth, alvo.opaque[i].depth);
        expect(depois.opaque[i].color, alvo.opaque[i].color);
      }
    });

    // §9 camera: NENHUMA ajuda visual entra na exportacao. O que a
    // exportacao usa e so `renderScene` — e ela nao conhece grade,
    // frustum, eixos nem plano de foco.
    test('ajudas de cena nao mudam um unico triangulo exportado', () {
      final scene = Scene3D(
        nodes: [SceneNode(name: 'A', size: 100)],
        lights: [Light3D()],
        showFloorGrid: true,
      );
      final comGrade = _render(scene);
      final semGrade = _render(scene.copyWith(showFloorGrid: false));
      expect(semGrade.triangles, comGrade.triangles);
      expect(semGrade.drawCalls, comGrade.drawCalls);
      for (var i = 0; i < comGrade.opaque.length; i++) {
        expect(semGrade.opaque[i].a, comGrade.opaque[i].a);
        expect(semGrade.opaque[i].color, comGrade.opaque[i].color);
      }
    });

    test('vista salva guarda e devolve o enquadramento exato', () {
      const view = SavedView(
        name: 'Plano geral',
        position: Vec3(400, 250, 900),
        target: Vec3(0, 40, 0),
      );
      final cam = alignToView(
        Camera3D(kind: CameraKind.twoNode),
        RenderCamera(position: view.position, target: view.target),
      );
      expect(cam.positionAt(Duration.zero).x, closeTo(400, 1e-9));
      expect(cam.pointOfInterestAt(Duration.zero).y, closeTo(40, 1e-9));
    });

    test('modo rascunho desliga o caro sem mudar o que a cena contem',
        () {
      final scene = Scene3D(
        nodes: [SceneNode(name: 'A')],
        lights: [Light3D()],
      );
      final draft = scene.copyWith(draftMode: true);
      expect(draft.nodes.length, scene.nodes.length);
      final a = _render(scene);
      final b = _render(draft);
      expect(b.triangles, a.triangles);
    });
  });
}
