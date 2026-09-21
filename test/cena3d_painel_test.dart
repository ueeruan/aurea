// O PAINEL "3D" E O GIZMO DO OBJETO — a ficha da cena refeita com os
// componentes do Aurea (pedido do dono: "use os proprios componentes do
// Aurea, nada de plugin externo").
//
// O QUE ESTE TESTE ANCORA, e por que cada ancora existe:
//
//   * a ficha e feita de [ParameterRow] — a linha do app, com a regua, o
//     campo de valor e o LOSANGO de keyframe. Se alguem trocar por um
//     slider proprio, este teste cai;
//   * os cartoes sao os do painel de Efeitos (abre/fecha pelo cabecalho),
//     e Transformar traz posicao, rotacao e escala (uniforme + por eixo);
//   * o gizmo aparece sobre o objeto quando a camada de cena esta
//     selecionada e SOME quando ela nao esta — sem viewport proprio: a
//     camada de gizmo vive dentro do palco.

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/presentation/am/cena3d_sheet.dart';
import 'package:aurea/src/features/editor/presentation/context/parameter_row.dart';
import 'package:aurea/src/features/editor/presentation/widgets/gizmo_da_cena_overlay.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

ProviderContainer _container() {
  final c = ProviderContainer();
  addTearDown(c.dispose);
  return c;
}

/// Uma camada de cena com um cubo, ja selecionada.
(ProviderContainer, String, String) _cenaComCubo() {
  final c = _container();
  final e = c.read(editorControllerProvider.notifier);
  e.addScene3DLayer(Duration.zero);
  final id = c.read(editorControllerProvider).layers.single.id;
  e.addSceneNode(id, Element3DKind.cube);
  c.read(selectedLayerProvider.notifier).state = id;
  final no =
      (c.read(editorControllerProvider).layerById(id)! as Scene3DLayer)
          .scene
          .nodes
          .single;
  return (c, id, no.id);
}

Future<void> _abrirPainel(
  WidgetTester tester,
  ProviderContainer c,
  String sceneId,
) async {
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        home: Scaffold(
          body: Consumer(
            builder: (context, ref, _) => TextButton(
              onPressed: () => showCena3DSheet(context, ref, sceneId: sceneId),
              child: const Text('abrir'),
            ),
          ),
        ),
      ),
    ),
  );
  await tester.tap(find.text('abrir'));
  await tester.pumpAndSettle();
}

void main() {
  testWidgets('a ficha 3D e feita de ParameterRow, e nao de widget proprio', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(400, 1000);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final (c, sceneId, _) = _cenaComCubo();

    await _abrirPainel(tester, c, sceneId);

    expect(find.byKey(const ValueKey('cena3d-painel')), findsOneWidget);
    // A LINHA DO APP, e nao um controle inventado.
    expect(find.byType(ParameterRow), findsWidgets);
    // E o losango vem da propria linha ([KeyframeState] em ParameterFrame).
    expect(find.byKey(const ValueKey('kf-x')), findsWidgets);

    // Os nove numeros de Transformar: posicao, rotacao e escala.
    for (final p in [
      PropDoNo.x,
      PropDoNo.y,
      PropDoNo.z,
      PropDoNo.giroX,
      PropDoNo.giroY,
      PropDoNo.giroZ,
      PropDoNo.escala,
      PropDoNo.escalaX,
      PropDoNo.escalaY,
      PropDoNo.escalaZ,
    ]) {
      expect(
        find.byKey(ValueKey('cena3d-linha-${p.name}')),
        findsOneWidget,
        reason: 'falta a linha de ${p.name}',
      );
    }
  });

  testWidgets('os cartoes abrem e fecham pelo cabecalho, como em Efeitos', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(400, 1000);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final (c, sceneId, _) = _cenaComCubo();

    await _abrirPainel(tester, c, sceneId);

    // Transformar nasce ABERTO (e o que se procura primeiro); tocar o
    // cabecalho recolhe, e o corpo nem e construido.
    expect(find.byKey(const ValueKey('cena3d-linha-x')), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('cena3d-cabecalho-transformar')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('cena3d-linha-x')), findsNothing);

    // Os seis assuntos do recado do dono. Conferidos com Transformar
    // RECOLHIDO: a lista e preguicosa, e o cartao aberto sozinho ja passa
    // da altura da folha (armadilha conhecida deste projeto).
    for (final nome in [
      'transformar',
      'material',
      'iluminacao',
      'ambiente',
      'animacao',
      'propriedades',
    ]) {
      expect(
        find.byKey(ValueKey('cena3d-cartao-$nome')),
        findsOneWidget,
        reason: 'falta o cartao $nome',
      );
    }

    // Material nasce fechado e abre com um toque.
    expect(find.byKey(const ValueKey('cena3d-cor-base')), findsNothing);
    await tester.tap(find.byKey(const ValueKey('cena3d-cabecalho-material')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('cena3d-cor-base')), findsOneWidget);
  });

  testWidgets('num telefone de 375 px nenhum cartao estoura a linha', (
    tester,
  ) async {
    // O SUSTO CONHECIDO deste projeto: um Wrap de fichas espremido ao
    // lado de um rotulo estoura a linha no telefone pequeno — e o erro so
    // aparece quando aquele cartao abre.
    tester.view.physicalSize = const Size(375, 667);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final (c, sceneId, _) = _cenaComCubo();
    c.read(editorControllerProvider.notifier)
      ..addSceneNode(sceneId, Element3DKind.sphere)
      ..addSceneLight(sceneId, Light3DKind.spot);

    await _abrirPainel(tester, c, sceneId);
    expect(tester.takeException(), isNull);

    await tester.tap(find.byKey(const ValueKey('cena3d-cabecalho-transformar')));
    await tester.pumpAndSettle();

    final lista = find.descendant(
      of: find.byKey(const ValueKey('cena3d-painel')),
      matching: find.byType(Scrollable),
    );
    Future<void> tocarCabecalho(String nome) async {
      final alvo = find.byKey(ValueKey('cena3d-cabecalho-$nome'));
      // A LISTA E PREGUICOSA: um cartao fora da vista nem existe na
      // arvore, entao procurar sem rolar acha zero.
      await tester.scrollUntilVisible(alvo, 120, scrollable: lista.first);
      await tester.pumpAndSettle();
      await tester.tap(alvo);
      await tester.pumpAndSettle();
    }

    for (final nome in [
      'material',
      'iluminacao',
      'ambiente',
      'animacao',
      'propriedades',
    ]) {
      await tocarCabecalho(nome);
      expect(tester.takeException(), isNull, reason: 'o cartao $nome estourou');
      await tocarCabecalho(nome);
      expect(tester.takeException(), isNull);
    }
  });

  testWidgets('o losango da ficha crava keyframe na trilha do objeto', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(400, 1000);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final (c, sceneId, nodeId) = _cenaComCubo();

    await _abrirPainel(tester, c, sceneId);

    AnimatedDouble trilhaX() =>
        (c.read(editorControllerProvider).layerById(sceneId)! as Scene3DLayer)
            .scene
            .nodeById(nodeId)!
            .x;

    expect(trilhaX().keyframes, isEmpty);
    // O losango da linha "X" de posicao: a chave sai do rotulo, como em
    // todo ParameterFrame do app.
    await tester.tap(find.byKey(const ValueKey('kf-x')).first);
    await tester.pumpAndSettle();
    expect(trilhaX().keyframes, hasLength(1));
  });

  testWidgets('o gizmo aparece sobre o objeto e some ao desselecionar', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(600, 600);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final (c, sceneId, _) = _cenaComCubo();
    final tempo = ValueNotifier<Duration>(Duration.zero);
    addTearDown(tempo.dispose);

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          home: Scaffold(
            body: Stack(
              children: [
                const SizedBox.expand(),
                GizmoDaCenaOverlay(tempo: tempo, escala: 1),
              ],
            ),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();

    // NAO HA VIEWPORT NOVO: o gizmo e uma camada de pintura dentro do
    // palco — nao ha segunda previa nem moldura branca.
    expect(find.byKey(const ValueKey('gizmo-da-cena')), findsOneWidget);
    // As fichas da ferramenta na mao.
    for (final m in ModoDoGizmo3D.values) {
      expect(find.byKey(ValueKey('gizmo-modo-${m.name}')), findsOneWidget);
    }

    // Desselecionar a camada apaga o gizmo inteiro.
    c.read(selectedLayerProvider.notifier).state = null;
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('gizmo-da-cena')), findsNothing);
    expect(find.byKey(const ValueKey('gizmo-modo-mover')), findsNothing);
    expect(tester.takeException(), isNull);
  });

  testWidgets('palco e ficha juntos: o cabecote anda sem derrubar a arvore', (
    tester,
  ) async {
    // A FICHA ESCUTA O CABECOTE QUE O PALCO PUBLICA. Se o palco publicasse
    // dentro do proprio build, a ficha seria marcada suja no meio da
    // construcao da arvore e o editor cairia com "setState() called during
    // build" — este teste e o que segura essa regra.
    tester.view.physicalSize = const Size(420, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final (c, sceneId, nodeId) = _cenaComCubo();
    final tempo = ValueNotifier<Duration>(Duration.zero);
    addTearDown(tempo.dispose);
    addTearDown(() => cabecoteDoPalco.value = Duration.zero);

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          home: Scaffold(
            body: Stack(
              children: [
                const SizedBox.expand(),
                GizmoDaCenaOverlay(tempo: tempo, escala: 1),
                // O BOTAO POR CIMA do gizmo: a camada do gizmo cobre o
                // palco inteiro e so recusa o dedo FORA das alcas — num
                // palco de teste as alcas caem no meio da tela.
                Align(
                  alignment: Alignment.topLeft,
                  child: Consumer(
                    builder: (context, ref, _) => TextButton(
                      onPressed: () =>
                          showCena3DSheet(context, ref, sceneId: sceneId),
                      child: const Text('abrir'),
                    ),
                  ),
                ),
              ],
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.text('abrir'));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('cena3d-painel')), findsOneWidget);

    tempo.value = const Duration(milliseconds: 500);
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
    expect(cabecoteDoPalco.value, const Duration(milliseconds: 500));

    // E o losango crava NO INSTANTE DO PALCO, e nao no zero.
    await tester.tap(find.byKey(const ValueKey('kf-x')).first);
    await tester.pumpAndSettle();
    final trilha =
        (c.read(editorControllerProvider).layerById(sceneId)! as Scene3DLayer)
            .scene
            .nodeById(nodeId)!
            .x;
    expect(trilha.keyframes.single.time, const Duration(milliseconds: 500));
  });

  testWidgets('escolher a ferramenta Escalar troca o que o gizmo oferece', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(600, 600);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final (c, _, _) = _cenaComCubo();
    final tempo = ValueNotifier<Duration>(Duration.zero);
    addTearDown(tempo.dispose);

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          home: Scaffold(
            body: Stack(
              children: [
                const SizedBox.expand(),
                GizmoDaCenaOverlay(tempo: tempo, escala: 1),
              ],
            ),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();

    expect(c.read(modoDoGizmo3DProvider), ModoDoGizmo3D.mover);
    await tester.tap(find.byKey(const ValueKey('gizmo-modo-escalar')));
    await tester.pumpAndSettle();
    expect(c.read(modoDoGizmo3DProvider), ModoDoGizmo3D.escalar);
    expect(tester.takeException(), isNull);
  });
}
