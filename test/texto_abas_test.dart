
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/text_anim.dart';
import 'package:aurea/src/features/editor/domain/text_animator.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_hierarchy_test.dart' show openEditor;

/// AS TRES ABAS DA EDICAO DE TEXTO — EDITAR TEXTO / ANIMACAO / PRESETS.
///
/// O animador manual tinha painel proprio, aberto de fora, e os presets
/// nao tinham tela nenhuma. Agora os dois moram dentro do painel de
/// texto, e o antigo caminho nao existe mais.
///
/// AS PREVIAS ANIMAM EM LOOP (`AnimationController.repeat`), entao a
/// partir do momento em que a aba de animacao monta `pumpAndSettle`
/// NUNCA assenta. Tudo depois disso usa [assentar].
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

  testWidgets('o painel de texto abre nas tres abas, e nao ha mais botao de '
      'animar dentro da edicao', (tester) async {
    final (c, _) = await abrirTexto(tester);

    expect(c.read(editorSessionProvider).textSection, TextSection.edit);
    // A aba de conteudo mostra a edicao; as outras duas existem ao lado.
    expect(find.byKey(const ValueKey('texto-conteudo')), findsOneWidget);
    expect(find.byKey(const ValueKey('texto-aba-editar')), findsOneWidget);
    expect(find.byKey(const ValueKey('texto-animar')), findsOneWidget);
    expect(find.byKey(const ValueKey('texto-aba-presets')), findsOneWidget);

    // O BOTAO ANTIGO SAIU: nao existe mais "Animar texto" dentro da
    // edicao, e nao existe mais o painel proprio do animador.
    expect(find.text('Animar texto'), findsNothing);
    expect(
      EditorPanel.values.map((e) => e.name),
      isNot(contains('animators')),
    );
  });

  testWidgets('a aba de animacao abre o animador manual, em area maior, e '
      'ele continua mexendo no texto', (tester) async {
    final (c, id) = await abrirTexto(tester);
    final alturaDaEdicao = tester.getSize(
      find.byKey(const ValueKey('context-sheet')),
    ).height;

    await tester.tap(find.byKey(const ValueKey('texto-animar')));
    await assentar(tester);

    expect(c.read(editorSessionProvider).textSection, TextSection.animation);
    // A edicao deu lugar ao animador: o campo de conteudo sai de cena.
    expect(find.byKey(const ValueKey('texto-conteudo')), findsNothing);
    // As sub-abas do animador (as posicoes da animacao) estao ali.
    expect(find.text('Entrada'), findsOneWidget);
    expect(find.text('Enfase'), findsOneWidget);
    expect(find.text('Saida'), findsOneWidget);

    // A FERRAMENTA PEDE MAIS ESPACO QUE A FICHA.
    final alturaDaAnimacao = tester.getSize(
      find.byKey(const ValueKey('context-sheet')),
    ).height;
    expect(
      alturaDaAnimacao,
      greaterThan(alturaDaEdicao),
      reason:
          'a aba de animacao nao pode ficar espremida no painel antigo '
          '($alturaDaEdicao x $alturaDaAnimacao em 430x932)',
    );

    // Escolher uma animacao do catalogo muda a camada — a mesma logica
    // de antes, agora por dentro do painel de texto.
    final alvo = find.text('Quicar por letra');
    for (var i = 0; i < 10 && alvo.evaluate().isEmpty; i++) {
      await tester.drag(
        find.byType(CustomScrollView).last,
        const Offset(0, -180),
        warnIfMissed: false,
      );
      await assentar(tester, 4);
    }
    expect(alvo, findsOneWidget, reason: 'o catalogo de entrada esta na tela');
    // A TILE PODE ESTAR CONSTRUIDA E FORA DA JANELA (a grade e preguicosa):
    // tocar sem trazer para dentro da area visivel cai no vazio.
    await tester.ensureVisible(alvo);
    await assentar(tester, 4);
    await tester.tap(alvo);
    await assentar(tester);

    final entrada = texto(
      c,
      id,
    ).anims.firstWhere((a) => a.slot == TextAnimSlot.entrada);
    expect(entrada.specId, 'bounceLetter');
    expect(entrada.ease, TextAnimEase.mola);
  });

  testWidgets('a aba de presets aplica a pilha de animadores de verdade, e '
      'sabe voltar ao texto parado', (tester) async {
    final (c, id) = await abrirTexto(tester);
    await tester.tap(find.byKey(const ValueKey('texto-aba-presets')));
    await assentar(tester);

    expect(c.read(editorSessionProvider).textSection, TextSection.presets);
    expect(texto(c, id).animators, isEmpty);

    await tester.tap(find.text('Fade por letra'));
    await assentar(tester);

    final pilha = texto(c, id).animators;
    expect(pilha.map((a) => a.name), ['Fade por letra']);
    // KEYFRAMES DE VERDADE: o preset varre o seletor de 0 a 1 no tempo.
    final seletor = pilha.first.selectors.first as RangeSelector;
    expect(seletor.start.isAnimated, isTrue);

    await tester.tap(find.text('Nenhuma'));
    await assentar(tester);
    expect(texto(c, id).animators, isEmpty);
  });

  testWidgets('ir e voltar entre as tres abas nao perde o texto nem o que '
      'foi animado', (tester) async {
    final (c, id) = await abrirTexto(tester);
    c.read(editorControllerProvider.notifier).editTextLayer(id, text: 'AUREA');
    await tester.pumpAndSettle();

    await tester.tap(find.byKey(const ValueKey('texto-aba-presets')));
    await assentar(tester);
    await tester.tap(find.text('Subir por letra'));
    await assentar(tester);
    await tester.tap(find.byKey(const ValueKey('texto-animar')));
    await assentar(tester);
    await tester.tap(find.byKey(const ValueKey('texto-aba-editar')));
    await tester.pumpAndSettle();

    expect(c.read(editorSessionProvider).textSection, TextSection.edit);
    expect(find.byKey(const ValueKey('texto-conteudo')), findsOneWidget);
    expect(texto(c, id).text, 'AUREA');
    expect(texto(c, id).animators.map((a) => a.name), ['Subir por letra']);
  });
}
