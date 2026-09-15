import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/presentation/am/am_timeline.dart';
import 'package:aurea/src/features/editor/application/ui/editor_layout.dart';
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/application/ui/pro_mode.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/presentation/shell/transport_bar.dart';
import 'package:aurea/src/features/editor/presentation/am/transform_panel.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_hierarchy_test.dart' show openEditor;

/// FASE 1 DO REDESIGN — A CASCA (docs/UI_REDESIGN_PLAN.md, secao 3.1).
///
/// - As cinco zonas resolvidas de uma vez: o preview so muda pela alca;
///   abrir categoria ou adicionar nunca move o preview; a timeline nunca
///   fica abaixo do minimo (salvo ao adicionar, que e um seletor).
/// - A barra de cima tem UM estado: Voltar, nome, desfazer/refazer, ⚙,
///   Simples/Pro e Exportar — sempre, com ou sem selecao, com ou sem
///   painel aberto.
/// - O transporte tem o timecode tocavel e o ◆ unico.
/// - Sem selecao, o painel contextual e a barra de ADICIONAR (E1).
void main() {
  setUpAll(() async {
    for (final family in ['Aurea Motion Sans', 'Roboto']) {
      await (FontLoader(family)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });

  group('EditorLayoutMetrics', () {
    test('as zonas somam a altura da tela', () {
      for (final h in [640.0, 667.0, 844.0, 932.0]) {
        final m = EditorLayoutMetrics.solve(
          totalHeight: h,
          previewFraction: 0.401,
          sheetFraction: 0.40,
        );
        expect(m.total, closeTo(h, 0.01), reason: 'altura $h');
      }
    });

    test('o preview nao muda quando o painel muda de nivel', () {
      double preview(double sheet, {bool adding = false}) =>
          EditorLayoutMetrics.solve(
            totalHeight: 844,
            previewFraction: 0.401,
            sheetFraction: sheet,
            sheetMayCoverTimeline: adding,
          ).preview;
      final base = preview(0.22);
      expect(preview(0.40), base);
      expect(preview(0.60), base);
      expect(preview(0.60, adding: true), base);
      expect(preview(0.0), base);
    });

    test('a timeline nunca fica abaixo do minimo: o painel encolhe', () {
      final m = EditorLayoutMetrics.solve(
        totalHeight: 640,
        previewFraction: 0.60,
        sheetFraction: 0.60,
      );
      expect(m.timeline, greaterThanOrEqualTo(EditorLayoutMetrics.timelineMin));
      expect(m.preview, greaterThanOrEqualTo(EditorLayoutMetrics.previewMin));
      final ws = EditorLayoutMetrics.workspace(640);
      expect(m.sheet, lessThan(ws * 0.60), reason: 'o painel cedeu');
    });

    test('ao adicionar, o menu pode cobrir a timeline', () {
      final m = EditorLayoutMetrics.solve(
        totalHeight: 640,
        previewFraction: 0.45,
        sheetFraction: 0.60,
        sheetMayCoverTimeline: true,
      );
      expect(m.timeline, lessThan(EditorLayoutMetrics.timelineMin));
      // O menu toma tudo que sobra abaixo do preview (que nao cede).
      expect(m.preview, closeTo(640 * 0.45, 0.01));
      expect(
        m.sheet,
        closeTo(EditorLayoutMetrics.workspace(640) - m.preview, 0.01),
      );
    });

    test('preview expandido toma tudo menos o transporte', () {
      final m = EditorLayoutMetrics.solve(
        totalHeight: 932,
        previewFraction: 0.401,
        sheetFraction: 0.40,
        previewExpanded: true,
      );
      expect(m.preview + m.transport, 932);
      expect(m.timeline, 0);
      expect(m.sheet, 0);
      expect(m.topBar, 0);
    });

    test('a fracao do preview e presa entre 14% e 60% da tela', () {
      // O PISO E 14%, e nao 30%. Um projeto cinemascope (2,39:1) pede
      // 19% da altura para encostar nas duas laterais; preso em 30%, ele
      // ganhava duas tarjas pretas — a queixa do beta. Quem cuida do
      // minimo util e previewMin, em pixels, logo abaixo.
      final baixo = EditorLayoutMetrics.solve(
        totalHeight: 932,
        previewFraction: 0.05,
        sheetFraction: 0.22,
      );
      expect(baixo.preview, closeTo(932 * 0.14, 0.01));
      final alto = EditorLayoutMetrics.solve(
        totalHeight: 932,
        previewFraction: 0.95,
        sheetFraction: 0.22,
      );
      expect(alto.preview, closeTo(932 * 0.60, 0.01));
    });

    test('focusedLayer garante piso generoso de pelo menos 270px para o painel', () {
      for (final h in [667.0, 750.0, 844.0, 932.0]) {
        final m = EditorLayoutMetrics.solve(
          totalHeight: h,
          previewFraction: 0.42,
          sheetFraction: 0.46,
          focusedLayer: true,
          sheetVisible: true,
        );
        expect(m.total, closeTo(h, 0.01), reason: 'soma total para h=$h');
        expect(m.sheet, greaterThanOrEqualTo(270.0), reason: 'sheet >= 270 para h=$h');
        expect(m.timeline, greaterThanOrEqualTo(56.0), reason: 'timeline >= 56 para h=$h');
        expect(m.preview, greaterThanOrEqualTo(EditorLayoutMetrics.previewMin), reason: 'preview vivo');
      }
    });
  });

  group('EditorSession', () {
    ProviderContainer container() {
      final c = ProviderContainer();
      addTearDown(c.dispose);
      // Alguem precisa escutar: e autoDispose.
      c.listen(editorSessionProvider, (_, _) {});
      return c;
    }

    test('nasce sem painel', () {
      final c = container();
      final s = c.read(editorSessionProvider);
      expect(s.panel, EditorPanel.none);
      expect(s.panelOpen, isFalse);
      expect(s.adding, isFalse);
    });

    test('abrir e fechar o painel nao mexe em altura nenhuma', () {
      // AS ALTURAS SAO CONSTANTES. Este teste existe para que ninguem
      // reintroduza um nivel de painel sem perceber: a queixa que fez o
      // arrasto sair foi justamente a de que a tela mudava de tamanho
      // sozinha a cada toque.
      final c = container();
      final n = c.read(editorSessionProvider.notifier);
      n.openPanel(EditorPanel.effects);
      expect(c.read(editorSessionProvider).panel, EditorPanel.effects);
      expect(c.read(editorSessionProvider).panelOpen, isTrue);
      n.openAdd();
      expect(c.read(editorSessionProvider).adding, isTrue);
      n.closeAdd();
      expect(c.read(editorSessionProvider).panel, EditorPanel.none);
      expect(EditorSession.alturaDaFolha, closeTo(0.40, 1e-9));
      expect(EditorSession.alturaDoPreview, closeTo(0.50, 1e-9));
    });

    test('a curva volta para o painel de onde veio', () {
      final c = container();
      final n = c.read(editorSessionProvider.notifier);
      n.openTransform(TransformTool.scale);
      n.openCurve(LayerProp.scale);
      expect(c.read(editorSessionProvider).panel, EditorPanel.curve);
      expect(c.read(editorSessionProvider).curveProp, LayerProp.scale);
      n.backFromCurve();
      expect(c.read(editorSessionProvider).panel, EditorPanel.transform);
      expect(c.read(editorSessionProvider).tool, TransformTool.scale);
    });

    test('editar pontos volta para o painel de onde veio e limpa o item', () {
      final c = container();
      final n = c.read(editorSessionProvider.notifier);
      n.openShape(ShapeTool.points);
      n.openEditPoints('item-1', returnTo: EditorPanel.editShape);
      expect(c.read(editorSessionProvider).panel, EditorPanel.editPoints);
      expect(c.read(editorSessionProvider).pointsItemId, 'item-1');
      n.backFromEditPoints();
      expect(c.read(editorSessionProvider).panel, EditorPanel.editShape);
      expect(c.read(editorSessionProvider).pointsItemId, isNull);
    });
  });

  group('parseTimecodeInput', () {
    test('segundos, minutos e quadros', () {
      expect(parseTimecodeInput('1.5', 30), const Duration(milliseconds: 1500));
      expect(parseTimecodeInput('1,5', 30), const Duration(milliseconds: 1500));
      expect(
        parseTimecodeInput('1:02.5', 30),
        const Duration(seconds: 62, milliseconds: 500),
      );
      expect(parseTimecodeInput('00:01:02', 30), const Duration(seconds: 62));
      expect(
        parseTimecodeInput('00:00:01:15', 30),
        const Duration(seconds: 1, milliseconds: 500),
      );
      expect(parseTimecodeInput('', 30), isNull);
      expect(parseTimecodeInput('abc', 30), isNull);
    });
  });

  testWidgets('o cabecalho acompanha a camada e mantem voltar e ajustes', (
    tester,
  ) async {
    final c = await openEditor(tester);
    // SEM SELECAO a barra e do projeto (nome, ajustes); COM SELECAO a
    // barra da camada toma o lugar (v1.1.1, mapa do dono): voltar, nome da
    // camada e o ⋯ dela. Desfazer e refazer moram na reproducao, sempre.
    void barra(String momento) {
      for (final k in [
        'editor-back',
        'editor-project-name',
        'editor-undo',
        'editor-redo',
        'editor-settings',
      ]) {
        expect(find.byKey(ValueKey(k)), findsOneWidget, reason: '$k $momento');
      }
    }

    void barraDaCamada(String momento) {
      for (final k in [
        'editor-back',
        'camada-nome',
        'camada-menu',
        'editor-undo',
        'editor-redo',
      ]) {
        expect(find.byKey(ValueKey(k)), findsOneWidget, reason: '$k $momento');
      }
      expect(
        find.byKey(const ValueKey('editor-project-name')),
        findsNothing,
        reason: 'a barra do projeto sai com a selecao ($momento)',
      );
    }

    barra('sem selecao');
    // A barra de adicionar so aparece pelo "+": sem selecao o painel fica
    // fechado e a timeline fica com o espaco.
    expect(find.byKey(const ValueKey('add-tab-midia')), findsNothing);
    expect(find.byKey(const ValueKey('editor-fab')), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('editor-fab')));
    await tester.pumpAndSettle();
    expect(
      find.byKey(const ValueKey('add-tab-midia')),
      findsOneWidget,
      reason: 'E1 pelo +',
    );
    await tester.tap(find.byKey(const ValueKey('editor-back')));
    await tester.pumpAndSettle();

    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();
    barraDaCamada('com selecao');
    expect(
      find.text('Movimentação e transformação'),
      findsOneWidget,
      reason: 'E2 com selecao (dock AM)',
    );

    await tester.tap(find.text('Movimentação e transformação'));
    await tester.pumpAndSettle();
    barraDaCamada('com painel aberto');
    expect(find.byType(TransformPanel), findsOneWidget);
  });

  testWidgets('o relógio da navbar permite digitar o tempo exato', (
    tester,
  ) async {
    // NA LINGUA DO AM (v1.1.1): o relogio mora na navbar; tocar nele
    // abre o "ir para o tempo". Segurar ▶| agora vai ao fim.
    await openEditor(tester);
    await tester.tap(find.byKey(const ValueKey('navbar-tempo')));
    await tester.pumpAndSettle();
    final campo = find.byKey(const ValueKey('transport-timecode-campo'));
    expect(campo, findsOneWidget);
    await tester.enterText(campo, '1.5');
    await tester.tap(find.text('Ir'));
    await tester.pumpAndSettle();
    expect(
      tester.widget<AmTimeline>(find.byType(AmTimeline)).playback.time.value,
      const Duration(milliseconds: 1500),
    );
  });

  testWidgets(
    'o ◆ da ferramenta crava e tira o keyframe da propriedade ativa',
    (tester) async {
      final c = await openEditor(tester);
      final id = c.read(editorControllerProvider).layers.first.id;
      c.read(selectedLayerProvider.notifier).state = id;
      await tester.pumpAndSettle();
      await tester.tap(find.text('Movimentação e transformação'));
      await tester.pumpAndSettle();

      Layer camada() => c.read(editorControllerProvider).layerById(id)!;
      expect(camada().keyframeTimes, isEmpty);

      final losango = find.bySemanticsLabel('Marcar keyframe aqui');
      expect(losango, findsOneWidget);
      await tester.tap(losango);
      await tester.pumpAndSettle();
      expect(camada().keyframeTimes, isNotEmpty, reason: 'um keyframe em 0 s');
      expect(find.bySemanticsLabel('Tirar o keyframe daqui'), findsOneWidget);

      await tester.tap(find.bySemanticsLabel('Tirar o keyframe daqui'));
      await tester.pumpAndSettle();
      expect(camada().keyframeTimes, isEmpty, reason: 'o mesmo toque tira');
      expect(
        find.bySemanticsLabel('Marcar keyframe aqui'),
        findsOneWidget,
      );
    },
  );

  testWidgets('editor sempre Pro sem seletor de modo nos ajustes', (
    tester,
  ) async {
    final c = await openEditor(tester);
    expect(c.read(proModeProvider), isTrue);
    c.read(proModeProvider.notifier).set(false);
    expect(c.read(proModeProvider), isTrue);
    // Os ajustes do projeto moram na barra do PROJETO (sem selecao).
    await tester.tap(find.byKey(const ValueKey('editor-settings')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('editor-pro')), findsNothing);
    expect(c.read(proModeProvider), isTrue);
    Navigator.of(tester.element(find.byKey(const ValueKey('projeto-fundo')))).pop();
    await tester.pumpAndSettle();
    c.read(selectedLayerProvider.notifier).state = c
        .read(editorControllerProvider)
        .layers
        .first
        .id;
    await tester.pumpAndSettle();
    expect(find.text('Movimentação e transformação'), findsOneWidget);
    expect(find.byKey(const ValueKey('editor-pro')), findsNothing);
  });

  testWidgets(
    'o + abre a barra; Texto cria em um toque; Midia abre o seletor',
    (tester) async {
      final c = await openEditor(tester);
      final antes = c.read(editorControllerProvider).layers.length;

      await tester.tap(find.byKey(const ValueKey('editor-fab')));
      await tester.pumpAndSettle();
      expect(
        c.read(editorSessionProvider).adding,
        isTrue,
        reason: 'o + e o unico lugar de adicionar',
      );
      await tester.tap(find.text('Texto'));
      await tester.pumpAndSettle();
      final camadas = c.read(editorControllerProvider).layers;
      expect(camadas.length, antes + 1);
      expect(
        camadas.any((l) => l is TextLayer),
        isTrue,
        reason: 'um toque, um texto',
      );

      c.read(selectedLayerProvider.notifier).state = null;
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('editor-fab')));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('add-tab-midia')));
      await tester.pumpAndSettle();
      expect(find.byTooltip('Fechar adicionar'), findsOneWidget);
      // Closing the selector returns directly to the timeline.
      await tester.tap(find.byTooltip('Fechar adicionar'));
      await tester.pumpAndSettle();
      expect(c.read(editorSessionProvider).adding, isFalse);
      expect(find.byKey(const ValueKey('add-tab-midia')), findsNothing);
      expect(
        find.byKey(const ValueKey('editor-fab')).hitTestable(),
        findsOneWidget,
      );
    },
  );

  testWidgets('selecao multipla mostra a barra do conjunto', (tester) async {
    final c = await openEditor(tester);
    final ids = c
        .read(editorControllerProvider)
        .layers
        .map((l) => l.id)
        .toList();
    c.read(selectedLayerProvider.notifier).state = ids[0];
    c.read(multiSelectProvider.notifier).state = {ids[0], ids[1]};
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('selecao-contagem')), findsOneWidget);
    expect(find.byKey(const ValueKey('selecao-agrupar')), findsOneWidget);
    expect(
      find.byKey(const ValueKey('camada-mais')),
      findsNothing,
      reason: 'E2 e de uma camada so',
    );
    await tester.tap(find.byKey(const ValueKey('selecao-limpar')));
    await tester.pumpAndSettle();
    expect(c.read(multiSelectProvider), isEmpty);
  });
}
