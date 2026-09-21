import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio/abrir_editor.dart' show openEditor;

/// A LINHA VERMELHA DE APOIO, provada pelo que ela desenha.
///
/// O relato do beta foi "a linha vermelha ta bugando, os user passam o
/// dedo por cima e para de mexer". A linha em si nunca parou nada — ela
/// vive dentro de um `IgnorePointer`. Quem parava era o ENCAIXE,
/// invisível, agarrando o objeto ao passar pelo centro; e a linha, que
/// aparecia o tempo todo por estar ligada apenas à seleção, não
/// explicava nada.
///
/// Estes testes fixam as duas metades da correção: a linha só existe
/// enquanto o dedo move o objeto E encaixou, e some ao soltar.

/// Um canvas que anota o que foi desenhado, para perguntar ao pintor o
/// que ele pintou sem depender de imagem nenhuma.
class _CanvasEspiao implements ui.Canvas {
  final vermelhas = <(Offset, Offset)>[];

  @override
  void drawLine(Offset a, Offset b, ui.Paint p) {
    // A cor da linha de apoio, e só ela: os riscos de guia e de grade
    // usam outras, e contá-los junto tornaria o teste sempre verde.
    //
    // A comparação é pelo inteiro, e não por `==` entre cores: `Paint`
    // guarda os canais como float, e a volta não bate exatamente com a
    // constante — um teste que falha por um bit de arredondamento não
    // prova nada sobre a linha.
    if (p.color.toARGB32() == 0xCCFF6B6B) vermelhas.add((a, b));
  }

  @override
  dynamic noSuchMethod(Invocation invocation) => null;
}

List<(Offset, Offset)> _linhas(WidgetTester tester, Size comp) {
  final pintor = tester
      .widget<CustomPaint>(find.byKey(const ValueKey('composition-guides')))
      .painter!;
  final espiao = _CanvasEspiao();
  pintor.paint(espiao, comp);
  return espiao.vermelhas;
}

void main() {
  testWidgets('sem arrastar, nao ha linha de apoio nenhuma', (tester) async {
    final c = await openEditor(tester, size: const Size(430, 844));
    final projeto = c.read(editorControllerProvider);
    final id = projeto.layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();

    final comp = Size(
      projeto.outputWidth.toDouble(),
      projeto.outputHeight.toDouble(),
    );
    // Selecionar NAO e mover. A cruz permanente que existia aqui era um
    // enfeite que a pessoa aprendia a ignorar — e, quando o objeto
    // parava nela, ela ja nao queria dizer nada.
    expect(_linhas(tester, comp), isEmpty);
  });

  testWidgets('a linha aparece ao encaixar e some ao soltar', (tester) async {
    final c = await openEditor(tester, size: const Size(430, 844));
    final projeto = c.read(editorControllerProvider);
    final id = projeto.layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();

    final comp = Size(
      projeto.outputWidth.toDouble(),
      projeto.outputHeight.toDouble(),
    );
    // A camada nasce no centro da composicao, que e um alvo de encaixe:
    // um arrasto curto sai e volta para dentro da tolerancia.
    final palco = tester.getCenter(find.byType(PreviewStage));
    final gesto = await tester.startGesture(palco);
    await tester.pump();
    // O reconhecedor de escala tem folga propria: um arrasto de seis
    // pixels nem chega ao palco. Trinta passam da folga e ainda deixam
    // o eixo vertical dentro da tolerancia de encaixe.
    await gesto.moveBy(const Offset(30, 2));
    await tester.pump();
    await gesto.moveBy(const Offset(4, 0));
    await tester.pumpAndSettle();

    expect(
      _linhas(tester, comp),
      isNotEmpty,
      reason: 'encaixou no centro e nao mostrou a linha',
    );

    await gesto.up();
    await tester.pumpAndSettle();
    expect(
      _linhas(tester, comp),
      isEmpty,
      reason: 'a linha ficou na tela depois de soltar o dedo',
    );
  });

  testWidgets('passar correndo pelo centro nao gruda o objeto', (
    tester,
  ) async {
    // O RELATO, ao pe da letra: "os user passam o dedo por cima e para
    // de mexer". Quem atravessa a tela depressa nao esta mirando o
    // centro; quem quer alinhar chega devagar. Um evento de movimento
    // maior que a propria tolerancia e a assinatura de quem estava so
    // passando.
    final c = await openEditor(tester, size: const Size(430, 844));
    final projeto = c.read(editorControllerProvider);
    final id = projeto.layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();

    final comp = Size(
      projeto.outputWidth.toDouble(),
      projeto.outputHeight.toDouble(),
    );
    final palco = tester.getCenter(find.byType(PreviewStage));
    final gesto = await tester.startGesture(palco - const Offset(120, 0));
    await tester.pump();
    // Um salto que atravessa o centro de uma vez.
    await gesto.moveBy(const Offset(120, 0));
    await tester.pumpAndSettle();
    expect(
      _linhas(tester, comp),
      isEmpty,
      reason: 'o encaixe agarrou alguem que so estava passando',
    );
    await gesto.up();
    await tester.pumpAndSettle();
  });

  testWidgets('longe de qualquer alvo, nao ha linha', (tester) async {
    final c = await openEditor(tester, size: const Size(430, 844));
    final projeto = c.read(editorControllerProvider);
    final id = projeto.layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();

    final comp = Size(
      projeto.outputWidth.toDouble(),
      projeto.outputHeight.toDouble(),
    );
    final palco = tester.getCenter(find.byType(PreviewStage));
    final gesto = await tester.startGesture(palco);
    await tester.pump();
    // Longe o bastante para sair da tolerancia nos dois eixos.
    await gesto.moveBy(const Offset(70, 55));
    await tester.pumpAndSettle();

    expect(
      _linhas(tester, comp),
      isEmpty,
      reason: 'sem encaixe nao deve haver linha',
    );
    await gesto.up();
    await tester.pumpAndSettle();
  });
}
