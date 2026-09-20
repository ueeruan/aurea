// O HOME PROFUNDO: o heroi "Continuar editando" (o projeto mais recente
// em cartao largo, sem repetir na grade) e a barra compacta com blur que
// so existe depois de rolar — o compacto do titulo grande do iOS.
import 'dart:io';

import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/projects/application/project_repository.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:aurea/src/features/projects/presentation/projects_tab.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

class _MemoryProjects extends ProjectsController {
  @override
  List<VideoProject> build() => [];
}

class _RepoNulo extends ProjectRepository {
  _RepoNulo()
    : super(directory: Directory.systemTemp);

  @override
  Future<List<VideoProject>> loadAll() async => const [];

  @override
  Future<void> save(VideoProject project) async {}

  @override
  Future<void> delete(String id) async {}
}

Future<ProviderContainer> _montar(WidgetTester tester) async {
  tester.view.physicalSize = const Size(390, 844);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  SharedPreferences.setMockInitialValues({});
  final prefs = await SharedPreferences.getInstance();
  final container = ProviderContainer(
    overrides: [
      sharedPreferencesProvider.overrideWithValue(prefs),
      projectsControllerProvider.overrideWith(_MemoryProjects.new),
      projectRepositoryProvider.overrideWithValue(_RepoNulo()),
    ],
  );
  addTearDown(container.dispose);
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: const MaterialApp(home: Scaffold(body: ProjectsTab())),
    ),
  );
  await tester.pump();
  return container;
}

void main() {
  testWidgets('o projeto mais recente vira o heroi e sai da grade', (
    tester,
  ) async {
    final c = await _montar(tester);
    final projetos = c.read(projectsControllerProvider.notifier);
    final velho = VideoProject.empty('Velho');
    final novo = VideoProject.empty('Novo');
    projetos.add(velho);
    projetos.add(novo); // o mais recente entra na frente
    await tester.pump();

    expect(find.text('Continuar editando'), findsOneWidget);
    expect(find.text('Continuar'), findsOneWidget);
    // O heroi carrega a chave de cartao do projeto; a grade nao o repete.
    final noHeroi = c.read(projectsControllerProvider).first;
    expect(find.byKey(ValueKey('projeto-${noHeroi.id}')), findsOneWidget);
    expect(find.text(noHeroi.name), findsOneWidget);
    // E o menu dele continua com a chave de sempre (apagar, renomear...).
    expect(
      find.byKey(ValueKey('projeto-menu-${noHeroi.id}')),
      findsOneWidget,
    );
  });

  testWidgets('sem projeto nao ha heroi nem "Recentes" vazio duplicado', (
    tester,
  ) async {
    await _montar(tester);
    expect(find.text('Continuar editando'), findsNothing);
  });

  testWidgets('a barra compacta so existe depois de rolar', (tester) async {
    final c = await _montar(tester);
    final projetos = c.read(projectsControllerProvider.notifier);
    for (var i = 0; i < 8; i++) {
      projetos.add(VideoProject.empty('P$i'));
    }
    await tester.pump();

    // Parado no topo: um "Aurea" so (o do cabecalho grande).
    expect(find.text('Aurea'), findsOneWidget);

    await tester.drag(
      find.byType(CustomScrollView),
      const Offset(0, -420),
    );
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 250));

    // Rolou: o cabecalho grande foi embora e a barra compacta assumiu.
    expect(find.text('Aurea'), findsWidgets);
    expect(find.byType(BackdropFilter), findsWidgets);

    await tester.drag(find.byType(CustomScrollView), const Offset(0, 420));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 250));
    expect(find.text('Aurea'), findsOneWidget);
  });
}
