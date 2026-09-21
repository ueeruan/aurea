// A BARRA DO PROJETO E A TIMELINE (v1.1.1): o titulo se edita ali mesmo,
// a trilha de grupos leva a qualquer nivel, o selo do tempo marca o
// instante e o cabecalho da linha mostra etiqueta, visto e recorte.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
}

Future<ProviderContainer> _editor(WidgetTester tester) async {
  tester.view.physicalSize = const Size(390, 844);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  final c = ProviderContainer(
    overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
  );
  addTearDown(c.dispose);
  final e = c.read(editorControllerProvider.notifier);
  e.renameProject('Meu vídeo');
  e.addShapeLayer(Duration.zero, name: 'A');
  e.addShapeLayer(Duration.zero, name: 'B');
  c.read(selectedLayerProvider.notifier).state = null;
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(home: EditorScreen()),
    ),
  );
  await tester.pumpAndSettle();
  return c;
}

void main() {
  testWidgets('o nome no topo renomeia, e o renomear se desfaz', (
    tester,
  ) async {
    // O NOME DO PROJETO mora no centro da barra do topo: o toque abre o
    // pedido de nome (o mesmo dialogo das camadas).
    final c = await _editor(tester);
    await tester.tap(find.byKey(const ValueKey('topo-nome')));
    await tester.pumpAndSettle();
    final campo = find.byKey(const ValueKey('estudio-nome'));
    expect(campo, findsOneWidget);
    await tester.enterText(campo, 'Abertura do canal');
    await tester.testTextInput.receiveAction(TextInputAction.done);
    await tester.pumpAndSettle();
    expect(c.read(editorControllerProvider).name, 'Abertura do canal');
    expect(find.byKey(const ValueKey('estudio-nome')), findsNothing);

    // Vazio nao vale: fica o que estava.
    await tester.tap(find.byKey(const ValueKey('topo-nome')));
    await tester.pumpAndSettle();
    await tester.enterText(find.byKey(const ValueKey('estudio-nome')), '   ');
    await tester.testTextInput.receiveAction(TextInputAction.done);
    await tester.pumpAndSettle();
    expect(c.read(editorControllerProvider).name, 'Abertura do canal');

    c.read(editorControllerProvider.notifier).undo();
    await tester.pumpAndSettle();
    expect(c.read(editorControllerProvider).name, 'Meu vídeo');
    await tester.pump(const Duration(seconds: 1));
  });

  testWidgets('dentro de grupos, a ponta da regua sai um nivel por toque', (
    tester,
  ) async {
    final c = await _editor(tester);
    final e = c.read(editorControllerProvider.notifier);
    e.groupLayers([for (final l in c.read(editorControllerProvider).layers) l.id]);
    final grupo = c.read(editorControllerProvider).layers.single.id;
    e.enterGroup(grupo);
    await tester.pumpAndSettle();
    final sair = find.byKey(const ValueKey('timeline-sair-do-grupo'));
    expect(sair, findsOneWidget);
    expect(e.nomeDoProjetoRaiz, 'Meu vídeo');

    // Um grupo dentro do grupo.
    e.groupLayers([c.read(editorControllerProvider).layers.first.id]);
    final interno = c.read(editorControllerProvider).layers.first.id;
    e.enterGroup(interno);
    await tester.pumpAndSettle();
    expect(e.profundidadeDoGrupo, 2);

    await tester.tap(sair);
    await tester.pumpAndSettle();
    expect(e.profundidadeDoGrupo, 1);
    await tester.tap(sair);
    await tester.pumpAndSettle();
    expect(e.profundidadeDoGrupo, 0);
    expect(sair, findsNothing);
    expect(tester.takeException(), isNull);
    await tester.pump(const Duration(seconds: 1));
  });

  testWidgets('tocar no cabecote marca o instante', (tester) async {
    final c = await _editor(tester);
    await tester.tap(find.byKey(const ValueKey('timeline-toque-do-cabecote')));
    await tester.pumpAndSettle();
    expect(c.read(editorControllerProvider).markers, hasLength(1));
    expect(tester.takeException(), isNull);
    await tester.pump(const Duration(seconds: 1));
  });
}
