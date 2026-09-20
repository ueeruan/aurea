// A FOLHA DO TEXTO 3D: o texto deixa de ser tres perguntas e vira um objeto
// que se edita — na metade de baixo da tela, com o palco a vista por cima.
//
// O QUE ESTES TESTES PRENDEM, e que era o buraco antes da folha:
//
//   * trocar o METAL nao pode jogar fora a camada — a posicao, o giro e os
//     keyframes continuam, e so o material muda;
//   * trocar o TEXTO refaz a geometria e mantem o metal escolhido;
//   * a ESPESSURA acompanha o dedo no rotulo enquanto o dedo arrasta (o
//     controle ficava parado ate soltar) e refaz a letra no fim;
//   * NAO HA PREVIA INTERNA: o unico preview e o palco (a previa propria
//     alternava o alvo do motor com o palco e recriava os alvos duas vezes
//     por toque), e a folha nasce com `Material` — sem ele os textos saiam
//     com o sublinhado amarelo do "texto sem estilo".
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/modelo_do_texto3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/texto3d_sheet.dart';
import 'package:aurea/src/features/editor/presentation/context/parameter_row.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  late ProviderContainer container;
  late String cenaId;
  late String noId;

  Scene3DLayer cena() =>
      container.read(editorControllerProvider).layers.whereType<Scene3DLayer>().single;

  SceneNode no() => cena().scene.nodeById(noId)!;

  Future<void> abrir(WidgetTester tester) async {
    tester.view.physicalSize = const Size(600, 1600);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    container = ProviderContainer();
    addTearDown(container.dispose);
    final controller = container.read(editorControllerProvider.notifier);
    controller.openProject(VideoProject.empty('texto 3d'));
    noId = (await controller.addTexto3D(
      Duration.zero,
      'AUREA',
      EstiloDoTexto3D.ouro,
      familia: 'fonte que nao existe',
    ))!;
    cenaId = cena().id;

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          home: Scaffold(
            body: Consumer(
              builder: (context, ref, _) => TextButton(
                onPressed: () => showTexto3DSheet(
                  context,
                  ref,
                  sceneId: cenaId,
                  nodeId: noId,
                  playhead: const Duration(seconds: 1),
                ),
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

  testWidgets('abre com o texto da camada e os quatro metais', (tester) async {
    await abrir(tester);
    final campo = tester.widget<TextField>(
      find.byKey(const ValueKey('texto3d-campo')),
    );
    expect(campo.controller!.text, 'AUREA');
    for (final e in EstiloDoTexto3D.values) {
      expect(
        find.byKey(ValueKey('texto3d-estilo-${e.name}')),
        findsOneWidget,
        reason: 'o metal ${e.name} tem de estar na folha',
      );
    }
    expect(find.byKey(const ValueKey('texto3d-importar-fonte')), findsOneWidget);
  });

  testWidgets('sem previa interna, na metade de baixo, com Material', (tester) async {
    await abrir(tester);
    // UM PREVIEW SO: o palco. Nada de imagem do motor dentro da folha, nem
    // do aviso que a previa antiga mostrava no PC.
    expect(find.byType(RawImage), findsNothing);
    expect(find.textContaining('aparece no aparelho'), findsNothing);

    // A METADE DE BAIXO: a folha nao passa de 52% da tela.
    final folha = find.byKey(const ValueKey('texto3d-folha'));
    expect(folha, findsOneWidget);
    final altura = tester.getSize(folha).height;
    // ignore: avoid_print
    print('ALTURA DA FOLHA: $altura de 1600');
    expect(altura, lessThanOrEqualTo(1600 * 0.52 + 0.5));
    expect(altura, greaterThan(200));

    // O `Material` acima dos textos: sem ele, sublinhado amarelo.
    expect(
      find.ancestor(
        of: find.byKey(const ValueKey('texto3d-campo')),
        matching: find.byType(Material),
      ),
      findsWidgets,
    );
    // OS CONTROLES SAO OS DA CASA, e nao sliders do Material.
    expect(find.byType(Slider), findsNothing);
    expect(find.byType(ParameterRow), findsWidgets);
  });

  testWidgets('trocar o metal troca so o material e mantem a malha', (tester) async {
    await abrir(tester);
    final antes = no();
    expect(antes.estiloTexto3d, EstiloDoTexto3D.ouro);
    final malhaAntes = antes.modelAsset;
    final giroAntes = antes.rotY.valueAt(Duration.zero);

    await tester.tap(
      find.byKey(const ValueKey('texto3d-estilo-cromo')),
    );
    await tester.pumpAndSettle();

    final depois = no();
    expect(depois.estiloTexto3d, EstiloDoTexto3D.cromo);
    expect(depois.texto3d!.texto, 'AUREA', reason: 'o texto nao pode se perder');
    expect(
      depois.rotY.valueAt(Duration.zero),
      giroAntes,
      reason: 'o giro da camada continua o que o dono ajustou',
    );
    expect(
      depois.modelAsset,
      isNot(equals(malhaAntes)),
      reason: 'o metal novo entra numa malha refeita',
    );
  });

  testWidgets('editar o texto refaz a geometria e guarda o metal', (tester) async {
    await abrir(tester);
    await tester.tap(find.byKey(const ValueKey('texto3d-estilo-cromo')));
    await tester.pumpAndSettle();

    await tester.enterText(
      find.byKey(const ValueKey('texto3d-campo')),
      'METAL',
    );
    await tester.testTextInput.receiveAction(TextInputAction.done);
    await tester.pumpAndSettle();

    final n = no();
    expect(n.texto3d!.texto, 'METAL');
    expect(
      n.estiloTexto3d,
      EstiloDoTexto3D.cromo,
      reason: 'trocar a palavra nao pode voltar o metal para o padrao',
    );
  });

  testWidgets('a espessura anda no rotulo durante o arrasto', (tester) async {
    await abrir(tester);
    final inicio = no().texto3d!.espessura;
    final controle = find.byKey(const ValueKey('texto3d-controle-Espessura'));
    await tester.ensureVisible(controle);
    await tester.pumpAndSettle();
    final linha = tester.widget<ParameterRow>(controle);
    expect(linha.value, inicio);

    // A LINHA INTEIRA E A REGUA: arrastar na horizontal puxa o valor.
    final meio = tester.getCenter(controle);
    final gesto = await tester.startGesture(meio);
    await gesto.moveBy(const Offset(90, 0));
    await tester.pump();
    // AINDA COM O DEDO NA TELA: o numero ja tem de ter mudado. Era aqui que
    // o controle parecia morto — nada mudava ate soltar.
    expect(
      tester.widget<ParameterRow>(controle).value,
      isNot(inicio),
      reason: 'o rotulo ficou parado com o dedo na tela',
    );
    await gesto.up();
    await tester.pumpAndSettle();
    expect(
      no().texto3d!.espessura,
      isNot(inicio),
      reason: 'soltar o dedo tem de refazer a letra',
    );
  });
}
