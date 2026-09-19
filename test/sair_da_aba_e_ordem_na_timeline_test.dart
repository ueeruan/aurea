import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/presentation/am/am_timeline.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_audit_helpers.dart';
import 'editor_hierarchy_test.dart' show openEditor;

/// OS PEDIDOS DO BETA SOBRE A TIMELINE E AS ABAS:
///
/// - "um botao de voltar proprio": todo painel tem um Voltar no cabecalho;
/// - "clicar na timeline fecha essa e qualquer outra aba": tocar na
///   barra da camada com um painel aberto volta as ferramentas, e tocar
///   no vazio da timeline tira a selecao (fecha tudo);
/// - "nao da pra trocar a camada pra cima ou pra baixo na timeline":
///   arrastar a barra selecionada para cima ou para baixo troca a ordem.
void main() {
  setUpAll(() async {
    for (final family in ['Aurea Motion Sans', 'Roboto']) {
      await (FontLoader(family)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });

  testWidgets('o painel tem um Voltar com alvo de toque, e ele volta', (
    tester,
  ) async {
    final c = await openEditor(tester);
    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();
    await tester.tap(find.text('Movimentação e transformação'));
    await tester.pumpAndSettle();
    expect(abaDoTrilho('Girar'), findsOneWidget, reason: 'o painel abriu');

    // DOIS VOLTARES NO PAINEL, e os dois sao de verdade: o do
    // CABECALHO da zona E (a casca do painel) fecha a ferramenta
    // inteira, e o do TRILHO esquerdo volta um nivel, para a grade de
    // categorias. Sao duas perguntas diferentes. Este caso mede o do
    // cabecalho — o primeiro na arvore.
    final voltar = find.byKey(const ValueKey('painel-voltar')).first;
    expect(voltar, findsWidgets);
    await tester.tap(voltar);
    await tester.pumpAndSettle();
    expect(abaDoTrilho('Girar'), findsNothing, reason: 'o painel fechou');
    expect(
      find.text('Movimentação e transformação'),
      findsOneWidget,
      reason: 'as ferramentas voltaram',
    );
  });

  testWidgets(
    'tocar na barra da camada com um painel aberto volta as ferramentas',
    (tester) async {
      final c = await openEditor(tester);
      final id = c.read(editorControllerProvider).layers.first.id;
      c.read(selectedLayerProvider.notifier).state = id;
      await tester.pumpAndSettle();
      await tester.tap(find.text('Movimentação e transformação'));
      await tester.pumpAndSettle();
      expect(abaDoTrilho('Girar'), findsOneWidget);

      // A BARRA E O PROPRIO ALVO, e nao um ponto adivinhado dentro da
      // linha: `ValueKey(id)` e a LINHA inteira (barra + faixa de
      // keyframes + o vazio ao lado), e um offset fixo a partir da
      // esquerda dela cai no vazio — o toque de la DESMARCA, que e o
      // caso do teste seguinte. A barra tem chave propria.
      final barra = find.byKey(ValueKey('clip-content-$id'));
      expect(barra, findsOneWidget);
      await tester.tap(barra);
      await tester.pumpAndSettle();
      expect(
        abaDoTrilho('Girar'),
        findsNothing,
        reason: 'o toque na barra fechou o painel',
      );
      expect(
        c.read(selectedLayerProvider),
        id,
        reason: 'a camada continua selecionada',
      );
      expect(find.text('Movimentação e transformação'), findsOneWidget);
    },
  );

  testWidgets(
    'tocar no vazio da timeline tira a selecao e fecha as ferramentas',
    (tester) async {
      final c = await openEditor(tester);
      final id = c.read(editorControllerProvider).layers.first.id;
      c.read(selectedLayerProvider.notifier).state = id;
      await tester.pumpAndSettle();
      expect(find.text('Movimentação e transformação'), findsOneWidget);

      // A faixa de baixo da linha, fora da barra (a barra tem 30 dos 38 px).
      final linha = tester.getRect(find.byKey(ValueKey(id)));
      await tester.tapAt(Offset(linha.left + 30, linha.top + kAmRowHeight - 3));
      await tester.pumpAndSettle();
      expect(
        c.read(selectedLayerProvider),
        isNull,
        reason: 'o vazio tirou a selecao',
      );
      expect(
        find.text('Movimentação e transformação'),
        findsNothing,
        reason: 'sem selecao, sem ferramentas',
      );
    },
  );

  testWidgets('arrastar a barra selecionada para cima sobe a camada na pilha', (
    tester,
  ) async {
    final c = await openEditor(tester);
    final camadas = c.read(editorControllerProvider).layers;
    expect(camadas.length, greaterThanOrEqualTo(2));
    final deBaixo = camadas[1].id;
    c.read(selectedLayerProvider.notifier).state = deBaixo;
    await tester.pumpAndSettle();

    // O GESTO E TOQUE LONGO + ARRASTAR, e nao um arrasto simples: o
    // degrau da pilha mora no `onLongPressMoveUpdate` da barra. Enquanto
    // o teste so arrastava, ele media um gesto que o aplicativo nao
    // atende — e o `kAmBarHeight` do papel nao valia nada, porque a
    // pegada caia fora da barra.
    final dedo = await tester.startGesture(
      tester.getCenter(find.byKey(ValueKey('clip-content-$deBaixo'))),
    );
    // O RECONHECEDOR DE TOQUE LONGO precisa do tempo parado antes do
    // primeiro pixel: `longPressTimeout` e 500 ms.
    await tester.pump(const Duration(milliseconds: 600));
    for (var i = 1; i <= 9; i++) {
      await dedo.moveBy(const Offset(0, -kAmRowHeight / 6));
      await tester.pump(const Duration(milliseconds: 16));
    }
    await dedo.up();
    await tester.pumpAndSettle();
    expect(
      c.read(editorControllerProvider).layers.first.id,
      deBaixo,
      reason: 'a camada de baixo virou a de cima',
    );

    // E para baixo, de volta.
    final dedo2 = await tester.startGesture(
      tester.getCenter(find.byKey(ValueKey('clip-content-$deBaixo'))),
    );
    await tester.pump(const Duration(milliseconds: 600));
    for (var i = 1; i <= 9; i++) {
      await dedo2.moveBy(const Offset(0, kAmRowHeight / 6));
      await tester.pump(const Duration(milliseconds: 16));
    }
    await dedo2.up();
    await tester.pumpAndSettle();
    expect(c.read(editorControllerProvider).layers[1].id, deBaixo);
  });

  testWidgets('o painel de mescla diz com o que a mescla conta', (
    tester,
  ) async {
    final c = await openEditor(tester);
    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();
    await tester.tap(find.text('Mistura e opacidade'));
    await tester.pumpAndSettle();
    // A aba de modos de mescla.
    await tester.tap(find.text('Mesclagem'));
    await tester.pumpAndSettle();
    // A NOTA E A ULTIMA LINHA DA LISTA, e a lista e preguicosa: ela so
    // existe depois de rolada ate o fim. Procurar sem rolar mediria a
    // altura do painel, e nao a presenca do aviso.
    await tester.dragUntilVisible(
      find.textContaining('camadas abaixo'),
      find.byKey(const ValueKey('mescla-categorias')),
      const Offset(0, -80),
    );
    await tester.pumpAndSettle();
    expect(find.textContaining('camadas abaixo'), findsOneWidget);
  });
}
