// O GIRO DA CAMADA DE CENA E GIRO DE VERDADE.
//
// Relato do dono, com print: um "Texto 3D" girado em -64 graus em Y
// continuava parecendo texto chapado — "um fake 3D". Ele estava certo, e
// eram DOIS defeitos no mesmo lugar:
//
//   1. o giro em X/Y da camada era aplicado como PERSPECTIVA DE CARTAO
//      sobre a imagem pronta. O motor nunca via o giro: girar 60 graus em
//      Y devolvia o MESMO quadro, com a letra chapada, so esticada;
//   2. a camera do MOTOR ficava parada, entao nao havia como o objeto
//      aparecer de lado — a extrusao existia na malha e nao chegava a tela.
//
// Agora o giro da camada ORBITA a camera em volta do alvo (o mesmo caminho
// que o nulo da composicao ja usava) e a imagem nao e mais inclinada como
// um cartao.

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/selection_geometry.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart'
    show CompositionView, Estado3DDoQuadro, estado3DDoQuadro;
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

Scene3DLayer _cena({double rotX = 0, double rotY = 0}) => Scene3DLayer(
  id: 'cena',
  name: 'Texto 3D',
  startTime: Duration.zero,
  duration: const Duration(seconds: 5),
  is3D: true,
  rotationX: AnimatedDouble(rotX),
  rotationY: AnimatedDouble(rotY),
  scene: Scene3D(nodes: [SceneNode(name: 'TEXTO', kind: Element3DKind.cube)]),
);

VideoProject _projeto(List<Layer> camadas) => VideoProject(
  name: 'giro',
  createdAt: DateTime(2026, 9, 20),
  aspectRatio: 1,
  resolutionHeight: 600,
  layers: camadas,
);

Estado3DDoQuadro _estado(Scene3DLayer l) => estado3DDoQuadro(
  project: _projeto([l]),
  l: l,
  local: Duration.zero,
  global: Duration.zero,
  largura: 600,
  altura: 600,
);

void main() {
  test('o giro em X/Y da camada chega ao motor', () {
    final parado = _estado(_cena());
    final girada = _estado(_cena(rotX: 30, rotY: 60));
    expect(
      girada.chave,
      isNot(parado.chave),
      reason: 'o giro nao chegou ao motor — o quadro desenhado seria o mesmo',
    );
  });

  test('o giro orbita a camera em volta do alvo, sem mexer no enquadramento', () {
    final parado = _estado(_cena()).camera!;
    final girada = _estado(_cena(rotX: 30, rotY: 60)).camera!;
    double raio(RenderCamera c) => (c.position - c.target).length;

    // ORBITA, e nao deslocamento: a distancia ao alvo nao muda e o alvo
    // fica onde estava — o objeto nao escorrega para fora do quadro.
    expect(raio(girada), closeTo(raio(parado), 1e-6));
    expect(girada.target.x, closeTo(parado.target.x, 1e-9));
    expect(girada.target.y, closeTo(parado.target.y, 1e-9));
    expect(girada.target.z, closeTo(parado.target.z, 1e-9));

    // +60 em Y na composicao leva a camera para -X da cena, e +30 em X
    // leva para +Y (a cena e Y para cima) — os lados que o cartao mostrava.
    expect(girada.position.x, lessThan(parado.position.x - 1));
    expect(girada.position.y, greaterThan(parado.position.y + 1));
    expect(girada.position.z, isNot(closeTo(parado.position.z, 1)));
  });

  test('a camera parada continua sendo a camera autoral', () {
    // Zero grau nao pode custar nada: sem giro, a camera sai identica.
    final parado = _estado(_cena()).camera!;
    final base = _cena().cameraAt(Duration.zero);
    expect(parado.position.x, closeTo(base.position.x, 1e-9));
    expect(parado.position.z, closeTo(base.position.z, 1e-9));
    expect(parado.target.z, closeTo(base.target.z, 1e-9));
  });

  /// A LARGURA DA MOLDURA DE SELECAO projetada: com o cartao inclinado em
  /// Y ela encurta (e' o cosseno do giro); sem inclinacao, nao muda.
  double larguraDaMoldura(Matrix4 m) =>
      (MatrixUtils.transformPoint(m, const Offset(150, 0)) -
              MatrixUtils.transformPoint(m, const Offset(-150, 0)))
          .distance;

  test('a moldura de selecao da cena acompanha a imagem, sem inclinar', () {
    final p = _projeto([_cena()]);
    final reta = larguraDaMoldura(
      selectionTransform(p, _cena(), Duration.zero),
    );
    final comGiro = larguraDaMoldura(
      selectionTransform(p, _cena(rotY: 60), Duration.zero),
    );
    expect(comGiro, closeTo(reta, 1e-6), reason: 'a moldura saiu inclinada');

    // O CONTROLE: uma camada de texto, que e um retangulo de verdade,
    // continua inclinando — senao a medida acima nao valeria nada.
    final texto = TextLayer(
      id: 'texto',
      name: 'Texto',
      text: 'TEXTO',
      startTime: Duration.zero,
      duration: const Duration(seconds: 5),
      is3D: true,
      rotationY: AnimatedDouble(60),
    );
    expect(
      larguraDaMoldura(
        selectionTransform(_projeto([texto]), texto, Duration.zero),
      ),
      lessThan(reta * 0.8),
    );
  });

  /// AS PERSPECTIVAS DE CARTAO DA ARVORE: quantas matrizes tem a entrada
  /// de profundidade (linha 3, coluna 2) ligada. E ela que inclina a
  /// imagem pronta como um cartao.
  int cartoesNaTela(WidgetTester tester) {
    var n = 0;
    for (final t in tester.widgetList<Transform>(find.byType(Transform))) {
      if (t.transform.entry(3, 2) != 0) n++;
    }
    return n;
  }

  Future<int> cartoes(WidgetTester tester, List<Layer> camadas) async {
    final container = ProviderContainer();
    addTearDown(container.dispose);
    container.read(editorControllerProvider.notifier).openProject(
      _projeto(camadas),
    );
    final time = ValueNotifier(Duration.zero);
    addTearDown(time.dispose);
    final videos = VideoLayerManager();
    addTearDown(videos.dispose);
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          home: Center(
            child: SizedBox(
              width: 300,
              height: 300,
              child: CompositionView(
                time: time,
                videos: videos,
                selectedId: null,
                exporting: false,
              ),
            ),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();
    return cartoesNaTela(tester);
  }

  testWidgets('a camada de cena nao inclina a imagem como um cartao', (
    tester,
  ) async {
    // A imagem da cena ja tem a profundidade DENTRO dela: inclina-la de
    // novo e o "fake 3D". Uma camada de texto, que e um retangulo de
    // verdade, continua inclinando — o controle que da sentido a medida.
    expect(await cartoes(tester, [_cena(rotX: 30, rotY: 60)]), 0);
    expect(
      await cartoes(tester, [
        TextLayer(
          id: 'texto',
          name: 'Texto',
          text: 'TEXTO',
          startTime: Duration.zero,
          duration: const Duration(seconds: 5),
          is3D: true,
          rotationY: AnimatedDouble(60),
        ),
      ]),
      greaterThan(0),
      reason: 'o controle nao inclinou: a medida nao vale',
    );
    expect(tester.takeException(), isNull);
  });
}
