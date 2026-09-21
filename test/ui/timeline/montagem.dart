import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/ui/timeline/timeline.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// A lista de projetos em memoria: o salvamento automatico escreve aqui.
class _ProjetosEmMemoria extends ProjectsController {
  @override
  List<VideoProject> build() => [];

  @override
  void upsert(VideoProject project) {
    state = [project];
  }
}

/// A TIMELINE SOZINHA, sem motor nem palco: 390 x 280 (a zona da casca).
class Bancada {
  Bancada(this.container, this.playback);

  final ProviderContainer container;
  final PlaybackController playback;

  EditorController get c => container.read(editorControllerProvider.notifier);
  VideoProject get projeto => container.read(editorControllerProvider);

  /// O estado de vista da timeline montada.
  EstadoDaTimeline estado(WidgetTester tester) =>
      tester.state<TimelineDoEditorState>(find.byType(TimelineDoEditor)).estado;
}

const larguraDaBancada = 390.0;
const alturaDaBancada = 280.0;

Future<Bancada> montarTimeline(
  WidgetTester tester, {
  void Function(EditorController c)? preparar,
  VoidCallback? aoTocarNoVazio,
}) async {
  tester.view.physicalSize = const Size(larguraDaBancada, alturaDaBancada);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  final container = ProviderContainer(
    overrides: [
      projectsControllerProvider.overrideWith(_ProjetosEmMemoria.new),
    ],
  );
  addTearDown(container.dispose);
  final c = container.read(editorControllerProvider.notifier);
  preparar?.call(c);
  container.read(selectedLayerProvider.notifier).state = null;
  final playback = PlaybackController(
    vsync: tester,
    durationOf: () => container.read(editorControllerProvider).duration,
  );
  addTearDown(playback.dispose);
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: MaterialApp(
        home: Scaffold(
          body: TimelineDoEditor(
            playback: playback,
            aoTocarNoVazio: aoTocarNoVazio,
          ),
        ),
      ),
    ),
  );
  await tester.pumpAndSettle();
  return Bancada(container, playback);
}

/// O centro da linha da camada [id].
Offset centroDaLinha(WidgetTester tester, String id) =>
    tester.getCenter(find.byKey(ValueKey('linha-$id')));

/// O ponto da linha de [id] no instante [t] (s) do projeto, com a vista
/// de agora, na altura [y] da linha (padrao: o meio).
Offset pontoNoTempo(
  WidgetTester tester,
  Bancada b,
  String id,
  double t, {
  double? y,
}) {
  final linha = tester.getRect(find.byKey(ValueKey('linha-$id')));
  final x = b.estado(tester).xDoTempo(t * 1e6);
  return Offset(linha.left + x, y == null ? linha.center.dy : linha.top + y);
}
