import 'editor_audit_helpers.dart';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/presentation/am/layer_menu.dart'
    show categoriasDeMescla;
import 'package:aurea/src/features/editor/presentation/context/parameter_row.dart';
import 'package:aurea/src/features/editor/presentation/widgets/campo_de_valor.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_hierarchy_test.dart' show openEditor;

/// FASE 3 DO REDESIGN — O PAINEL CONTEXTUAL (docs/UI_REDESIGN_PLAN.md, 3.3).
///
/// - `ParameterRow`: [◆][nome][regua][valor tocavel]; tocar o numero abre
///   o teclado e aceita "1080/3" e "50%".
/// - Transformar, Mesclagem e Texto em linhas de parametro; opacidade com
///   valor exato em 4 toques (criterio de aceite).
/// - Mesclagem: 6 modos com miniatura no Simples, 27 no Pro.
/// - Todo painel tem um Voltar no cabecalho, com alvo de toque.
void main() {
  setUpAll(() async {
    for (final family in ['Aurea Motion Sans', 'Roboto']) {
      await (FontLoader(family)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });

  Future<void> digitar(WidgetTester tester, String texto) async {
    final campo = find.byKey(const ValueKey('valor-campo'));
    expect(campo, findsOneWidget, reason: 'o teclado de valor abriu');
    await tester.enterText(campo, texto);
    await tester.tap(find.text('OK'));
    await tester.pumpAndSettle();
  }

  testWidgets(
    'ParameterRow: tocar o numero digita o valor, com conta e porcentagem',
    (tester) async {
      tester.view.physicalSize = const Size(430, 932);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);
      final recebidos = <double>[];
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: Column(
              children: [
                ParameterRow(
                  label: 'Opacidade',
                  value: 100,
                  min: 0,
                  max: 100,
                  unit: '%',
                  decimals: 0,
                  valueKey: const ValueKey('v1'),
                  onChanged: recebidos.add,
                ),
                ParameterRow(
                  label: 'Posicao X',
                  value: 10,
                  valueKey: const ValueKey('v2'),
                  onChanged: recebidos.add,
                ),
              ],
            ),
          ),
        ),
      );
      expect(find.text('100%'), findsOneWidget);
      await tester.tap(find.byKey(const ValueKey('v1')));
      await tester.pumpAndSettle();
      await digitar(tester, '50%');
      expect(recebidos, [50.0]);

      await tester.tap(find.byKey(const ValueKey('v2')));
      await tester.pumpAndSettle();
      await digitar(tester, '1080/3');
      expect(recebidos.last, 360.0);

      // Fora da faixa: preso ao limite.
      await tester.tap(find.byKey(const ValueKey('v1')));
      await tester.pumpAndSettle();
      await digitar(tester, '250');
      expect(recebidos.last, 100.0);
    },
  );

  testWidgets(
    'opacidade com valor exato: selecionar, Mesclagem, tocar o numero, digitar',
    (tester) async {
      final c = await openEditor(tester);
      final id = c.read(editorControllerProvider).layers.first.id;
      c.read(selectedLayerProvider.notifier).state = id; // 1
      await tester.pumpAndSettle();
      await tester.tap(find.text('Mistura e opacidade')); // 2
      await tester.pumpAndSettle();
      expect(find.text('Mesclagem e opacidade'), findsOneWidget);
      await tester.tap(find.byKey(const ValueKey('opacidade-valor'))); // 3
      await tester.pumpAndSettle();
      await digitar(tester, '40'); // 4
      final camada = c.read(editorControllerProvider).layerById(id)!;
      expect(camada.opacity.valueAt(Duration.zero), closeTo(0.4, 1e-6));
    },
  );

  testWidgets('Transformar: o trilho troca a face, e o campo crava o valor exato', (
    tester,
  ) async {
    // A OPACIDADE SAIU DESTE TESTE. Ela nao tem face no painel Transformar
    // desde o redesign: mora em "Mistura e opacidade", e o caso anterior
    // deste arquivo ja a cobre pelo caminho que existe. O que se testa
    // aqui e o que o painel Transformar faz hoje — as CINCO faces do
    // trilho direito, cada uma escrevendo a propriedade dela.
    final c = await openEditor(tester);
    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();
    await tester.tap(find.text('Movimentação e transformação'));
    await tester.pumpAndSettle();

    Future<void> face(String rotulo) async {
      await tester.tap(abaDoTrilho(rotulo));
      await tester.pumpAndSettle();
    }

    // O CAMPO SE ACHA PELO PROPRIO WIDGET, e nao pelo rotulo desenhado
    // embaixo dele: o rotulo e o irmao de baixo da caixa, e tocar nele
    // mediria a area da etiqueta, nao a do numero.
    Future<void> digitarCampo(String rotulo, String texto) async {
      final campo = find.byWidgetPredicate(
        (w) => w is CampoDeValor && w.rotulo == rotulo && w.aoDigitar != null,
      );
      expect(campo, findsOneWidget, reason: 'o campo "$rotulo" esta na face');
      await tester.tap(campo);
      await tester.pumpAndSettle();
      await digitarNoCampoDeValor(tester, texto);
    }

    // MOVER: os tres campos da face, pelos rotulos da fileira.
    await face('Mover');
    await digitarCampo('x', '120');
    expect(
      c.read(editorControllerProvider).layerById(id)!.position.valueAt(
        Duration.zero,
      ).dx,
      closeTo(120, 1e-6),
    );

    // ESCALAR: a corrente ligada leva os dois eixos junto.
    await face('Escalar');
    await digitarCampo('Largura', '150');
    final l = c.read(editorControllerProvider).layerById(id)!;
    expect(l.scaleX.valueAt(Duration.zero), closeTo(1.5, 1e-6));
    expect(
      l.scaleY.valueAt(Duration.zero),
      closeTo(1.5, 1e-6),
      reason: 'proporcao travada',
    );

    // O LOSANGO DO TRILHO ESQUERDO crava keyframe NA PROPRIEDADE DA FACE
    // aberta. Antes ele era procurado por um tooltip que so o corpo
    // antigo publicava; o trilho nomeia o botao por Semantics.
    await tester.tap(find.bySemanticsLabel('Marcar keyframe aqui'));
    await tester.pumpAndSettle();
    expect(
      c.read(editorControllerProvider).layerById(id)!.scaleX.isAnimated,
      isTrue,
    );

    // PIVO: a quinta face, com o pad de arrasto e o campo do zero.
    await face('Pivô');
    expect(find.byKey(const ValueKey('pivot-drag-pad')), findsOneWidget);
    await digitarCampo('x', '30');
    expect(
      c.read(editorControllerProvider).layerById(id)!.pivot.valueAt(
        Duration.zero,
      ).dx,
      closeTo(30, 1e-6),
    );
    await tester.tap(find.byKey(const ValueKey('pivot-centro')));
    await tester.pumpAndSettle();
    expect(
      c.read(editorControllerProvider).layerById(id)!.pivot.valueAt(
        Duration.zero,
      ),
      Offset.zero,
      reason: 'o botao devolve o ponto de giro ao centro',
    );
  });

  testWidgets('todo painel tem um Voltar no cabecalho, e ele volta', (
    tester,
  ) async {
    final c = await openEditor(tester);
    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();
    for (final tile in [
      'Movimentação e transformação',
      'Mistura e opacidade',
      'Cor e preenchimento',
      'Efeitos',
    ]) {
      await tester.tap(find.text(tile));
      await tester.pumpAndSettle();
      final voltar = find.byKey(const ValueKey('editor-back'));
      expect(voltar, findsOneWidget, reason: tile);
      // SO O SIMBOLO, E COM TAMANHO DE ALVO. Antes era um chevron de 16
      // com a palavra em corpo 10, os dois espremidos numa faixa de 18
      // px — "MUITO pequena", nas palavras do beta. O que o teste segura
      // agora e a area tocavel, que e o que faltava.
      final caixa = tester.getSize(voltar);
      expect(caixa.width, greaterThanOrEqualTo(44), reason: tile);
      expect(caixa.height, greaterThanOrEqualTo(24), reason: tile);
      await tester.tap(find.byKey(const ValueKey('editor-back')));
      await tester.pumpAndSettle();
      expect(
        c.read(editorSessionProvider).panel,
        EditorPanel.none,
        reason: '$tile fechou',
      );
      expect(find.text('Movimentação e transformação'), findsOneWidget);
    }
  });

  testWidgets('Editar texto: tamanho, negrito e conteudo em linhas', (
    tester,
  ) async {
    final c = await openEditor(tester);
    final e = c.read(editorControllerProvider.notifier);
    c.read(selectedLayerProvider.notifier).state = null;
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('editor-fab')));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Texto'));
    await tester.pumpAndSettle();
    final texto = c
        .read(editorControllerProvider)
        .layers
        .whereType<TextLayer>()
        .single;
    c.read(selectedLayerProvider.notifier).state = texto.id;
    await tester.pumpAndSettle();
    if (find.text('Editar texto').evaluate().isNotEmpty) {
      await tester.tap(find.text('Editar texto'));
    }
    await tester.pumpAndSettle();
    expect(c.read(editorSessionProvider).panel, EditorPanel.editText);
    // DOIS: a pastilha da grade e o cabecalho do painel carregam o mesmo
    // nome de proposito — quem entrou pelo tile precisa reencontrar o
    // nome no cabecalho. `findsOneWidget` aqui mediria a coincidencia
    // de o tile ja ter saido da tela, e nao o painel ter aberto.
    expect(find.text('Editar texto'), findsWidgets);

    await tester.tap(find.byKey(const ValueKey('texto-tamanho')));
    await tester.pumpAndSettle();
    await digitar(tester, '90');
    expect(
      (c.read(editorControllerProvider).layerById(texto.id)! as TextLayer)
          .fontSize,
      90,
    );

    final negritoAntes =
        (c.read(editorControllerProvider).layerById(texto.id)! as TextLayer)
            .bold;
    await tester.tap(find.byKey(const ValueKey('texto-negrito')));
    await tester.pumpAndSettle();
    expect(
      (c.read(editorControllerProvider).layerById(texto.id)! as TextLayer).bold,
      !negritoAntes,
    );

    await tester.enterText(
      find.byKey(const ValueKey('texto-conteudo')),
      'Ola mundo',
    );
    await tester.pumpAndSettle();
    expect(
      (c.read(editorControllerProvider).layerById(texto.id)! as TextLayer).text,
      'Ola mundo',
    );
    expect(e.canUndo, isTrue);
  });

  testWidgets('Mesclagem: seis modos com miniatura no Simples, todos no Pro', (
    tester,
  ) async {
    final c = await openEditor(tester);
    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();
    await tester.tap(find.text('Mistura e opacidade'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Mesclagem'));
    await tester.pumpAndSettle();

    // O PAINEL DE MESCLAGEM VIROU CATEGORIAS (v1.1.1): nao ha mais uma
    // fileira unica com os 27 modos. Cada familia e um cabecalho, e so a
    // familia do modo LIGADO nasce aberta — a lista e preguicosa, entao
    // contar chips na tela mediria a altura da janela, nao o catalogo.
    // O catalogo se conta na TABELA; a tela se confere por categoria.
    final todos = [
      for (final cat in categoriasDeMescla) ...cat.modos,
    ];
    expect(todos.length, 29, reason: '17 nativos + 10 Aurea + 2 de recorte');
    expect(categoriasDeMescla.length, 7);

    // Normal e a categoria do modo atual, entao ela ja esta aberta com os
    // dois modos dela na tela.
    expect(find.byKey(const ValueKey('categoria-mescla-Normal')), findsOneWidget);
    expect(find.byKey(const ValueKey('mescla-srcOver')), findsOneWidget);
    expect(find.byKey(const ValueKey('mescla-dissolve')), findsOneWidget);

    // Uma categoria fechada e um cabecalho; abrir revela os modos dela.
    expect(find.byKey(const ValueKey('mescla-multiply')), findsNothing);
    await tester.tap(find.byKey(const ValueKey('categoria-mescla-Escurecer')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('mescla-multiply')));
    await tester.pumpAndSettle();
    expect(
      c.read(editorControllerProvider).layerById(id)!.blendMode,
      BlendMode.multiply,
    );
  });
}
