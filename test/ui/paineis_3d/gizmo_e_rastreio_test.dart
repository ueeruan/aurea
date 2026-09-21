// O GIZMO NA CASCA NOVA, O PAINEL RASTREAR E A REGRA DO PREVIEW UNICO.
//
//   * o gizmo do objeto esta montado sobre o palco da casca nova: nasce com
//     a cena selecionada e some sem ela; as fichas de modo sao botoes do DS;
//   * Rastrear abre com Analisar;
//   * com o painel aberto num video analisado, os pontos aparecem NO PALCO
//     e o toque num ponto o escolhe;
//   * nenhum painel do 3D tem previa propria (RawImage, Texture, palco).
import 'package:aurea/src/core/ds/ds.dart';
import 'package:aurea/src/features/editor/application/camera_track_service.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/algebra_numerica.dart';
import 'package:aurea/src/features/editor/domain/camera_solver3d.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/ambiente.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/animacao3d.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/camera.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/cena3d.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/luz.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/material.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/rastrear.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/texto3d.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/editor_shell.dart';
import 'package:aurea/src/features/editor/presentation/widgets/gizmo_da_cena_overlay.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'banco.dart';

class _ProjetosEmMemoria extends ProjectsController {
  @override
  List<VideoProject> build() => [];

  @override
  void upsert(VideoProject project) => state = [project];
}

/// Uma solucao sintetica: camera parada na origem olhando +Z, focal 500,
/// quadro de 640 x 360. O ponto 1 cai no CENTRO do quadro; o 2, 250 px a
/// direita.
SolucaoCamera3D _solucao() => SolucaoCamera3D(
  largura: 640,
  altura: 360,
  focalPx: 500,
  poses: [
    PoseCamera(0, Mat3.identidade, const [0, 0, 0]),
  ],
  nuvem: const {
    1: [0, 0, 100],
    2: [50, 0, 100],
  },
  erroPixels: 0.4,
  quadros: 24,
  fps: 24,
  motor: 'teste',
  analiseMs: 1200,
);

void main() {
  testWidgets('o gizmo do objeto esta NA CASCA NOVA: nasce com a cena '
      'selecionada e some sem ela', (tester) async {
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final c = ProviderContainer(
      overrides: [
        projectsControllerProvider.overrideWith(_ProjetosEmMemoria.new),
      ],
    );
    addTearDown(c.dispose);
    final ctl = c.read(editorControllerProvider.notifier);
    ctl.addScene3DLayer(Duration.zero);
    final id = c.read(editorControllerProvider).layers.single.id;
    ctl.addSceneNode(id, Element3DKind.cube);
    c.read(selectedLayerProvider.notifier).state = null;

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: const MaterialApp(home: EditorScreen()),
      ),
    );
    await tester.pumpAndSettle();
    expect(find.byType(EditorShell), findsOneWidget);
    expect(find.byType(PreviewStage), findsOneWidget);
    expect(find.byKey(const ValueKey('gizmo-da-cena')), findsNothing);

    c.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();
    // DENTRO DO PALCO da casca (o unico preview), e nao numa previa nova.
    final gizmo = find.descendant(
      of: find.byType(PreviewStage),
      matching: find.byKey(const ValueKey('gizmo-da-cena')),
    );
    expect(gizmo, findsOneWidget);
    for (final m in ModoDoGizmo3D.values) {
      expect(find.byKey(ValueKey('gizmo-modo-${m.name}')), findsOneWidget);
    }
    // As fichas sao os botoes de barra do design system.
    expect(
      find.descendant(
        of: find.byKey(const ValueKey('gizmo-modos')),
        matching: find.byType(AureaToolbarButton),
      ),
      findsNWidgets(3),
    );

    c.read(selectedLayerProvider.notifier).state = null;
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('gizmo-da-cena')), findsNothing);
    expect(find.byKey(const ValueKey('gizmo-modo-mover')), findsNothing);
    expect(tester.takeException(), isNull);
  });

  testWidgets('as fichas do gizmo trocam o modo (Mover, Girar, Escalar)', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(600, 600);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final c = containerNovo();
    final ctl = controladorDe(c);
    ctl.addScene3DLayer(Duration.zero);
    final id = c.read(editorControllerProvider).layers.single.id;
    ctl.addSceneNode(id, Element3DKind.cube);
    c.read(selectedLayerProvider.notifier).state = id;
    final tempo = ValueNotifier(Duration.zero);
    addTearDown(tempo.dispose);
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          home: Scaffold(
            body: Stack(
              children: [
                const SizedBox.expand(),
                GizmoDaCenaOverlay(tempo: tempo, escala: 1),
              ],
            ),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();
    for (final m in ModoDoGizmo3D.values) {
      await tester.tap(find.byKey(ValueKey('gizmo-modo-${m.name}')));
      await tester.pumpAndSettle();
      expect(c.read(modoDoGizmo3DProvider), m);
    }
    // Alvo de dedo: nenhum botao de modo menor que 44.
    final r = tester.getRect(find.byKey(const ValueKey('gizmo-modo-girar')));
    expect(r.height, greaterThanOrEqualTo(44));
    expect(r.width, greaterThanOrEqualTo(44));
    expect(tester.takeException(), isNull);
  });

  testWidgets('Rastrear abre com Analisar (e sem analise nao oferece criar)', (
    tester,
  ) async {
    final c = containerNovo();
    final video = controladorDe(c).addVideoLayer(
      Duration.zero,
      '/nao/existe.mp4',
      'Clipe',
      const Duration(seconds: 3),
    );
    await montar(tester, c, (_) => PainelRastrear(layerId: video));
    expect(find.byKey(const ValueKey('painel-rastrear')), findsOneWidget);
    expect(find.byKey(const ValueKey('rastreio-analisar')), findsOneWidget);
    expect(find.byKey(const ValueKey('rastreio-tomada-auto')), findsOneWidget);
    expect(find.byKey(const ValueKey('rastreio-criar-cena')), findsNothing);
    // As quatro abas: Analisar, Pontos, Criar, Seguir objeto (BlobTracker).
    await tocarNaAba(tester, 'rastrear', 3);
    expect(find.byKey(const ValueKey('rastreio-objetos')), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('com o painel aberto num video analisado, os pontos aparecem '
      'no palco e o toque escolhe', (tester) async {
    final c = containerNovo();
    final projeto = c.read(editorControllerProvider);
    final w = projeto.outputWidth.toDouble();
    final h = projeto.outputHeight.toDouble();
    tester.view.physicalSize = Size(w, h);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final video = controladorDe(c).addVideoLayer(
      Duration.zero,
      '/nao/existe.mp4',
      'Clipe',
      const Duration(seconds: 3),
    );
    CameraTrackService.instance.adotar(video, _solucao());
    addTearDown(() => CameraTrackService.instance.clear(video));
    c.read(selectedLayerProvider.notifier).state = video;
    final tempo = ValueNotifier(Duration.zero);
    addTearDown(tempo.dispose);

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          home: Scaffold(
            body: Stack(
              children: [
                const SizedBox.expand(),
                GizmoDaCenaOverlay(tempo: tempo, escala: 1),
              ],
            ),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();
    // Painel fechado: nada no palco.
    expect(
      find.byKey(const ValueKey('rastreio-pontos-no-palco')),
      findsNothing,
    );

    c.read(painelAbertoProvider.notifier).state = PainelId.rastrear;
    await tester.pumpAndSettle();
    expect(
      find.byKey(const ValueKey('rastreio-pontos-no-palco')),
      findsOneWidget,
    );

    // O ponto 1 cai no centro do video, que esta no centro da composicao.
    await tester.tapAt(Offset(w / 2, h / 2));
    await tester.pumpAndSettle();
    final escolha = c.read(pontosDoRastreioProvider);
    expect(escolha.layerId, video);
    expect(escolha.ids, {1});

    // A conta da projecao: o ponto 2 fica 250/640 da largura da caixa a
    // direita do 1.
    final caixa = controladorDe(c).layerBoxRect(
      c.read(editorControllerProvider).layerById(video)!,
      Duration.zero,
    );
    final pontos = pontosDoRastreioNaComposicao(
      _solucao(),
      0,
      centro: Offset(w / 2, h / 2),
      caixa: caixa.size,
    );
    expect(pontos[1]!.dx, closeTo(w / 2, 1e-6));
    expect(
      pontos[2]!.dx - pontos[1]!.dx,
      closeTo(caixa.width * 250 / 640, 1e-6),
    );

    c.read(painelAbertoProvider.notifier).state = null;
    await tester.pumpAndSettle();
    expect(
      find.byKey(const ValueKey('rastreio-pontos-no-palco')),
      findsNothing,
    );
    expect(tester.takeException(), isNull);
  });

  testWidgets('nenhum painel do 3D tem previa propria', (tester) async {
    final c = containerNovo();
    final ctl = controladorDe(c);
    final (texto3d, _) = await criarTexto3D(tester, c);
    ctl.addScene3DLayer(Duration.zero);
    final cena = c
        .read(editorControllerProvider)
        .layers
        .whereType<Scene3DLayer>()
        .firstWhere((l) => l.id != texto3d)
        .id;
    ctl.addSceneNode(cena, Element3DKind.cube);
    ctl.addCameraLayer(Duration.zero);
    final camera = c
        .read(editorControllerProvider)
        .layers
        .whereType<CameraLayer>()
        .single
        .id;
    final video = ctl.addVideoLayer(
      Duration.zero,
      '/nao/existe.mp4',
      'Clipe',
      const Duration(seconds: 3),
    );

    final casos = <(String, int, Widget Function())>[
      ('texto3d', 5, () => PainelTexto3D(layerId: texto3d)),
      ('cena3d', 6, () => PainelCena3D(layerId: cena)),
      ('cena3d', 6, () => PainelCena3D(layerId: texto3d)),
      ('material', 0, () => PainelMaterial(layerId: cena)),
      ('material', 0, () => PainelMaterial(layerId: texto3d)),
      ('luz', 0, () => PainelLuz(layerId: cena)),
      ('ambiente', 0, () => PainelAmbiente(layerId: cena)),
      ('animacao3d', 0, () => PainelAnimacao3D(layerId: cena)),
      ('animacao3d', 0, () => PainelAnimacao3D(layerId: texto3d)),
      ('camera', 3, () => PainelCamera(layerId: camera)),
      ('rastrear', 4, () => PainelRastrear(layerId: video)),
    ];
    for (final (nome, abas, painel) in casos) {
      await montar(
        tester,
        c,
        (_) => KeyedSubtree(key: UniqueKey(), child: painel()),
      );
      for (var i = 0; i < (abas == 0 ? 1 : abas); i++) {
        if (abas > 0) await tocarNaAba(tester, nome, i);
        final raiz = find.byKey(ValueKey('painel-$nome'));
        expect(raiz, findsOneWidget, reason: nome);
        for (final tipo in [RawImage, Texture, PreviewStage, CompositionView]) {
          expect(
            find.descendant(of: raiz, matching: find.byType(tipo)),
            findsNothing,
            reason: '$nome aba $i: $tipo',
          );
        }
        expect(tester.takeException(), isNull, reason: '$nome aba $i');
      }
    }
  });
}
