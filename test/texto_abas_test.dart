import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/domain/animador_de_texto.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/text_animator.dart';
import 'package:aurea/src/features/editor/presentation/am/text_animators_panel.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_hierarchy_test.dart' show openEditor;

/// AS ABAS DA EDICAO DE TEXTO — EDITAR TEXTO / PRESETS.
///
/// A TERCEIRA ABA ("Animação") SAIU em 20/09, a pedido do dono. Ela abria
/// o Animador Manual: posicoes entrada/enfase/saida, grade de trinta e
/// seis miniaturas, seis controles proprios e uma secao "Avancado" com
/// seletores montados a mao. O veredito foi "praticamente impossivel de
/// usar" e "parece plugin externo".
///
/// Animar texto agora e APLICAR UM EFEITO — Selecionar Texto → Efeitos →
/// Texto → Animador de Texto —, coberto por `animador_de_texto_test.dart`.
/// Aqui fica o que sobrou da ficha: escrever o texto e aplicar uma receita
/// pronta.
///
/// AS PREVIAS DOS PRESETS ANIMAM EM LOOP, entao a partir do momento em que
/// a aba monta `pumpAndSettle` NUNCA assenta. Tudo depois disso usa
/// [assentar].
void main() {
  /// Quadros fixos, sem esperar a arvore ficar parada — ela nao fica.
  Future<void> assentar(WidgetTester tester, [int quadros = 8]) async {
    for (var i = 0; i < quadros; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }
  }

  Future<(ProviderContainer, String)> abrirTexto(WidgetTester tester) async {
    final c = await openEditor(tester);
    c.read(editorControllerProvider.notifier).addTextLayer(Duration.zero);
    await tester.pumpAndSettle();
    final id = c.read(selectedLayerProvider)!;
    c.read(editorSessionProvider.notifier).openPanel(EditorPanel.editText);
    await tester.pumpAndSettle();
    return (c, id);
  }

  TextLayer texto(ProviderContainer c, String id) =>
      c.read(editorControllerProvider).layerById(id)! as TextLayer;

  testWidgets('o painel de texto abre nas duas abas, e a porta do animador '
      'manual nao existe mais', (tester) async {
    final (c, _) = await abrirTexto(tester);

    expect(c.read(editorSessionProvider).textSection, TextSection.edit);
    expect(find.byKey(const ValueKey('texto-conteudo')), findsOneWidget);
    expect(find.byKey(const ValueKey('texto-aba-editar')), findsOneWidget);
    expect(find.byKey(const ValueKey('texto-aba-presets')), findsOneWidget);

    // A ABA GIGANTE SAIU, e com ela o painel proprio: nao ha aba
    // "Animação", nem sub-abas de posicao, nem "Animar texto".
    expect(find.byKey(const ValueKey('texto-animar')), findsNothing);
    expect(find.text('Animação'), findsNothing);
    expect(find.text('Entrada'), findsNothing);
    expect(find.text('Enfase'), findsNothing);
    expect(find.text('Saida'), findsNothing);
    expect(find.text('Animar texto'), findsNothing);
    expect(find.text('Avancado (animadores do AE)'), findsNothing);
    expect(EditorPanel.values.map((e) => e.name), isNot(contains('animators')));
  });

  testWidgets('a sessao que parou na aba antiga volta para a edicao, sem '
      'tela em branco', (tester) async {
    final (c, _) = await abrirTexto(tester);
    c
        .read(editorSessionProvider.notifier)
        .setTextSection(TextSection.animation);
    await tester.pumpAndSettle();

    expect(find.byKey(const ValueKey('texto-conteudo')), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('a aba de presets aplica a pilha de animadores de verdade, e '
      'sabe voltar ao texto parado', (tester) async {
    final (c, id) = await abrirTexto(tester);
    await tester.tap(find.byKey(const ValueKey('texto-aba-presets')));
    await assentar(tester);

    expect(c.read(editorSessionProvider).textSection, TextSection.presets);
    expect(texto(c, id).animators, isEmpty);

    await tester.tap(find.text('Letra por letra'));
    await assentar(tester);

    final pilha = texto(c, id).animators;
    expect(pilha.map((a) => a.name), ['Letra por letra']);
    // KEYFRAMES DE VERDADE: o preset varre o OFFSET no tempo, que e o
    // que faz a janela do seletor atravessar a frase.
    final seletor = faixaDoAnimador(pilha.first)!;
    expect(seletor.offset.isAnimated, isTrue);
    // E e um animador de faixa — o mesmo que o cartao de efeito edita.
    expect(pilha.first.selectors.single, isA<RangeSelector>());
    expect(ehAnimadorDeTexto(pilha.first), isTrue);

    await tester.tap(find.text('Nenhuma'));
    await assentar(tester);
    expect(texto(c, id).animators, isEmpty);
  });

  testWidgets('ir e voltar entre as abas nao perde o texto nem o que foi '
      'animado', (tester) async {
    final (c, id) = await abrirTexto(tester);
    c.read(editorControllerProvider.notifier).editTextLayer(id, text: 'AUREA');
    await tester.pumpAndSettle();

    await tester.tap(find.byKey(const ValueKey('texto-aba-presets')));
    await assentar(tester);
    // A GRADE E PREGUICOSA: o cartao de fora da janela nem existe.
    final alvo = find.text('Fade Up');
    for (var i = 0; i < 10 && alvo.evaluate().isEmpty; i++) {
      await tester.drag(
        find.byType(CustomScrollView).last,
        const Offset(0, -180),
        warnIfMissed: false,
      );
      await assentar(tester, 4);
    }
    expect(alvo, findsOneWidget);
    await tester.ensureVisible(alvo);
    await assentar(tester, 4);
    await tester.tap(alvo);
    await assentar(tester);
    await tester.tap(find.byKey(const ValueKey('texto-aba-editar')));
    await tester.pumpAndSettle();

    expect(c.read(editorSessionProvider).textSection, TextSection.edit);
    expect(find.byKey(const ValueKey('texto-conteudo')), findsOneWidget);
    expect(texto(c, id).text, 'AUREA');
    expect(texto(c, id).animators.map((a) => a.name), ['Fade Up']);
  });

  testWidgets('a previa dos presets continua desenhando, sem painel em '
      'volta', (tester) async {
    final (c, _) = await abrirTexto(tester);
    await tester.tap(find.byKey(const ValueKey('texto-aba-presets')));
    await assentar(tester);
    // O unico pedaco do arquivo antigo que ficou e o desenho.
    expect(find.byType(PreviaDeAnimador), findsWidgets);
    expect(tester.takeException(), isNull);
  });
}
