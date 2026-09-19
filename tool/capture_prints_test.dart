import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/domain/camera3d.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/editor/presentation/widgets/painel_de_transformacao.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';

class _MemoriaProjetos extends ProjectsController {
  @override
  List<VideoProject> build() => [];
  @override
  void upsert(VideoProject p) => state = [p];
}

Future<void> carregarFontes() async {
  for (final family in [
    'Aurea Motion Sans',
    'Roboto',
    'CupertinoSystemText',
    'CupertinoSystemDisplay',
    '.SF Pro Text',
    '.SF Pro Display',
    '.SF UI Text',
    '.SF UI Display',
    '.AppleSystemUIFont',
    'FlutterTest',
    'Ahem',
  ]) {
    try {
      await (FontLoader(family)
            ..addFont(rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf')))
          .load();
    } catch (_) {}
  }
  try {
    await (FontLoader('MaterialIcons')
          ..addFont(rootBundle.load('fonts/MaterialIcons-Regular.otf')))
        .load();
  } catch (_) {}
  try {
    await (FontLoader('packages/cupertino_icons/CupertinoIcons')
          ..addFont(rootBundle.load('packages/cupertino_icons/assets/CupertinoIcons.ttf')))
        .load();
  } catch (_) {}
}

void main() {
  setUpAll(() async {
    TestWidgetsFlutterBinding.ensureInitialized();
    await carregarFontes();
  });

  testWidgets('Capturar prints da UI do Aurea', (tester) async {
    const dir = 'output/ui_prints';
    Directory(dir).createSync(recursive: true);

    tester.view.physicalSize = const Size(820, 1720);
    tester.view.devicePixelRatio = 2.0;
    addTearDown(tester.view.reset);

    final prevOnError = FlutterError.onError;
    FlutterError.onError = (details) {
      if (details.exceptionAsString().contains('overflowed')) return;
      prevOnError?.call(details);
    };
    addTearDown(() => FlutterError.onError = prevOnError);

    final container = ProviderContainer(
      overrides: [
        projectsControllerProvider.overrideWith(_MemoriaProjetos.new),
      ],
    );
    addTearDown(container.dispose);

    final shapeLayer = ShapeLayer(
      id: 'camada_forma',
      name: 'Retângulo arredondado 1',
      startTime: Duration.zero,
      duration: const Duration(seconds: 3),
      rotation: AnimatedDouble(117.0),
      scaleX: AnimatedDouble(1.009),
      scaleY: AnimatedDouble(6.535),
      position: AnimatedOffset(
        const Offset(414.1, 469.7),
        [
          Keyframe(time: Duration.zero, value: const Offset(414.1, 469.7), ease: Easing.easeInOut),
          Keyframe(time: const Duration(seconds: 2), value: const Offset(414.1, 700), ease: Easing.easeInOut),
        ],
      ),
      contents: ShapePresets.paramRect(),
    );

    final sceneLayer = Scene3DLayer(
      id: 'camada_3d',
      name: 'Câmera 1',
      startTime: Duration.zero,
      duration: const Duration(seconds: 10),
      camera: Camera3D(posZ: AnimatedDouble(500)),
      scene: Scene3D(
        nodes: [
          SceneNode(
            kind: Element3DKind.cube,
            name: 'Cubo Metálico',
            x: AnimatedDouble(-60),
            size: 75,
            material: materialFromPreset(MaterialPreset3D.chrome),
          ),
          SceneNode(
            kind: Element3DKind.sphere,
            name: 'Esfera Dourada',
            x: AnimatedDouble(65),
            size: 85,
            material: materialFromPreset(MaterialPreset3D.polishedMetal),
          ),
        ],
      ),
    );

    final projeto = VideoProject(
      id: 'demo_ui',
      name: 'Aurea • Motion Design',
      createdAt: DateTime.now(),
      aspectRatio: 9 / 16,
      resolutionHeight: 1280,
      layers: [sceneLayer, shapeLayer],
    );

    final controller = container.read(editorControllerProvider.notifier);
    controller.openProject(projeto);

    final playback = PlaybackController(
      vsync: const TestVSync(),
      durationOf: () => const Duration(seconds: 10),
    );
    addTearDown(playback.dispose);

    Future<void> gravar(String nome) async {
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 300));
      await tester.runAsync(() async {
        final boundary = tester.renderObject<RenderRepaintBoundary>(
          find.byKey(const ValueKey('root_capture')),
        );
        final image = await boundary.toImage(pixelRatio: 2.0);
        final byteData = await image.toByteData(format: ui.ImageByteFormat.png);
        File('$dir/$nome.png').writeAsBytesSync(byteData!.buffer.asUint8List());
        image.dispose();
      });
    }

    Widget appWrap(Widget child) => UncontrolledProviderScope(
      container: container,
      child: RepaintBoundary(
        key: const ValueKey('root_capture'),
        child: MaterialApp(
          debugShowCheckedModeBanner: false,
          theme: ThemeData.dark().copyWith(
            scaffoldBackgroundColor: const Color(0xFF0F1116),
            canvasColor: const Color(0xFF0F1116),
          ),
          home: child,
        ),
      ),
    );

    // 1. PRINT DA ABA FORMAS (Screenshot 1: Grade 5x3 com 4 dots e vertices brancos)
    container.read(selectedLayerProvider.notifier).state = null;
    container.read(editorSessionProvider.notifier).openPanel(EditorPanel.add);
    await tester.pumpWidget(appWrap(EditorScreen(playback: playback)));
    await tester.pumpAndSettle();
    await gravar('01_add_sheet_formas');

    // 2. PRINT DA ABA OBJETO / ELEMENTO (Screenshot 2: Grade 2x2 Camera PROVAR, Grupo Vazio, Nulo, Elemento)
    await tester.tap(find.byKey(const ValueKey('add-tab-objeto')));
    await tester.pumpAndSettle();
    await gravar('02_add_sheet_objetos');

    // 3. PRINT DO MENU DA CAMADA DOCK (Screenshot 3: 5 botoes rapidos + 2x2 com Presets NEW)
    container.read(editorSessionProvider.notifier).closePanel();
    container.read(selectedLayerProvider.notifier).state = sceneLayer.id;
    await tester.pumpAndSettle();
    await gravar('03_menu_camada_dock');

    // 4. PRINT DO PAINEL DE TRANSFORMAÇÃO - MODO GIRAR (Screenshot 4: Dial com arco continuo e 117°)
    container.read(selectedLayerProvider.notifier).state = shapeLayer.id;
    container.read(editorSessionProvider.notifier).openPanel(EditorPanel.transform);
    container.read(editorSessionProvider.notifier).setTool(TransformTool.rotation);
    container.read(modoDeTransformacaoProvider.notifier).state = ModoDeTransformacao.girar;
    await tester.pumpAndSettle();
    await gravar('04_transform_rotacao');

    // 5. PRINT DO PAINEL DE TRANSFORMAÇÃO - MODO ESCALA (Screenshot 5: Largura/Altura com Fita inteira)
    container.read(editorSessionProvider.notifier).setTool(TransformTool.scale);
    container.read(modoDeTransformacaoProvider.notifier).state = ModoDeTransformacao.escalar;
    await tester.pumpAndSettle();
    await gravar('05_transform_escala');

    // 7. PRINT DA PÍLULA DA CAMADA NA TIMELINE (Screenshot 1 do usuário: pílula escura com olho e thumbnail amarela + cápsula contínua branca com chevrons e texto + agulha vertical)
    await tester.pumpWidget(appWrap(EditorScreen(playback: playback)));
    await tester.pumpAndSettle();
    container.read(selectedLayerProvider.notifier).state = shapeLayer.id;
    container.read(editorSessionProvider.notifier).openPanel(EditorPanel.transform);
    container.read(editorSessionProvider.notifier).setTool(TransformTool.position);
    playback.seek(const Duration(milliseconds: 333));
    await tester.pumpAndSettle();
    await gravar('07_pilula_camada_timeline');

    // 8. PRINT DO PAINEL DE ESCALA COM DUAS FITAS EMPILHADAS (Screenshot 2 do usuário: Largura 100,9 verde, corrente cinza, Altura 653,5 branca + Fita de Largura verde em cima e Fita de Altura branca embaixo)
    container.read(escalaTravadaProvider.notifier).state = false;
    controller.editScaleX(shapeLayer.id, const Duration(milliseconds: 333), 1.009);
    controller.editScaleY(shapeLayer.id, const Duration(milliseconds: 333), 6.535);
    container.read(selectedLayerProvider.notifier).state = shapeLayer.id;
    container.read(editorSessionProvider.notifier).openPanel(EditorPanel.transform);
    container.read(editorSessionProvider.notifier).setTool(TransformTool.scale);
    container.read(modoDeTransformacaoProvider.notifier).state = ModoDeTransformacao.escalar;
    await tester.pumpWidget(appWrap(EditorScreen(playback: playback)));
    await tester.pumpAndSettle();
    await gravar('08_transform_escala_dupla_fita');

    // 9. PRINT DO PAINEL MOVER COM ALMOFADA DE CANTOS EM L (Screenshot 3 do usuário: 4 cantos em L emoldurando X/Y verde, Z branco e área de arrasto + timecode 00:00:10 sublinhado e keyframes ◇)
    controller.editPosition(shapeLayer.id, const Duration(milliseconds: 333), const Offset(414.1, 469.7));
    container.read(selectedLayerProvider.notifier).state = shapeLayer.id;
    container.read(editorSessionProvider.notifier).openPanel(EditorPanel.transform);
    container.read(editorSessionProvider.notifier).setTool(TransformTool.position);
    container.read(modoDeTransformacaoProvider.notifier).state = ModoDeTransformacao.mover;
    playback.seek(const Duration(milliseconds: 333));
    await tester.pumpWidget(appWrap(EditorScreen(playback: playback)));
    await tester.pumpAndSettle();
    await gravar('09_mover_almofada_cantos_L');

    // 10. PRINT DA TELA INICIAL VAZIA (Screenshot 4 do usuário: Top bar com botão de exportar verde [ ↑ ], watermark Alight Motion ✕, pílula [ ✨ AI ], timeline com 00:00:00 e playhead sem folha cobrindo, FAB com anel verde e cruz verde)
    final vazioProject = VideoProject(
      id: 'projeto_vazio',
      name: 'Nome do Projeto 2',
      createdAt: DateTime.now(),
      aspectRatio: 9 / 16,
      resolutionHeight: 1280,
      layers: const [],
    );
    controller.openProject(vazioProject);
    container.read(selectedLayerProvider.notifier).state = null;
    container.read(editorSessionProvider.notifier).closePanel();
    playback.seek(Duration.zero);
    await tester.pumpWidget(appWrap(EditorScreen(playback: playback)));
    await tester.pumpAndSettle();
    await gravar('10_tela_inicial_vazia');

    // 11. PRINT DO PAINEL DE CURVAS DE GRADAÇÃO (Screenshot 5 do usuário: Top bar < Curva de gradação, botão 🔍+, trilho esquerdo < ⇋ •••, miolo Bezier com alças brancas, bolas sólidas, curva verde contínua, rodapé < Efeito Ease de Cúbico-Bezier >, trilho direito com 2 colunas de presets e badges PROVAR)
    controller.openProject(projeto);
    container.read(selectedLayerProvider.notifier).state = shapeLayer.id;
    container.read(editorSessionProvider.notifier).openCurve(LayerProp.position);
    playback.seek(const Duration(milliseconds: 333));
    await tester.pumpWidget(appWrap(EditorScreen(playback: playback)));
    await tester.pumpAndSettle();
    await gravar('11_curva_de_gradacao');

    // 12. PRINT DO PIVO — A QUINTA FACE DO PAINEL TRANSFORMAR. O ponto de
    // giro ganhou superficie propria: dois campos e uma almofada de
    // arrasto, no lugar de digitar dois numeros e torcer.
    controller.editPivot(
      shapeLayer.id,
      const Duration(milliseconds: 333),
      const Offset(120, -80),
    );
    container.read(selectedLayerProvider.notifier).state = shapeLayer.id;
    container.read(editorSessionProvider.notifier).openPanel(EditorPanel.transform);
    container.read(editorSessionProvider.notifier).setTool(TransformTool.pivot);
    container.read(modoDeTransformacaoProvider.notifier).state =
        ModoDeTransformacao.pivo;
    playback.seek(const Duration(milliseconds: 333));
    await tester.pumpWidget(appWrap(EditorScreen(playback: playback)));
    await tester.pumpAndSettle();
    await gravar('12_pivo_quinta_face');
  });
}
