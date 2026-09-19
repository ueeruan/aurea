import 'editor_audit_helpers.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/application/ui/pro_mode.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/layer_meta.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_hierarchy_test.dart' show openEditor;

/// FASE 7 (QA) E FASE 8 (OFICIO) DO REDESIGN.
///
/// Fase 7: os fluxos da secao 10 em toques; alvos de 44 pt nas barras;
/// tablet/paisagem com timeline e painel lado a lado acima de 700 pt.
/// Fase 8: edicao de 3 pontos (Entrada/Saida na regua + Inserir,
/// Sobrescrever, Levantar, Extrair), voz/EQ e dados CSV (Pro).
void main() {
  setUpAll(() async {
    for (final family in ['Aurea Motion Sans', 'Roboto']) {
      await (FontLoader(family)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });

  testWidgets('tablet/paisagem: acima de 700 pt o painel fica ao lado do preview', (tester) async {
    final c = await openEditor(tester, size: const Size(1024, 768));
    expect(find.byKey(const ValueKey('editor-largo')), findsOneWidget);
    // Na primeira abertura o lugar da dica e das QUATRO DICAS de estreia:
    // elas moram na folha, e nao mais por cima do palco (la cobriam a
    // alca de girar). Depois de "Entendi", a linha de dica assume.
    expect(find.byKey(const ValueKey('editor-dica')), findsNothing, reason: 'preview remains unobstructed');

    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();
    final preview = tester.getRect(find.byType(PreviewStage));
    final folha = tester.getRect(find.byKey(const ValueKey('context-sheet')));
    expect(folha.left, greaterThanOrEqualTo(preview.right - 1), reason: 'painel a direita');
    expect(folha.width, closeTo(380, 1));
    expect(folha.height, greaterThan(500), reason: 'painel de altura inteira');
    expect(find.text('Movimentação e transformação'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('alvos de toque: barras de cima e de transporte com 44 pt', (tester) async {
    final c = await openEditor(tester);

    Future<void> medir(List<String> chaves, String quando) async {
      for (final k in chaves) {
        expect(
          find.byKey(ValueKey(k)),
          findsOneWidget,
          reason: '$k na tela ($quando)',
        );
        final r = tester.getSize(find.byKey(ValueKey(k)));
        expect(
          r.height,
          greaterThanOrEqualTo(44),
          reason: '$k altura ${r.height} ($quando)',
        );
        expect(
          r.width,
          greaterThanOrEqualTo(40),
          reason: '$k largura ${r.width} ($quando)',
        );
      }
    }

    // SEM SELECAO: a barra de cima e a DO PROJETO — voltar, desfazer,
    // refazer, projeto e exportar.
    await medir(const [
      'editor-back',
      'editor-undo',
      'editor-redo',
      'editor-settings',
      'editor-export',
      'transport-start',
      'transport-play',
      'transport-end',
      'transport-expand',
      'editor-fab',
    ], 'sem selecao');

    // COM SELECAO: a barra da camada TOMA O LUGAR da do projeto (o
    // comentario esta escrito na propria `BarraDaCamada`). Projeto e
    // exportar saem de cena — procurar os dois aqui acusaria o aplicativo
    // por uma troca que ele faz de proposito.
    c.read(selectedLayerProvider.notifier).state =
        c.read(editorControllerProvider).layers.first.id;
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('editor-settings')), findsNothing);
    await medir(const [
      'editor-back',
      'camada-duplicar',
      'transport-start',
      'transport-play',
      'transport-end',
      'transport-expand',
    ], 'com selecao');
  });

  testWidgets('fluxos da secao 10: dividir em 1 toque, categoria em 2, keyframe em 1', (tester) async {
    final c = await openEditor(tester);
    final e = c.read(editorControllerProvider.notifier);
    final id = c.read(editorControllerProvider).layers.first.id;
    e.trimLayerEnd(id, const Duration(seconds: 4));
    c.read(selectedLayerProvider.notifier).state = id; // selecionar
    await tester.pumpAndSettle();
    // Dividir no cabecote: 1 toque (acao rapida).
    e.moveLayer(id, Duration.zero);
    final playback = tester.widget<PreviewStage>(find.byType(PreviewStage)).playback;
    playback.seek(const Duration(seconds: 2));
    await tester.pumpAndSettle();
    final antes = c.read(editorControllerProvider).layers.length;
    await openLayerActions(tester);
    await tester.ensureVisible(find.byKey(const ValueKey('mais-dividir')));
    await tester.tap(find.byKey(const ValueKey('mais-dividir')));
    await tester.pumpAndSettle();
    expect(c.read(editorControllerProvider).layers.length, antes + 1);
    // Descobrir uma categoria: selecionar + olhar o grid (ja visivel).
    expect(find.text('Efeitos'), findsOneWidget);
    // Keyframe: 1 toque no ◆ da transporte.
    final sel = c.read(selectedLayerProvider)!;
    await tester.tap(find.text('Movimentação e transformação'));
    await tester.pumpAndSettle();
    await tester.tap(find.bySemanticsLabel('Marcar keyframe aqui'));
    await tester.pumpAndSettle();
    expect(c.read(editorControllerProvider).layerById(sel)!.keyframeTimes, isNotEmpty);
  });

  group('edicao de 3 pontos', () {
    test('a sessao guarda Entrada/Saida e so devolve o trecho quando faz sentido', () {
      final c = ProviderContainer();
      addTearDown(c.dispose);
      c.listen(editorSessionProvider, (_, _) {});
      final n = c.read(editorSessionProvider.notifier);
      expect(c.read(editorSessionProvider).inOut, isNull);
      n.setInPoint(const Duration(seconds: 2));
      n.setOutPoint(const Duration(seconds: 1));
      expect(c.read(editorSessionProvider).inOut, isNull, reason: 'saida antes da entrada');
      n.setOutPoint(const Duration(seconds: 3));
      expect(c.read(editorSessionProvider).inOut, (const Duration(seconds: 2), const Duration(seconds: 3)));
      n.clearInOut();
      expect(c.read(editorSessionProvider).inPoint, isNull);
    });

    testWidgets('Pro: I e O na regua, e Extrair tira o trecho da camada', (tester) async {
      final c = await openEditor(tester);
      final e = c.read(editorControllerProvider.notifier);
      c.read(proModeProvider.notifier).set(true);
      final id = c.read(editorControllerProvider).layers.first.id;
      e.moveLayer(id, Duration.zero);
      e.trimLayerEnd(id, const Duration(seconds: 4));
      c.read(selectedLayerProvider.notifier).state = id;
      await tester.pumpAndSettle();


      final playback = tester.widget<PreviewStage>(find.byType(PreviewStage)).playback;
      playback.seek(const Duration(seconds: 1));
      await tester.pumpAndSettle();
      // AS MARCAS MORAM NO MENU DAS MARCAS. Elas ficaram um tempo na
      // regua, e o dono mandou tirar da regua tudo o que a atravanca —
      // ha teste guardando isso. Entrada e Saida sao assunto de marca,
      // e o menu de marcas e onde o assunto mora.
      await tester.tap(find.byKey(const ValueKey('timeline-selo-do-tempo')));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('timeline-entrada')));
      await tester.pumpAndSettle();
      playback.seek(const Duration(seconds: 2));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('timeline-selo-do-tempo')));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('timeline-saida')));
      await tester.pumpAndSettle();
      expect(c.read(editorSessionProvider).inOut, isNotNull);
      expect(find.byKey(const ValueKey('marca-I')), findsOneWidget);
      expect(find.byKey(const ValueKey('marca-O')), findsOneWidget);

      await openLayerActions(tester);
      expect(find.byKey(const ValueKey('mais-levantar')), findsOneWidget);
      await tester.ensureVisible(find.byKey(const ValueKey('mais-extrair')));
      await tester.tap(find.byKey(const ValueKey('mais-extrair')));
      await tester.pumpAndSettle();
      final total = c.read(editorControllerProvider).layers
          .where((l) => l.name.startsWith('Título'))
          .fold(Duration.zero, (d, l) => d + l.duration);
      expect(total, const Duration(seconds: 3), reason: 'um segundo saiu e o resto encostou');
      // O aviso "Desfazer" tem um temporizador: deixa ele terminar.
      await tester.pump(const Duration(seconds: 8));
    });
  });

  test('voz e EQ: o AudioProcessing da camada aceita os seis controles', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addAudioLayer(Duration.zero, 'a.wav', 'Som', const Duration(seconds: 3));
    final id = c.read(editorControllerProvider).layers.single.id;
    e.updateAudioSpec(id, (a) => a.copyWith(processing: a.processing.copyWith(denoise: .5, voice: .3, highDb: 4)));
    final spec = e.audioSpecOf(id)!;
    expect(spec.processing.denoise, .5);
    expect(spec.processing.voice, .3);
    expect(spec.processing.highDb, 4);
    expect(spec.processing.isNeutral, isFalse);
  });

  testWidgets('dados: CSV carregado e o texto vinculado a uma coluna (Pro)', (tester) async {
    final c = await openEditor(tester);
    final e = c.read(editorControllerProvider.notifier);
    c.read(proModeProvider.notifier).set(true);
    e.setDataSource(parseCsv('nome,preco\nAurea,42\n', name: 'tabela.csv'));
    e.addTextLayer(Duration.zero, text: 'x');
    final texto = c.read(editorControllerProvider).layers.whereType<TextLayer>().single;
    c.read(selectedLayerProvider.notifier).state = texto.id;
    await tester.pumpAndSettle();
    await tester.tap(find.text('Editar texto'));
    await tester.pumpAndSettle();
    // A LINHA DE DADOS E A ULTIMA DA LISTA, e a lista e preguicosa: ela
    // so existe depois de rolada ate o fim. `ensureVisible` exige o
    // elemento na arvore, e antes de rolar ele nao esta.
    await tester.dragUntilVisible(
      find.byKey(const ValueKey('texto-dados')),
      find.byType(ListView).last,
      const Offset(0, -60),
    );
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('texto-dados')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('texto-dados-nome')));
    await tester.pumpAndSettle();
    expect((c.read(editorControllerProvider).layerById(texto.id)! as TextLayer).text, 'Aurea');
    expect(c.read(editorControllerProvider).bindings.single.column, 'nome');
  });
}
