import 'package:aurea/src/core/ds/ds.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/application/ui/pro_mode.dart';
import 'package:aurea/src/features/editor/presentation/ui/toolbar/barra_contextual.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio/abrir_editor.dart' show openEditor;

/// FASE 7 (QA) E FASE 8 (OFICIO) DO REDESIGN.
///
/// Fase 7: os fluxos da secao 10 em toques e os alvos de toque das barras
/// (na casca nova). Fase 8: edicao de 3 pontos (Entrada/Saida no menu das
/// marcas + Levantar, Extrair) e voz/EQ.
void main() {
  setUpAll(() async {
    for (final family in ['Aurea Motion Sans', 'Roboto']) {
      await (FontLoader(family)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });

  testWidgets('alvos de toque: botoes das barras com o toque minimo do DS', (tester) async {
    // A CASCA NOVA segue as medidas do plano (secao 2): barra do topo de 42
    // com botao de 40 x 42, transporte de 46 — o toque minimo e 40
    // ([AureaDims.toqueMinimo]); 44 so onde cabe sem estourar a densidade.
    final c = await openEditor(tester);

    Future<void> medir(List<String> chaves, String quando) async {
      for (final k in chaves) {
        expect(find.byKey(ValueKey(k)), findsOneWidget, reason: '$k na tela ($quando)');
        final r = tester.getSize(find.byKey(ValueKey(k)));
        expect(r.height, greaterThanOrEqualTo(AureaDims.toqueMinimo),
            reason: '$k altura ${r.height} ($quando)');
        expect(r.width, greaterThanOrEqualTo(AureaDims.toqueMinimo),
            reason: '$k largura ${r.width} ($quando)');
      }
    }

    await medir(const [
      'topo-voltar',
      'topo-menu',
      'topo-projeto',
      'topo-exportar',
      'transporte-desfazer',
      'transporte-refazer',
      'transporte-anterior',
      'transporte-play',
      'transporte-proximo',
      'editor-adicionar',
    ], 'sem selecao');

    // COM SELECAO a barra de baixo vira a da camada (as ferramentas).
    c.read(selectedLayerProvider.notifier).state =
        c.read(editorControllerProvider).layers.first.id;
    await tester.pumpAndSettle();
    await medir(const [
      'topo-voltar',
      'ferramenta-transformar',
      'ferramenta-dividir',
      'transporte-play',
    ], 'com selecao');
  });

  testWidgets('fluxos da secao 10: dividir em 1 toque, categoria em 1, keyframe em 1', (tester) async {
    final c = await openEditor(tester);
    final e = c.read(editorControllerProvider.notifier);
    final id = c.read(editorControllerProvider).layers.first.id;
    e.trimLayerEnd(id, const Duration(seconds: 4));
    e.moveLayer(id, Duration.zero);
    c.read(selectedLayerProvider.notifier).state = id; // selecionar
    await tester.pumpAndSettle();
    final playback = tester.widget<PreviewStage>(find.byType(PreviewStage)).playback;
    playback.seek(const Duration(seconds: 1));
    await tester.pumpAndSettle();

    // Keyframe: o painel Transformar abre num toque e o losango crava.
    await tester.tap(find.byKey(const ValueKey('ferramenta-transformar')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('kf-posicao')));
    await tester.pumpAndSettle();
    expect(c.read(editorControllerProvider).layerById(id)!.keyframeTimes, isNotEmpty);
    // Fecha o painel pelo Voltar: a barra da camada volta a aparecer.
    await tester.tap(find.byKey(const ValueKey('topo-voltar')));
    await tester.pumpAndSettle();

    // Categoria: Efeitos esta na barra da camada, a um toque.
    expect(find.byKey(const ValueKey('ferramenta-efeitos')), findsOneWidget);

    // Dividir no cabecote: 1 toque (a tesoura da barra).
    final antes = c.read(editorControllerProvider).layers.length;
    await tester.tap(find.byKey(const ValueKey('ferramenta-dividir')));
    await tester.pumpAndSettle();
    expect(c.read(editorControllerProvider).layers.length, antes + 1);
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

    testWidgets('Pro: I e O no menu das marcas, e Extrair tira o trecho da camada', (tester) async {
      final c = await openEditor(tester);
      final e = c.read(editorControllerProvider.notifier);
      c.read(proModeProvider.notifier).set(true);
      final id = c.read(editorControllerProvider).layers.first.id;
      e.moveLayer(id, Duration.zero);
      e.trimLayerEnd(id, const Duration(seconds: 4));
      c.read(selectedLayerProvider.notifier).state = id;
      await tester.pumpAndSettle();

      final playback = tester.widget<PreviewStage>(find.byType(PreviewStage)).playback;
      // AS MARCAS MORAM NO MENU DAS MARCAS: toque longo no cabecote.
      Future<void> marcar(Duration t, String chave) async {
        playback.seek(t);
        await tester.pumpAndSettle();
        await tester.longPress(find.byKey(const ValueKey('timeline-toque-do-cabecote')));
        await tester.pumpAndSettle();
        await tester.ensureVisible(find.byKey(ValueKey(chave)));
        await tester.pumpAndSettle();
        await tester.tap(find.byKey(ValueKey(chave)));
        await tester.pumpAndSettle();
      }

      await marcar(const Duration(seconds: 1), 'timeline-entrada');
      await marcar(const Duration(seconds: 2), 'timeline-saida');
      expect(c.read(editorSessionProvider).inOut, isNotNull);

      // Extrair mora em "Mais acoes..." do menu da camada. O "Mais" e o
      // ultimo da barra da camada, que rola de lado quando nao cabe.
      await tester.scrollUntilVisible(
        find.byKey(const ValueKey('ferramenta-mais')),
        120,
        scrollable: find
            .descendant(
              of: find.byType(BarraContextual),
              matching: find.byType(Scrollable),
            )
            .first,
      );
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('ferramenta-mais')));
      await tester.pumpAndSettle();
      // O MENU E UMA LISTA DE 450 que rola: os ultimos itens so existem
      // depois de rolados.
      Future<void> tocarNoMenu(String chave) async {
        await tester.scrollUntilVisible(
          find.byKey(ValueKey(chave)),
          120,
          scrollable: find
              .descendant(
                of: find.byType(AureaMenu<String>),
                matching: find.byType(Scrollable),
              )
              .first,
        );
        await tester.pumpAndSettle();
        await tester.tap(find.byKey(ValueKey(chave)));
        await tester.pumpAndSettle();
      }

      await tocarNoMenu('menu-camada-mais-acoes');
      await tocarNoMenu('menu-camada-extrair');
      final total = c.read(editorControllerProvider).layers
          .where((l) => l.name.startsWith('Título'))
          .fold(Duration.zero, (d, l) => d + l.duration);
      expect(total, const Duration(seconds: 3), reason: 'um segundo saiu e o resto encostou');
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
}
