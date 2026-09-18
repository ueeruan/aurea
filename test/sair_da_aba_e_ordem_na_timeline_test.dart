import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/presentation/am/am_timeline.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

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
    expect(find.byTooltip('Girar'), findsOneWidget, reason: 'o painel abriu');

    final voltar = find.byKey(const ValueKey('painel-voltar'));
    expect(voltar, findsOneWidget);
    await tester.tap(voltar);
    await tester.pumpAndSettle();
    expect(find.byTooltip('Girar'), findsNothing, reason: 'o painel fechou');
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
      expect(find.byTooltip('Girar'), findsOneWidget);

      // A barra fica no comeco da linha (a camada comeca em 0 s).
      final linha = tester.getRect(find.byKey(ValueKey(id)));
      await tester.tapAt(Offset(linha.left + 30, linha.top + kAmBarHeight / 2));
      await tester.pumpAndSettle();
      expect(
        find.byTooltip('Girar'),
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

    final linha = tester.getRect(find.byKey(ValueKey(deBaixo)));
    final pegada = Offset(linha.left + 30, linha.top + kAmBarHeight / 2);
    // UMA LINHA E MEIA para cima, devagar (varios passos, como um dedo).
    //
    // Uma linha exata nao basta e nunca bastou: o reconhecedor de arrasto
    // so entra depois da folga de 18 px, e o degrau da pilha so troca
    // depois de meia linha ACEITA. Um gesto de 38 px fica na divisa, e o
    // teste passava ou nao conforme o arredondamento do layout. Quem
    // arrasta com o dedo anda bem mais que isso.
    final dedo = await tester.startGesture(pegada);
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
    final linhaNova = tester.getRect(find.byKey(ValueKey(deBaixo)));
    final dedo2 = await tester.startGesture(
      Offset(linhaNova.left + 30, linhaNova.top + kAmBarHeight / 2),
    );
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
    await tester.tap(find.text('Mesclar e\nopacidade'));
    await tester.pumpAndSettle();
    // A aba de modos de mescla.
    await tester.tap(find.text('Mesclagem'));
    await tester.pumpAndSettle();
    expect(find.textContaining('por baixo desta'), findsOneWidget);
  });
}
