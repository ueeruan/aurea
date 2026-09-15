// PRINTS DO CROMO DO EDITOR (v1.1.1, sem emulador): barra do projeto,
// barra de reproducao, trilho do palco, barra da selecao, barra do lote
// e o popup de adicionar. Com AUREA_PRINT_DIR apontado sai um PNG por
// tela; sem, os testes provam que o cromo monta num celular sem estouro.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
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
    expect(find.byKey(const ValueKey('rail-zoom-texto')), findsOneWidget);
    expect(find.byKey(const ValueKey('editor-fab')), findsOneWidget);
    expect(find.byKey(const ValueKey('timeline-overflow')), findsOneWidget);
    await gravarPrint(tester, chave, 'cromo-editor');
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
