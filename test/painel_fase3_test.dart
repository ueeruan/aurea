import 'editor_audit_helpers.dart';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/application/ui/pro_mode.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/presentation/context/parameter_row.dart';
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
      await tester.tap(find.text('Mesclar e\nopacidade')); // 2
      await tester.pumpAndSettle();
      expect(find.text('Mesclagem e opacidade'), findsOneWidget);
      await tester.tap(find.byKey(const ValueKey('opacidade-valor'))); // 3
      await tester.pumpAndSettle();
      await digitar(tester, '40'); // 4
      final camada = c.read(editorControllerProvider).layerById(id)!;
      expect(camada.opacity.valueAt(Duration.zero), closeTo(0.4, 1e-6));
    },
  );

  testWidgets('Transformar: Opacidade e Escalar em linhas com valor tocavel', (
    tester,
  ) async {
    final c = await openEditor(tester);
    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();
    await tester.tap(find.text('Movimentação e transformação'));
    await tester.pumpAndSettle();
    await selectTransformTool(tester, 'Opacid.');
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('opacidade-valor')));
    await tester.pumpAndSettle();
    await digitar(tester, '25');
    expect(
      c
          .read(editorControllerProvider)
          .layerById(id)!
          .opacity
          .valueAt(Duration.zero),
      closeTo(0.25, 1e-6),
    );

    await selectTransformTool(tester, 'Escalar');
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('scale-width-valor')));
    await tester.pumpAndSettle();
    await digitar(tester, '150');
    final l = c.read(editorControllerProvider).layerById(id)!;
    expect(l.scaleX.valueAt(Duration.zero), closeTo(1.5, 1e-6));
    expect(
      l.scaleY.valueAt(Duration.zero),
      closeTo(1.5, 1e-6),
      reason: 'proporcao travada',
    );

    // O losango da linha crava keyframe na propriedade.
    await tester.tap(find.byTooltip('Adicionar keyframe neste instante'));
    await tester.pumpAndSettle();
    expect(
      c.read(editorControllerProvider).layerById(id)!.scaleX.isAnimated,
      isTrue,
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
      'Mesclar e\nopacidade',
      'Cor e\npreench.',
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
    if (find.text('Editar\ntexto').evaluate().isNotEmpty) {
      await tester.tap(find.text('Editar\ntexto'));
    }
    await tester.pumpAndSettle();
    expect(c.read(editorSessionProvider).panel, EditorPanel.editText);
    expect(find.text('Editar texto'), findsOneWidget);

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
    await tester.tap(find.text('Mesclar e\nopacidade'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Mesclagem'));
    await tester.pumpAndSettle();
    Finder chips() => find.byWidgetPredicate(
      (w) =>
          w.key is ValueKey<String> &&
          (w.key as ValueKey<String>).value.startsWith('mescla-'),
    );
    expect(
      chips().evaluate().length,
      27,
      reason: 'Pro permanente: todos os modos',
    );
    expect(find.text('Tela'), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('mescla-multiply')));
    await tester.pumpAndSettle();
    expect(
      c.read(editorControllerProvider).layerById(id)!.blendMode,
      BlendMode.multiply,
    );

    c.read(proModeProvider.notifier).set(true);
    await tester.pumpAndSettle();
    expect(chips().evaluate().length, 27, reason: 'Pro: 17 nativos + 10 Aurea');
  });
}
