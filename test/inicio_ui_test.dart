import 'dart:io';

import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/projects/application/project_repository.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:aurea/src/features/projects/presentation/projects_tab.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// A TELA INICIO NUM CELULAR DE VERDADE.
class _MemoryProjects extends ProjectsController {
  @override
  List<VideoProject> build() => [];
}

Future<void> abrirInicio(WidgetTester tester, Size tamanho) async {
  tester.view.physicalSize = tamanho;
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
  await tester.pumpWidget(
    const ProviderScope(
      child: MaterialApp(home: Scaffold(body: ProjectsTab())),
    ),
  );
  await tester.pump();
}

void main() {
  for (final MapEntry(key: nome, value: tamanho) in const {
    'iPhone SE': Size(375, 667),
    'iPhone 13': Size(390, 844),
    'Pixel largo': Size(430, 932),
  }.entries) {
    testWidgets('a Inicio nao estoura a largura no $nome', (tester) async {
      await abrirInicio(tester, tamanho);
      expect(tester.takeException(), isNull);
      // Os dois botoes principais continuam inteiros em qualquer largura.
      expect(find.text('Novo projeto'), findsOneWidget);
      expect(find.text('AutoEdit'), findsOneWidget);
      for (final texto in ['Novo projeto', 'AutoEdit']) {
        final caixa = tester.getRect(find.text(texto));
        expect(
          caixa.width,
          greaterThan(0),
          reason: '$texto sumiu da linha no $nome',
        );
        expect(
          caixa.right,
          lessThanOrEqualTo(tamanho.width),
          reason: '$texto saiu da tela no $nome',
        );
      }
      // Abrir template e importar preset continuam alcancaveis — em
      // quadrado nas telas largas, escritos nas estreitas.
      final temTemplate =
          find.byTooltip('Abrir template').evaluate().isNotEmpty ||
          find.text('Template').evaluate().isNotEmpty;
      expect(temTemplate, isTrue, reason: 'perdeu o Abrir template no $nome');
    });
  }

  test('recente quer dizer recente: quem foi editado vai para a frente', () {
    final pasta = Directory.systemTemp.createTempSync('aurea_inicio');
    addTearDown(() => pasta.deleteSync(recursive: true));
    final container = ProviderContainer(
      overrides: [
        projectsControllerProvider.overrideWith(_MemoryProjects.new),
        projectRepositoryProvider.overrideWithValue(
          ProjectRepository(directory: pasta, installBundledExamples: false),
        ),
      ],
    );
    addTearDown(container.dispose);
    final c = container.read(projectsControllerProvider.notifier);
    final a = VideoProject.empty('A');
    final b = VideoProject.empty('B');
    final d = VideoProject.empty('C');
    for (final p in [a, b, d]) {
      c.add(p);
    }
    // add empilha na frente: C, B, A.
    expect(
      container.read(projectsControllerProvider).map((p) => p.name),
      ['C', 'B', 'A'],
    );
    // Mexer no A leva o A para a frente, sem duplicar ninguem.
    c.upsert(a.copyWith(name: 'A editado'));
    expect(
      container.read(projectsControllerProvider).map((p) => p.name),
      ['A editado', 'C', 'B'],
    );
    // Um projeto que ainda nao estava na lista tambem entra na frente.
    c.upsert(VideoProject.empty('D'));
    expect(
      container.read(projectsControllerProvider).first.name,
      'D',
    );
    expect(container.read(projectsControllerProvider), hasLength(4));
  });
}
