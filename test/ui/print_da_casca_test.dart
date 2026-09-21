// PRINTS DA CASCA NOVA (so gravam com AUREA_PRINT_DIR apontando uma pasta):
//
//   AUREA_PRINT_DIR=build/prints flutter test test/ui/print_da_casca_test.dart
//
// Sem a variavel o teste so monta as telas e confere que nada estourou.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import '../apoio/print_da_ui.dart';

class _ProjetosEmMemoria extends ProjectsController {
  @override
  List<VideoProject> build() => [];

  @override
  void upsert(VideoProject project) => state = [project];
}

void main() {
  setUpAll(carregarFontesReais);

  testWidgets('prints da casca: vazia, camada, painel, adicionar', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final container = ProviderContainer(
      overrides: [
        projectsControllerProvider.overrideWith(_ProjetosEmMemoria.new),
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
            theme: ThemeData(
              platform: TargetPlatform.iOS,
              fontFamily: 'Aurea Motion Sans',
            ),
            home: const EditorScreen(),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();
    await gravarPrint(tester, chave, 'casca-vazia');

    final c = container.read(editorControllerProvider.notifier)
      ..addShapeLayer(Duration.zero, name: 'Fundo')
      ..addTextLayer(Duration.zero, text: 'Título')
      ..addNullLayer(Duration.zero);
    await tester.pumpAndSettle();
    final texto = container
        .read(editorControllerProvider)
        .layers
        .firstWhere((l) => l.name.contains('Título'));
    container.read(selectedLayerProvider.notifier).state = texto.id;
    await tester.pumpAndSettle();
    await gravarPrint(tester, chave, 'casca-camada');

    container.read(painelAbertoProvider.notifier).state = PainelId.transformar;
    await tester.pumpAndSettle();
    await gravarPrint(tester, chave, 'casca-transformar');

    container.read(painelAbertoProvider.notifier).state = PainelId.texto;
    await tester.pumpAndSettle();
    await gravarPrint(tester, chave, 'casca-texto');

    container.read(painelAbertoProvider.notifier).state = null;
    container.read(selectedLayerProvider.notifier).state = null;
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('editor-adicionar')));
    await tester.pumpAndSettle();
    await gravarPrint(tester, chave, 'casca-adicionar');
    expect(c, isNotNull);
    expect(tester.takeException(), isNull);
  });
}
