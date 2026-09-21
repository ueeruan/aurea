// PRINTS DO REDESIGN (sem emulador): a Home nova (cartoes com a
// miniatura de verdade, o topo fixo e chapado). Com AUREA_PRINT_DIR
// apontado, sai um PNG por tela; sem, os testes so provam que nada
// estoura.
import 'dart:io';

import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/core/theme/app_theme.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:aurea/src/features/projects/application/thumbnail_service.dart';
import 'package:aurea/src/features/projects/presentation/projects_tab.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
// ignore: depend_on_referenced_packages
import 'package:path_provider_platform_interface/path_provider_platform_interface.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'apoio/print_da_ui.dart';
import 'apoio/repositorio_sem_disco.dart';

/// path_provider de mentira: as miniaturas moram numa pasta temporaria,
/// e o heroi da Home sai com uma imagem DE VERDADE no print.
class _Docs extends PathProviderPlatform {
  _Docs(this.dir);
  final String dir;
  @override
  Future<String?> getApplicationDocumentsPath() async => dir;
}

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => [
    VideoProject.empty('Clipe da campanha'),
    VideoProject.empty('Vinheta do canal'),
    VideoProject.empty('Estudo de rastreio'),
    VideoProject.empty('Efeitos'),
  ];
}

/// A fonte nativa do botao nao existe no flutter_tester: sem isto o
/// texto do FilledButton sai em retangulos no PNG (mesma solucao do
/// print da Inicio).
ThemeData _temaDeTeste() {
  final theme = AppTheme.dark;
  final button = theme.filledButtonTheme.style!;
  final resolved = button.textStyle!
      .resolve({})!
      .copyWith(fontFamily: 'Roboto');
  return theme.copyWith(
    filledButtonTheme: FilledButtonThemeData(
      style: button.copyWith(textStyle: WidgetStatePropertyAll(resolved)),
    ),
  );
}

String _safe(String id) {
  final c = id.replaceAll(RegExp(r'[^A-Za-z0-9_-]'), '');
  return c.isEmpty ? 'projeto' : c;
}

Future<ProviderContainer> _home(WidgetTester tester, GlobalKey chave) async {
  tester.view.physicalSize = const Size(390, 844);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  SharedPreferences.setMockInitialValues({});
  final prefs = await SharedPreferences.getInstance();
  final c = ProviderContainer(
    overrides: [
      sharedPreferencesProvider.overrideWithValue(prefs),
      projectsControllerProvider.overrideWith(_Projetos.new),
      projectRepositoryProvider.overrideWithValue(RepositorioSemDisco()),
    ],
  );
  addTearDown(c.dispose);

  // A MINIATURA DO HEROI: uma pasta temporaria vira "documentos", e o
  // quadro do CAMPO vira a thumb do projeto mais recente.
  await tester.runAsync(() async {
    final tmp = await Directory.systemTemp.createTemp('aurea-prints');
    // O Windows segura o PNG da miniatura ate o cache soltar: a limpeza
    // e de melhor esforco, a pasta e temporaria de qualquer jeito.
    addTearDown(() {
      try {
        tmp.deleteSync(recursive: true);
      } catch (_) {}
    });
    PathProviderPlatform.instance = _Docs(tmp.path);
    final thumbs = Directory('${tmp.path}/thumbs')..createSync(recursive: true);
    final heroi = c.read(projectsControllerProvider).first;
    File('assets/templates/notes.jpg')
        .copySync('${thumbs.path}/${_safe(heroi.id)}.png');
    await ThumbnailService.instance.init();
  });

  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        theme: _temaDeTeste(),
        home: RepaintBoundary(
          key: chave,
          child: Scaffold(
            backgroundColor: AppColors.background,
            body: const ProjectsTab(),
          ),
        ),
      ),
    ),
  );
  // A miniatura decodifica no relogio DE VERDADE (runAsync); esperar o
  // precacheImage aqui DEADLOCKA o tester — foi o que pendurou a suite.
  await tester.runAsync(
    () => Future<void>.delayed(const Duration(milliseconds: 350)),
  );
  await tester.pump(const Duration(milliseconds: 300));
  await tester.pump(const Duration(milliseconds: 300));
  return c;
}

void main() {
  setUpAll(carregarFontesReais);

  testWidgets('Home nova: cartoes com miniatura, sem estouro', (tester) async {
    final chave = GlobalKey();
    await _home(tester, chave);
    expect(tester.takeException(), isNull);
    expect(find.text('Novo projeto'), findsOneWidget);
    expect(find.text('Clipe da campanha'), findsOneWidget);
    await gravarPrint(tester, chave, 'redesign-inicio');
    await tester.pump(const Duration(seconds: 1));
  });

  testWidgets('Home rolada: o topo fica, a lista rola, sem blur', (
    tester,
  ) async {
    final chave = GlobalKey();
    await _home(tester, chave);
    await tester.drag(find.byType(CustomScrollView), const Offset(0, -520));
    await tester.pump(const Duration(milliseconds: 250));
    expect(tester.takeException(), isNull);
    expect(find.text('AUREA'), findsOneWidget);
    expect(find.byType(BackdropFilter), findsNothing);
    await gravarPrint(tester, chave, 'redesign-inicio-rolada');
    await tester.pump(const Duration(seconds: 1));
  });
}
