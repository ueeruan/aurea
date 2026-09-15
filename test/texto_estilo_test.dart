// BARRA DE ESTILO DO TEXTO (v1.1.1): alinhamento que cicla e vale no
// palco, no texto animado e no arquivo; fonte por um mini navegador com
// favoritas e recentes (e a folha completa para importar); tamanho numa
// folha curta com tamanhos prontos; cor; concluir fecha o painel.
import 'dart:convert';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/context/categories/barra_de_estilo_do_texto.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
}

ProviderContainer _container() {
  final c = ProviderContainer(
    overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
  );
  addTearDown(c.dispose);
  return c;
}

TextLayer _texto(ProviderContainer c) =>
    c.read(editorControllerProvider).layers.whereType<TextLayer>().first;

void main() {
  test('o alinhamento cicla e volta do arquivo (centro nao se grava)', () {
    expect(proximoAlinhamento(TextAlign.center), TextAlign.right);
    expect(proximoAlinhamento(TextAlign.right), TextAlign.left);
    expect(proximoAlinhamento(TextAlign.left), TextAlign.center);

    final c = _container();
    final e = c.read(editorControllerProvider.notifier);
    e.addTextLayer(Duration.zero);
    final id = _texto(c).id;
    final semAlinhamento = jsonEncode(
      projectToJson(c.read(editorControllerProvider)),
    );
    expect(semAlinhamento.contains('"align"'), isFalse);

    e.editTextLayer(id, alinhamento: TextAlign.right);
    final volta = projectFromJson(projectToJson(c.read(editorControllerProvider)));
    expect(volta.layers.whereType<TextLayer>().single.alinhamento, TextAlign.right);
    // Duplicar e editar outra coisa nao perdem o alinhamento.
    expect(_texto(c).duplicated().alinhamento, TextAlign.right);
    e.editTextLayer(id, fontSize: 50);
    expect(_texto(c).alinhamento, TextAlign.right);
  });

  testWidgets('a barra: alinhar, fonte com favoritas e recentes, tamanho, concluir', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final c = _container();
    c.read(editorControllerProvider.notifier).addTextLayer(Duration.zero);
    final id = _texto(c).id;
    c.read(editorControllerProvider.notifier).editTextLayer(id, text: 'Um\nTítulo maior');
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: const MaterialApp(home: EditorScreen()),
      ),
    );
    await tester.pumpAndSettle();
    c.read(selectedLayerProvider.notifier).state = id;
    c.read(editorSessionProvider.notifier).openPanel(EditorPanel.editText);
    await tester.pumpAndSettle();

    await tester.tap(find.byKey(const ValueKey('texto-alinhar')));
    await tester.pumpAndSettle();
    expect(_texto(c).alinhamento, TextAlign.right);
    // O palco desenha as linhas com o alinhamento da camada.
    expect(
      find.byWidgetPredicate(
        (w) => w is Text && w.data == 'Um\nTítulo maior' && w.textAlign == TextAlign.right,
      ),
      findsOneWidget,
    );

    await tester.tap(find.byKey(const ValueKey('texto-fonte-rapida')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('mini-navegador-de-fontes')), findsOneWidget);
    await tester.tap(find.byKey(ValueKey('fonte-estrela-todas-$fonteDoAplicativo')));
    await tester.pumpAndSettle();
    expect(find.byKey(ValueKey('fonte-fav-$fonteDoAplicativo')), findsOneWidget);
    await tester.tap(find.byKey(ValueKey('fonte-todas-$fonteDoAplicativo')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('mini-navegador-de-fontes')), findsNothing);
    expect(_texto(c).fontFamily, isNull, reason: 'a fonte do app e o nulo');

    await tester.tap(find.byKey(const ValueKey('texto-fonte-rapida')));
    await tester.pumpAndSettle();
    expect(find.byKey(ValueKey('fonte-rec-$fonteDoAplicativo')), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('fontes-ver-todas')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('fontes-importar')), findsOneWidget);
    Navigator.of(tester.element(find.byKey(const ValueKey('fontes-importar')))).pop();
    await tester.pumpAndSettle();

    await tester.tap(find.byKey(const ValueKey('texto-tamanho-rapido')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('texto-tamanho-pronto-48')));
    await tester.pumpAndSettle();
    expect(_texto(c).fontSize, 48);
    Navigator.of(
      tester.element(find.byKey(const ValueKey('texto-tamanho-pronto-48'))),
    ).pop();
    await tester.pumpAndSettle();

    await tester.tap(find.byKey(const ValueKey('texto-concluir')));
    await tester.pumpAndSettle();
    expect(c.read(editorSessionProvider).panel, EditorPanel.none);
    expect(tester.takeException(), isNull);
    await tester.pump(const Duration(seconds: 1));
  });
}
