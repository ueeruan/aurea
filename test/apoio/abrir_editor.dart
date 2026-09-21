import 'dart:io';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

// ABRIR O EDITOR NUM TESTE — a porta de todos os testes de widget do editor.
//
// Monta o `EditorScreen` (a sessao: relogio, video, salvar) com a casca
// NOVA dentro (`ui/shell/editor_shell.dart`), empurrado por uma rota como
// a Home faz, com duas formas no projeto e nada escolhido. Morava em
// `editor_hierarchy_test.dart`, que testava o editor antigo; saiu de la
// para os testes que so precisavam do editor aberto nao dependerem da UI
// apagada.

class _ProjetosEmMemoria extends ProjectsController {
  @override
  List<VideoProject> build() => [];
  @override
  void upsert(VideoProject project) {
    state = [project];
  }
}

Future<ProviderContainer> openEditor(
  WidgetTester tester, {
  Size size = const Size(430, 932),
}) async {
  tester.view.physicalSize = size;
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
  final container = ProviderContainer(
    overrides: [
      projectsControllerProvider.overrideWith(_ProjetosEmMemoria.new),
    ],
  );
  addTearDown(container.dispose);
  final editor = container.read(editorControllerProvider.notifier);
  editor.addShapeLayer(Duration.zero, name: 'Título principal');
  editor.addShapeLayer(Duration.zero, name: 'Fundo');
  container.read(selectedLayerProvider.notifier).state = null;
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: MaterialApp(
        theme: ThemeData(
          platform: TargetPlatform.iOS,
          fontFamily: 'Aurea Motion Sans',
        ),
        home: Builder(
          builder: (context) => Scaffold(
            body: TextButton(
              onPressed: () => Navigator.of(context).push(
                MaterialPageRoute<void>(
                  builder: (_) => const RepaintBoundary(
                    key: ValueKey('editor-capture'),
                    child: EditorScreen(),
                  ),
                ),
              ),
              child: const Text('abrir projeto'),
            ),
          ),
        ),
      ),
    ),
  );
  await tester.tap(find.text('abrir projeto'));
  await tester.pumpAndSettle();
  return container;
}

/// Foto do editor em `build/qa/hierarchy/<nome>.png`, so com
/// `--dart-define=HIERARCHY_SCREENSHOTS=true`.
Future<void> capture(WidgetTester tester, String name) async {
  if (!const bool.fromEnvironment('HIERARCHY_SCREENSHOTS')) return;
  await tester.runAsync(() async {
    final boundary = tester.renderObject<RenderRepaintBoundary>(
      find.byKey(const ValueKey('editor-capture')),
    );
    final image = await boundary.toImage();
    final bytes = await image.toByteData(format: ui.ImageByteFormat.png);
    final file = File('build/qa/hierarchy/$name.png');
    await file.parent.create(recursive: true);
    await file.writeAsBytes(bytes!.buffer.asUint8List());
    image.dispose();
  });
}
