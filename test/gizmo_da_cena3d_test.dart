// O GIZMO DO OBJETO DA CENA 3D — onde ele cai e para onde cada eixo aponta.
//
// A CONTA DESTE TESTE E A DO APARELHO, sem motor: `gizmoDoNo` projeta o
// pivo do no pela MESMA camera que a ponte manda ao motor e leva o ponto
// para a composicao pela MESMA matriz da moldura de selecao. Se o eixo
// fosse adivinhado em vez de medido, o caso do pai girado 90 graus em Y
// cairia aqui — e e justamente ele que quebrava a mao de quem arrasta.


import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/gizmo3d.dart';
import 'package:aurea/src/features/editor/domain/gizmo_da_cena3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/gizmo3d_painter.dart';
import 'package:aurea/src/features/editor/presentation/widgets/gizmo_da_cena_overlay.dart'
    show noAtivoDaCena;
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

const double _cw = 1920;
const double _ch = 1080;
const _palco = Size(_cw, _ch);
const _centro = Offset(_cw / 2, _ch / 2);
const _t0 = Duration.zero;

Scene3DLayer _camada(List<SceneNode> nos) => Scene3DLayer(
  name: 'Cena',
  startTime: _t0,
  duration: const Duration(seconds: 5),
  is3D: true,
  scene: Scene3D(nodes: nos),
  position: AnimatedOffset(_centro),
  scaleX: AnimatedDouble(1),
  scaleY: AnimatedDouble(1),
);

VideoProject _projeto(Scene3DLayer camada) => VideoProject(
  name: 'gizmo do no',
  createdAt: DateTime(2026),
  resolutionHeight: 1080,
  aspectRatio: 16 / 9,
  layers: [camada],
);

GizmoNaTela _gizmo(List<SceneNode> nos, String noId) {
  final camada = _camada(nos);
  final g = gizmoDoNo(_projeto(camada), camada, noId, _t0, _palco);
  expect(g, isNotNull, reason: 'o no deveria aparecer no quadro');
  return g!;
}

void main() {
  test('objeto na origem: o gizmo cai no centro da composicao', () {
    final no = SceneNode(name: 'Cubo');
    final g = _gizmo([no], no.id);

    expect(g.origem.dx, closeTo(_cw / 2, 0.5));
    expect(g.origem.dy, closeTo(_ch / 2, 0.5));
  });

  test('+100 em X anda para a DIREITA, e so para a direita', () {
    final no = SceneNode(name: 'Cubo', x: AnimatedDouble(100));
    final g = _gizmo([no], no.id);

    expect(g.origem.dx, greaterThan(_cw / 2 + 10));
    expect(g.origem.dy, closeTo(_ch / 2, 0.5));
  });

  test('o Y do no SOBE na tela (a cena e Y para cima)', () {
    final no = SceneNode(name: 'Cubo', y: AnimatedDouble(100));
    final g = _gizmo([no], no.id);

    expect(g.origem.dy, lessThan(_ch / 2 - 10));
    expect(g.origem.dx, closeTo(_cw / 2, 0.5));
  });

  test('os tres eixos saem medidos: X para a direita, Y para cima', () {
    final no = SceneNode(name: 'Cubo');
    final g = _gizmo([no], no.id);

    expect(g.x.dx, greaterThan(0));
    expect(g.x.dy.abs(), lessThan(1e-6));
    expect(g.y.dy, lessThan(0));
    expect(g.y.dx.abs(), lessThan(1e-6));
    // O Z APONTA PARA O OLHO com a camera parada: comprimento quase nulo
    // na tela, e por isso [eixoVisivel] recusa o gesto em vez de dar um
    // salto de sensibilidade.
    expect(eixoVisivel(g, EixoDoGizmo.z), isFalse);
  });

  test('no filho de pai girado 90 em Y: o braco X anda em Z do mundo', () {
    final pai = SceneNode(name: 'Nulo', isNull: true, rotY: AnimatedDouble(90));
    final filho = SceneNode(name: 'Cubo', parentId: pai.id);
    final g = _gizmo([pai, filho], filho.id);

    // O X LOCAL VIROU O -Z DO MUNDO: ele anda na direcao do olho, entao
    // nao anda na tela — e o eixo recusa o arrasto.
    expect(eixoVisivel(g, EixoDoGizmo.x), isFalse);
    // E o Z LOCAL virou o X do mundo: e ele que agora corre na horizontal.
    expect(g.z.dx.abs(), greaterThan(g.x.dx.abs()));
    expect(g.z.dy.abs(), lessThan(1e-6));
  });

  test('o passo do arrasto sai no espaco do PAI, e nao no da tela', () {
    final no = SceneNode(name: 'Cubo');
    final g = _gizmo([no], no.id);

    // Andar com o dedo exatamente o comprimento do eixo X na tela tem de
    // valer UMA unidade da propriedade.
    expect(avancoNoEixo(g.x, g.x), closeTo(1, 1e-9));
    // E andar de lado no eixo nao mexe nele.
    expect(avancoNoEixo(g.x, g.y), closeTo(0, 1e-9));
  });

  test('arrastar o eixo X muda so x', () {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final e = container.read(editorControllerProvider.notifier);
    e.addScene3DLayer(_t0);
    final id = container.read(editorControllerProvider).layers.single.id;
    e.addSceneNode(id, Element3DKind.cube);
    final antes =
        (container.read(editorControllerProvider).layerById(id)!
                as Scene3DLayer)
            .scene
            .nodes
            .single;

    e.editSceneNodeProp(id, antes.id, PropDoNo.x, _t0, 137);

    final depois =
        (container.read(editorControllerProvider).layerById(id)!
                as Scene3DLayer)
            .scene
            .nodes
            .single;
    expect(depois.x.valueAt(_t0), 137);
    expect(depois.y.valueAt(_t0), antes.y.valueAt(_t0));
    expect(depois.z.valueAt(_t0), antes.z.valueAt(_t0));
    expect(depois.rotX.valueAt(_t0), antes.rotX.valueAt(_t0));
    expect(depois.rotY.valueAt(_t0), antes.rotY.valueAt(_t0));
    expect(depois.rotZ.valueAt(_t0), antes.rotZ.valueAt(_t0));
    expect(depois.scale.valueAt(_t0), antes.scale.valueAt(_t0));
  });

  test('trocar de objeto MOVE o gizmo (e o padrao e o primeiro)', () {
    final a = SceneNode(name: 'A');
    final b = SceneNode(name: 'B', x: AnimatedDouble(300));
    final camada = _camada([a, b]);
    final projeto = _projeto(camada);

    final ga = gizmoDoNo(projeto, camada, a.id, _t0, _palco)!;
    final gb = gizmoDoNo(projeto, camada, b.id, _t0, _palco)!;
    expect(ga.origem.dx, closeTo(_cw / 2, 0.5));
    expect(gb.origem.dx, greaterThan(ga.origem.dx + 20));

    // Quem escolhe o objeto e o provedor da selecao; sem escolha vale o
    // primeiro da cena, e uma escolha que nao existe mais nao trava o
    // gizmo num fantasma.
    expect(noAtivoDaCena(camada.scene, null), a.id);
    expect(noAtivoDaCena(camada.scene, b.id), b.id);
    expect(noAtivoDaCena(camada.scene, 'nao-existe'), a.id);
  });

  test('cena sem objeto visivel: nao ha gizmo', () {
    final escondido = SceneNode(name: 'Cubo', visible: false);
    final nulo = SceneNode(name: 'Nulo', isNull: true);
    final camada = _camada([escondido, nulo]);

    expect(objetosDaCena(camada.scene), isEmpty);
    expect(noAtivoDaCena(camada.scene, null), isNull);
    expect(noAtivoDaCena(camada.scene, escondido.id), isNull);
  });

  test('escala por eixo: arrastar o braco X muda SO o scaleX', () {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final e = container.read(editorControllerProvider.notifier);
    e.addScene3DLayer(_t0);
    final id = container.read(editorControllerProvider).layers.single.id;
    e.addSceneNode(id, Element3DKind.cube);
    SceneNode no() =>
        (container.read(editorControllerProvider).layerById(id)!
                as Scene3DLayer)
            .scene
            .nodes
            .single;
    final antes = no();

    e.editSceneNodeProp(id, antes.id, PropDoNo.escalaX, _t0, 2.5);

    final depois = no();
    expect(depois.scaleX.valueAt(_t0), 2.5);
    expect(depois.scaleY.valueAt(_t0), 1);
    expect(depois.scaleZ.valueAt(_t0), 1);
    expect(depois.scale.valueAt(_t0), antes.scale.valueAt(_t0));
    expect(depois.x.valueAt(_t0), antes.x.valueAt(_t0));
    expect(depois.y.valueAt(_t0), antes.y.valueAt(_t0));
  });

  test('escala por eixo estica SO aquele eixo no desenho', () {
    // A prova que importa nao e o campo existir, e a GEOMETRIA mudar: um
    // cubo com scaleX=2 tem de ficar duas vezes mais largo e continuar
    // com a mesma altura.
    const cam = RenderCamera(position: Vec3(0, 0, 900));
    const viewport = Size(1000, 1000);

    (double largura, double altura) caixa(Scene3D cena) {
      final quadro = renderScene(cena, cam, viewport, _t0);
      var minX = double.infinity, maxX = -double.infinity;
      var minY = double.infinity, maxY = -double.infinity;
      for (final tri in [...quadro.opaque, ...quadro.transparent]) {
        for (final p in [tri.a, tri.b, tri.c]) {
          if (p.dx < minX) minX = p.dx;
          if (p.dx > maxX) maxX = p.dx;
          if (p.dy < minY) minY = p.dy;
          if (p.dy > maxY) maxY = p.dy;
        }
      }
      return (maxX - minX, maxY - minY);
    }

    final normal = caixa(Scene3D(nodes: [SceneNode(name: 'Cubo')]));
    final esticado = caixa(
      Scene3D(
        nodes: [SceneNode(name: 'Cubo', scaleX: AnimatedDouble(2))],
      ),
    );

    expect(normal.$1, greaterThan(0));
    expect(esticado.$1 / normal.$1, closeTo(2, 0.02));
    expect(esticado.$2, closeTo(normal.$2, 0.5), reason: 'a altura nao muda');
  });

  test('escala por eixo vai e volta do arquivo, e so entra quando ha', () {
    // O NO PARADO NAO ENGORDA O ARQUIVO: tres trilhas a mais por no, em
    // cenas com centenas deles, so para dizer "1".
    final parado = SceneNode(name: 'Cubo');
    final jsonParado =
        (projectToJson(_projeto(_camada([parado])))['layers'] as List).single
            as Map<String, dynamic>;
    final noParado =
        ((jsonParado['scene'] as Map)['nodes'] as List).single as Map;
    expect(noParado.containsKey('sx'), isFalse);
    expect(noParado.containsKey('sy'), isFalse);
    expect(noParado.containsKey('sz'), isFalse);

    // E um projeto ANTIGO (sem os campos) volta com os tres em 1 — a
    // leitura tolerante do QA 1.0.
    final voltaParado = projectFromJson(
      projectToJson(_projeto(_camada([parado]))),
    );
    final noVolta =
        (voltaParado.layers.single as Scene3DLayer).scene.nodes.single;
    expect(noVolta.scaleX.valueAt(_t0), 1);
    expect(noVolta.scaleY.valueAt(_t0), 1);
    expect(noVolta.scaleZ.valueAt(_t0), 1);

    // Mexido, vai e volta com keyframe e tudo.
    final esticado = SceneNode(
      name: 'Cubo',
      scaleX: AnimatedDouble(2.5),
      scaleY: AnimatedDouble(1, [
        Keyframe<double>(time: const Duration(seconds: 1), value: 3),
      ]),
    );
    final volta = projectFromJson(
      projectToJson(_projeto(_camada([esticado]))),
    );
    final no = (volta.layers.single as Scene3DLayer).scene.nodes.single;
    expect(no.scaleX.valueAt(_t0), 2.5);
    expect(no.scaleY.valueAt(const Duration(seconds: 1)), 3);
    expect(no.scaleZ.valueAt(_t0), 1);
  });

  test('o pintor desenha NA ORIGEM, e nao em origem x escala', () {
    // A ESCALA DUPLA ERA O "GIZMO FIXO NO CANTO": o pintor multiplicava
    // por `escala` dentro de um canvas que o `FittedBox` ja escala. Com
    // escala 0,5 e origem em (100,100), o gizmo saia desenhado em (50,50)
    // — longe do objeto, e o eixo que o dedo pegava ficava invisivel.
    TestWidgetsFlutterBinding.ensureInitialized();
    const gizmo = GizmoNaTela(
      origem: Offset(100, 100),
      x: Offset(1, 0),
      y: Offset(0, -1),
      z: Offset.zero,
      escala: 1,
    );
    final espiao = _CanvasEspiao();
    const Gizmo3DPainter(
      gizmo: gizmo,
      escala: 0.5,
      comprimento: 40,
      raio: 60,
    ).paint(espiao, const Size(200, 200));

    // O PONTO CENTRAL sai EXATAMENTE no pivo, e nao em (50,50).
    expect(espiao.circulos.map((c) => c.$1), contains(const Offset(100, 100)));
    expect(espiao.circulos.map((c) => c.$1), isNot(contains(const Offset(50, 50))));
    // E o raio dele cresce com o zoom do palco: 4,5 px de TELA valem 9 px
    // de composicao quando o palco esta a meio tamanho.
    expect(espiao.circulos.first.$2, closeTo(9, 1e-9));
    // Todo braco parte do pivo, e o de X acaba a 80 px (40 de tela / 0,5).
    expect(espiao.linhas.every((l) => l.$1 == const Offset(100, 100)), isTrue);
    expect(espiao.linhas.map((l) => l.$2), contains(const Offset(180, 100)));
  });
}

/// UM CANVAS QUE SO ANOTA. Rasterizar num teste pede o motor de verdade
/// (`Picture.toImage` nunca completa sob o relogio falso do `flutter_test`);
/// contar chamadas responde a mesma pergunta — ONDE o pintor pos a tinta.
class _CanvasEspiao implements Canvas {
  final List<(Offset, double)> circulos = [];
  final List<(Offset, Offset)> linhas = [];

  @override
  void drawCircle(Offset c, double raio, Paint paint) =>
      circulos.add((c, raio));

  @override
  void drawLine(Offset de, Offset para, Paint paint) => linhas.add((de, para));

  @override
  dynamic noSuchMethod(Invocation invocation) => null;
}
