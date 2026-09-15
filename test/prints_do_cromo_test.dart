// PRINTS DO CROMO DO EDITOR (v1.1.1, sem emulador): barra do projeto,
// barra de reproducao, trilho do palco, barra da selecao, barra do lote
// e o popup de adicionar. Com AUREA_PRINT_DIR apontado sai um PNG por
// tela; sem, os testes provam que o cromo monta num celular sem estouro.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/editor/application/ui/opcoes_de_visualizacao.dart';
import 'package:aurea/src/features/editor/presentation/shell/cromo_editor.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio/print_da_ui.dart';

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
}

Future<(ProviderContainer, GlobalKey)> _editor(WidgetTester tester) async {
  tester.view.physicalSize = const Size(390, 844);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  final c = ProviderContainer(
    overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
  );
  addTearDown(c.dispose);
  final editor = c.read(editorControllerProvider.notifier);
  editor.addShapeLayer(Duration.zero, name: 'Título principal');
  editor.addShapeLayer(Duration.zero, name: 'Fundo');
  c.read(selectedLayerProvider.notifier).state = null;
  final chave = GlobalKey();
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        theme: ThemeData(
          platform: TargetPlatform.iOS,
          fontFamily: 'Aurea Motion Sans',
          brightness: Brightness.dark,
        ),
        home: RepaintBoundary(key: chave, child: const EditorScreen()),
      ),
    ),
  );
  await tester.pumpAndSettle();
  return (c, chave);
}

void main() {
  setUpAll(carregarFontesReais);

  testWidgets('o cromo do editor monta sem estouro', (tester) async {
    final (_, chave) = await _editor(tester);
    expect(tester.takeException(), isNull);
    // As pecas no lugar: relogio na barra do projeto, reproducao, trilho.
    expect(find.byKey(const ValueKey('navbar-tempo')), findsOneWidget);
    expect(find.byKey(const ValueKey('editor-undo')), findsOneWidget);
    expect(find.byKey(const ValueKey('playbar-marcador')), findsOneWidget);
    expect(find.byKey(const ValueKey('editor-fab')), findsOneWidget);
    expect(find.byKey(const ValueKey('timeline-overflow')), findsOneWidget);
    // A coluna de visualizacao nasce fechada: o palco fica livre.
    expect(find.byKey(const ValueKey('coluna-de-visualizacao')), findsNothing);
    await gravarPrint(tester, chave, 'cromo-editor');
  });

  testWidgets('o olho da barra abre a coluna: grade, pixels e zoom', (
    tester,
  ) async {
    final (c, chave) = await _editor(tester);
    await tester.tap(find.byKey(const ValueKey('playbar-visualizacao')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('coluna-de-visualizacao')), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('visao-grade')));
    await tester.pumpAndSettle();
    expect(c.read(opcoesDeVisualizacaoProvider).grade, isTrue);
    expect(find.byKey(const ValueKey('palco-grade')), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('rail-zoom-mais')));
    await tester.pumpAndSettle();
    expect(c.read(zoomDoPalcoProvider), closeTo(1.25, 1e-9));
    // Fora do ajustado, o numero aparece sobre o palco e volta no toque.
    expect(find.byKey(const ValueKey('preview-zoom-indicador')), findsOneWidget);
    await gravarPrint(tester, chave, 'cromo-editor-visualizacao');
    await tester.tap(find.byKey(const ValueKey('preview-zoom-indicador')));
    await tester.pumpAndSettle();
    expect(c.read(zoomDoPalcoProvider), 1.0);
    expect(find.byKey(const ValueKey('preview-zoom-indicador')), findsNothing);
    // A coluna inteira mora dentro do palco, acima da barra de reproducao.
    final coluna = tester.getRect(
      find.byKey(const ValueKey('coluna-de-visualizacao')),
    );
    final barra = tester.getRect(find.byKey(const ValueKey('editor-undo')));
    expect(coluna.bottom, lessThanOrEqualTo(barra.top));
    expect(tester.takeException(), isNull);
  });

  testWidgets('o marcador da barra marca e desmarca o cabecote', (
    tester,
  ) async {
    final (c, _) = await _editor(tester);
    await tester.tap(find.byKey(const ValueKey('playbar-marcador')));
    await tester.pumpAndSettle();
    expect(c.read(editorControllerProvider).markers, hasLength(1));
    await tester.tap(find.byKey(const ValueKey('playbar-marcador')));
    await tester.pumpAndSettle();
    expect(c.read(editorControllerProvider).markers, isEmpty);
    // O salvamento adiado do projeto termina antes do teste acabar.
    await tester.pump(const Duration(seconds: 1));
  });

  testWidgets('a barra de informacoes toma o lugar da reproducao', (
    tester,
  ) async {
    final (c, chave) = await _editor(tester);
    c.read(infobarProvider.notifier).state = const DadosDaInfobar.tempo(
      tempo: Duration(milliseconds: 1250),
      deslocamento: Duration(milliseconds: -500),
    );
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('barra-de-informacoes')), findsOneWidget);
    expect(find.byKey(const ValueKey('editor-undo')), findsNothing);
    expect(find.text('0:01.25'), findsOneWidget);
    expect(find.text('-0:00.50'), findsOneWidget);
    await gravarPrint(tester, chave, 'cromo-editor-infobar');
    c.read(infobarProvider.notifier).state = null;
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('editor-undo')), findsOneWidget);
  });

  testWidgets('camada selecionada: a barra flutuante de aparar/dividir', (
    tester,
  ) async {
    final (c, chave) = await _editor(tester);
    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
    expect(find.byType(BarraDaSelecao), findsOneWidget);
    expect(find.byKey(const ValueKey('camada-aparar-esq')), findsOneWidget);
    expect(find.byKey(const ValueKey('transport-keyframe')), findsOneWidget);
    await gravarPrint(tester, chave, 'cromo-editor-selecao');
  });

  testWidgets('multi-selecao: a barra do lote assume o topo', (
    tester,
  ) async {
    final (c, chave) = await _editor(tester);
    final ids = c.read(editorControllerProvider).layers.map((l) => l.id);
    c.read(multiSelectProvider.notifier).state = ids.toSet();
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
    expect(find.byType(BarraDoLote), findsOneWidget);
    expect(find.byKey(const ValueKey('selectbar-agrupar')), findsOneWidget);
    expect(
      find.byKey(const ValueKey('selectbar-alinhar-esquerda')),
      findsOneWidget,
    );
    await gravarPrint(tester, chave, 'cromo-editor-multi');
  });

  testWidgets('o + abre o popup de adicionar com as abas', (
    tester,
  ) async {
    final (_, chave) = await _editor(tester);
    await tester.tap(find.byKey(const ValueKey('editor-fab')));
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
    expect(find.byKey(const ValueKey('add-tab-forma')), findsOneWidget);
    expect(find.byKey(const ValueKey('add-tab-objeto')), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('add-tab-objeto')));
    await tester.pumpAndSettle();
    expect(find.text('Nulo'), findsOneWidget);
    await gravarPrint(tester, chave, 'cromo-editor-adicionar');
  });
}
