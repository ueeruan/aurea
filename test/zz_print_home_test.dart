import 'dart:io';
import 'dart:ui' as ui;

import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/core/theme/app_theme.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/projects/application/project_repository.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:aurea/src/features/projects/presentation/boas_vindas.dart';
import 'package:aurea/src/features/projects/presentation/home_shell.dart';
import 'package:aurea/src/features/projects/presentation/release_notice.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

class _MemoryProjects extends ProjectsController {
  @override
  List<VideoProject> build() => [];
}

class _RepoNulo extends ProjectRepository {
  _RepoNulo()
      : super(directory: Directory.systemTemp, installBundledExamples: false);

  @override
  Future<List<VideoProject>> loadAll() async => const [];

  @override
  Future<void> save(VideoProject project) async {}

  @override
  Future<void> delete(String id) async {}
}

Future<void> _foto(WidgetTester tester, GlobalKey key, String path) async {
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 500));
  final obj = key.currentContext!.findRenderObject()! as RenderRepaintBoundary;
  final img = await obj.toImage(pixelRatio: 2.0);
  final data = await img.toByteData(format: ui.ImageByteFormat.png);
  await File(path).writeAsBytes(data!.buffer.asUint8List(), flush: true);
}

void main() {
  testWidgets('prints da home nova', (tester) async {
    tester.view.physicalSize = const Size(1170, 2532);
    tester.view.devicePixelRatio = 3.0;
    addTearDown(tester.view.reset);
    SharedPreferences.setMockInitialValues({
      chaveDoAceite: '2026-09-17T00:00:00',
      releaseNoticeSeenKey: releaseNoticeRevision,
    });
    final prefs = await SharedPreferences.getInstance();
    final container = ProviderContainer(
      overrides: [
        sharedPreferencesProvider.overrideWithValue(prefs),
        projectsControllerProvider.overrideWith(_MemoryProjects.new),
        projectRepositoryProvider.overrideWithValue(_RepoNulo()),
      ],
    );
    addTearDown(container.dispose);
    final chave = GlobalKey();
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: RepaintBoundary(
          key: chave,
          child: MaterialApp(
            theme: AppTheme.dark,
            locale: const Locale('pt'),
            home: const HomeShell(),
          ),
        ),
      ),
    );
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 500));

    final projetos = container.read(projectsControllerProvider.notifier);
    for (var i = 1; i <= 8; i++) {
      projetos.add(VideoProject.empty('Projeto $i'));
    }
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 500));

    const pasta = r'C:\Users\SnyX\AppData\Local\Temp\opencode';
    await _foto(tester, chave, '$pasta\\home_topo.png');

    await tester.drag(find.byType(CustomScrollView), const Offset(0, -900));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));
    await _foto(tester, chave, '$pasta\\home_rolada.png');

    await projetos.flush();
    expect(true, isTrue);
  });
}
