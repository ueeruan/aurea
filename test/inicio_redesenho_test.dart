import 'dart:io';

import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/projects/application/project_repository.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:aurea/src/features/projects/presentation/new_project_sheet.dart';
import 'package:aurea/src/features/projects/presentation/projects_tab.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// A INICIO REDESENHADA: um botao de criar, projetos numa lista com um
/// menu que se ve, e a folha de projeto novo com a moldura do formato.
class _MemoryProjects extends ProjectsController {
  @override
  List<VideoProject> build() => [];
}

/// Um repositorio que nao grava nada. O de verdade escreve em disco por
/// isolate (compute), e isolate nao anda no relogio de mentira do
/// testWidgets: o flush esperaria para sempre.
class _RepoNulo extends ProjectRepository {
  _RepoNulo() : super(directory: Directory.systemTemp);

  @override
  Future<List<VideoProject>> loadAll() async => const [];

  @override
  Future<void> save(VideoProject project) async {}

  @override
  Future<void> delete(String id) async {}
}

/// Quadros contados em vez de pumpAndSettle: a Inicio e as folhas dela
/// tem coisas que animam sem parar (o cursor do Cupertino, por exemplo).
/// Um quadro para a rota nascer e dois de 400 ms para ela chegar.
Future<void> _assentar(WidgetTester tester) async {
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 400));
  await tester.pump(const Duration(milliseconds: 400));
}

Future<ProviderContainer> _montar(WidgetTester tester, Widget home) async {
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
      child: MaterialApp(home: home),
    ),
  );
  await tester.pump();
  return container;
}

void main() {
  testWidgets('um botao de criar, sem pilulas, e o menu de cada projeto', (
    tester,
  ) async {
    final c = await _montar(tester, const Scaffold(body: ProjectsTab()));
    final projetos = c.read(projectsControllerProvider.notifier);
    final um = VideoProject.empty('Um');
    final dois = VideoProject.empty('Dois');
    projetos.add(um);
    projetos.add(dois);
    await tester.pump();

    expect(find.text('Novo projeto'), findsOneWidget);
    expect(find.byTooltip('Abrir template'), findsOneWidget);
    // As pilulas de formato sairam da Inicio: o formato mora na folha.
    expect(find.textContaining('YouTube / TV'), findsNothing);
    expect(find.textContaining('Reels / TikTok'), findsNothing);
    // Cada projeto tem o seu menu, visivel.
    expect(find.byKey(ValueKey('projeto-menu-${um.id}')), findsOneWidget);
    expect(find.byKey(ValueKey('projeto-menu-${dois.id}')), findsOneWidget);
    expect(tester.takeException(), isNull);

    // Duplicar pelo menu.
    await tester.tap(find.byKey(ValueKey('projeto-menu-${dois.id}')));
    await _assentar(tester);
    await tester.tap(find.byKey(const ValueKey('projeto-duplicar')));
    await _assentar(tester);
    final lista = c.read(projectsControllerProvider);
    expect(lista, hasLength(3));
    expect(lista.first.name, 'Dois (cópia)');
    expect(lista.first.id, isNot(dois.id), reason: 'copia tem id novo');

    // Renomear pelo menu.
    await tester.tap(find.byKey(ValueKey('projeto-menu-${um.id}')));
    await _assentar(tester);
    await tester.tap(find.byKey(const ValueKey('projeto-renomear')));
    // O campo com foco anima o cursor sem parar (Cupertino): pumpAndSettle
    // nunca assentaria. Quadros contados, entao.
    await _assentar(tester);
    await tester.enterText(
      find.byKey(const ValueKey('renomear-campo')),
      'Um renomeado',
    );
    await tester.tap(find.byKey(const ValueKey('renomear-salvar')));
    await _assentar(tester);
    await _assentar(tester);
    expect(
      c.read(projectsControllerProvider).map((p) => p.name),
      contains('Um renomeado'),
    );
    expect(
      c.read(projectsControllerProvider).map((p) => p.name),
      isNot(contains('Um')),
    );
    // O upsert agenda a gravacao; sem esperar, o teste termina com um
    // relogio pendente.
    await projetos.flush();
  });

  testWidgets('a folha de projeto novo: moldura, ficha viva e nome sugerido', (
    tester,
  ) async {
    VideoProject? criado;
    await _montar(
      tester,
      Scaffold(
        body: Builder(
          builder: (context) => TextButton(
            onPressed: () async {
              criado = await showNewProjectSheet(
                context,
                nomeSugerido: 'Projeto 3',
              );
            },
            child: const Text('abrir'),
          ),
        ),
      ),
    );
    await tester.tap(find.text('abrir'));
    await tester.pumpAndSettle();

    // Comeca no formato padrao dos ajustes (16:9), com a ficha certa.
    expect(find.byKey(const ValueKey('moldura-formato')), findsOneWidget);
    expect(find.text('16:9'), findsWidgets);
    expect(find.text('1920 × 1080 · 30 fps'), findsOneWidget);

    // Tocar num formato muda a moldura e a ficha na hora.
    await tester.tap(find.byKey(const ValueKey('formato-9:16')));
    await tester.pumpAndSettle();
    expect(find.text('1080 × 1920 · 30 fps'), findsOneWidget);
    final moldura = tester.widget<Text>(
      find.byKey(const ValueKey('moldura-formato')),
    );
    expect(moldura.data, '9:16');
    expect(tester.takeException(), isNull);

    // Criar sem escrever nome usa o sugerido.
    await tester.tap(find.byKey(const ValueKey('criar-projeto')));
    await tester.pumpAndSettle();
    expect(criado, isNotNull);
    expect(criado!.name, 'Projeto 3');
    expect(criado!.aspectRatio, closeTo(9 / 16, 1e-9));
    expect(criado!.resolutionHeight, 1080);
  });
}
