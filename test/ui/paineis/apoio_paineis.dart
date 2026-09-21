import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// A lista de projetos em memoria: o salvamento automatico escreve aqui.
class ProjetosEmMemoria extends ProjectsController {
  @override
  List<VideoProject> build() => [];

  @override
  void upsert(VideoProject project) {
    state = [project];
  }
}

/// O que a bancada de um painel devolve: o container (projeto, providers),
/// as preferencias do aparelho (para ler o que foi lembrado) e os paineis
/// que o painel pediu para abrir.
class BancadaDoPainel {
  BancadaDoPainel(this.container, this.prefs, this.abertos);

  final ProviderContainer container;
  final SharedPreferences prefs;
  final List<PainelId> abertos;

  EditorController get c => container.read(editorControllerProvider.notifier);
  VideoProject get projeto => container.read(editorControllerProvider);
  Layer camada(String id) => projeto.layerById(id)!;
}

/// Monta UM painel (o que [painel] constroi para a camada) na base da
/// tela, com a casca minima em volta: o relogio e o gerente de video de
/// verdade ([EscopoDoEditor]), um Navigator com Material (folhas, menus,
/// teclado numerico) e as preferencias em memoria.
///
/// [preparar] cria o projeto e devolve o id da camada que o painel edita.
/// Sem a casca inteira, o palco e a timeline nao entram: o teste mede o
/// painel e so ele.
Future<(BancadaDoPainel, String)> montarPainel(
  WidgetTester tester, {
  required String Function(EditorController c) preparar,
  required Widget Function(String layerId) painel,
  double altura = 326,
  Size tamanho = const Size(390, 844),
  Map<String, Object> prefsIniciais = const {},
}) async {
  tester.view.physicalSize = tamanho;
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
  SharedPreferences.setMockInitialValues(prefsIniciais);
  final prefs = await SharedPreferences.getInstance();
  final container = ProviderContainer(
    overrides: [
      sharedPreferencesProvider.overrideWithValue(prefs),
      projectsControllerProvider.overrideWith(ProjetosEmMemoria.new),
    ],
  );
  addTearDown(container.dispose);
  final c = container.read(editorControllerProvider.notifier);
  final id = preparar(c);
  container.read(selectedLayerProvider.notifier).state = id;
  final abertos = <PainelId>[];
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: MaterialApp(
        home: _Hospedeiro(
          altura: altura,
          aoAbrir: abertos.add,
          filho: painel(id),
        ),
      ),
    ),
  );
  await tester.pumpAndSettle();
  return (BancadaDoPainel(container, prefs, abertos), id);
}

class _Hospedeiro extends StatefulWidget {
  const _Hospedeiro({
    required this.altura,
    required this.aoAbrir,
    required this.filho,
  });

  final double altura;
  final void Function(PainelId) aoAbrir;
  final Widget filho;

  @override
  State<_Hospedeiro> createState() => _HospedeiroState();
}

class _HospedeiroState extends State<_Hospedeiro>
    with TickerProviderStateMixin {
  late final PlaybackController _playback = PlaybackController(
    vsync: this,
    durationOf: () => const Duration(seconds: 10),
  );
  final VideoLayerManager _videos = VideoLayerManager();

  @override
  void dispose() {
    _playback.dispose();
    _videos.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => Scaffold(
    body: EscopoDoEditor(
      playback: _playback,
      videos: _videos,
      abrirPainel: widget.aoAbrir,
      fecharPainel: () {},
      child: Align(
        alignment: Alignment.bottomCenter,
        child: SizedBox(height: widget.altura, child: widget.filho),
      ),
    ),
  );
}

/// Um projeto so com [camadas] (camadas de midia construidas a mao: nao
/// passam pela sonda do arquivo, que um teste nao tem).
void abrirProjetoCom(EditorController c, List<Layer> camadas) =>
    c.openProject(VideoProject.empty('teste').copyWith(layers: camadas));

VideoLayer videoDeTeste() => VideoLayer(
  name: 'Video',
  startTime: Duration.zero,
  duration: const Duration(seconds: 4),
  sourcePath: 'nao-existe.mp4',
);

AudioLayer audioDeTeste() => AudioLayer(
  name: 'Musica',
  startTime: Duration.zero,
  duration: const Duration(seconds: 4),
  sourcePath: 'nao-existe.m4a',
);

/// Arrasta [alvo] em [passos] passos de [cada], um quadro por passo — o
/// dedo de verdade, e nao um salto so.
Future<void> arrastarEmPassos(
  WidgetTester tester,
  Finder alvo, {
  int passos = 12,
  Offset cada = const Offset(8, 0),
}) async {
  final g = await tester.startGesture(tester.getCenter(alvo));
  for (var i = 0; i < passos; i++) {
    await g.moveBy(cada);
    await tester.pump(const Duration(milliseconds: 16));
  }
  await g.up();
  await tester.pump();
}
