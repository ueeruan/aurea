// PRINTS DA TIMELINE NOVA (so gravam com AUREA_PRINT_DIR apontando uma pasta):
//
//   AUREA_PRINT_DIR=build/prints flutter test test/ui/timeline/print_da_timeline_test.dart
//
// Sem a variavel o teste so monta as telas e confere que nada estourou.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:aurea/src/features/editor/presentation/ui/timeline/timeline.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import '../../apoio/print_da_ui.dart';

class _ProjetosEmMemoria extends ProjectsController {
  @override
  List<VideoProject> build() => [];

  @override
  void upsert(VideoProject project) => state = [project];
}

void main() {
  setUpAll(carregarFontesReais);

  testWidgets('prints da timeline: camadas, escolhida, expandida', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(390, 280);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final container = ProviderContainer(
      overrides: [
        projectsControllerProvider.overrideWith(_ProjetosEmMemoria.new),
      ],
    );
    addTearDown(container.dispose);
    final c = container.read(editorControllerProvider.notifier)
      ..addShapeLayer(Duration.zero, name: 'Fundo')
      ..addTextLayer(const Duration(milliseconds: 600), text: 'Título')
      ..addNullLayer(const Duration(seconds: 1));
    final texto = container
        .read(editorControllerProvider)
        .layers
        .firstWhere((l) => l.name.contains('Título'));
    c
      ..toggleKeyframe(texto.id, const Duration(seconds: 1), LayerProp.position)
      ..toggleKeyframe(texto.id, const Duration(seconds: 2), LayerProp.opacity)
      ..toggleKeyframe(texto.id, const Duration(seconds: 2), LayerProp.scale);
    container.read(selectedLayerProvider.notifier).state = null;
    final playback = PlaybackController(
      vsync: tester,
      durationOf: () => container.read(editorControllerProvider).duration,
    );
    addTearDown(playback.dispose);
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
            home: Scaffold(body: TimelineDoEditor(playback: playback)),
          ),
        ),
      ),
    );
    playback.seek(const Duration(milliseconds: 1400));
    await tester.pumpAndSettle();
    await gravarPrint(tester, chave, 'timeline-camadas');

    container.read(selectedLayerProvider.notifier).state = texto.id;
    await tester.pumpAndSettle();
    await gravarPrint(tester, chave, 'timeline-escolhida');

    container.read(camadasExpandidasProvider.notifier).state = {texto.id};
    container.read(propriedadeAtivaProvider.notifier).state =
        const PropriedadeAtiva.transformacao(LayerProp.opacity);
    await tester.pumpAndSettle();
    await gravarPrint(tester, chave, 'timeline-expandida');
    expect(tester.takeException(), isNull);
  });
}
