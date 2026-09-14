import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/ui/editor_layout.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/estudio/folha_de_animacao.dart';
import 'package:aurea/src/features/editor/presentation/estudio/estado_do_estudio.dart';
import 'package:aurea/src/features/editor/presentation/estudio/folha_de_camera.dart';
import 'package:aurea/src/features/editor/presentation/estudio/folha_de_exportar.dart';
import 'package:aurea/src/features/editor/presentation/estudio/folha_de_luzes.dart';
import 'package:aurea/src/features/editor/presentation/widgets/campo_de_valor.dart';
import 'package:aurea/src/features/editor/presentation/widgets/editor_de_curva.dart';
import 'package:aurea/src/features/editor/presentation/estudio/vista_da_cena.dart';
import 'package:aurea/src/features/editor/domain/camera3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/export/domain/export_settings.dart';
import 'package:aurea/src/features/export/presentation/export_video_screen.dart';

void main() {
  testWidgets(
    'camera viewport preserves the project frame in portrait and landscape',
    (tester) async {
      final c = ProviderContainer();
      addTearDown(c.dispose);
      final e = c.read(editorControllerProvider.notifier);
      e.addScene3DLayer(Duration.zero);
      final id = c.read(editorControllerProvider).layers.first.id;
      final nav = NavegacaoDaVista();
      addTearDown(nav.dispose);
      for (final aspect in [16 / 9, 9 / 16, 1.0]) {
        e.openProject(
          c.read(editorControllerProvider).copyWith(aspectRatio: aspect),
        );
        await tester.pumpWidget(
          UncontrolledProviderScope(
            container: c,
            child: MaterialApp(
              home: Scaffold(
                body: SizedBox(
                  width: 390,
                  height: 600,
                  child: VistaDaCena(
                    layerId: id,
                    navegacao: nav,
                    tempo: Duration.zero,
                    aoTocarVazio: () {},
                  ),
                ),
              ),
            ),
          ),
        );
        final size = tester.getSize(
          find.byKey(const ValueKey('scene-camera-frame')),
        );
        expect(size.aspectRatio, closeTo(aspect, 1e-6));
        expect(size.width, lessThanOrEqualTo(390));
        expect(size.height, lessThanOrEqualTo(600));
        expect(tester.takeException(), isNull);
      }
      nav.verVista(SceneView.top);
      await tester.pump();
      expect(find.byKey(const ValueKey('scene-camera-frame')), findsNothing);
    },
  );

  test('layer parenting preserves the independent camera parent', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addScene3DLayer(Duration.zero);
    final id = c.read(editorControllerProvider).layers.first.id;
    e.addNullLayer(Duration.zero);
    final cameraParent = c.read(selectedLayerProvider)!;
    e.addNullLayer(Duration.zero);
    final layerParent = c.read(selectedLayerProvider)!;
    e.setSceneCameraCompParent(id, cameraParent);
    e.linkProperty(id, LayerProp.parent, layerParent, Duration.zero);
    read() => c.read(editorControllerProvider).layerById(id) as Scene3DLayer;
    expect(read().cameraParentLayerId, cameraParent);
    e.unlinkProperty(id, LayerProp.parent);
    expect(read().cameraParentLayerId, cameraParent);
  });

  testWidgets('camera number fields edit the selected camera at its keyframe', (
    tester,
  ) async {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addScene3DLayer(const Duration(seconds: 3));
    final scene = c.read(editorControllerProvider).layers.first as Scene3DLayer;
    final cameraId = e.duplicateSceneCamera(scene.id, scene.camera.id);
    c.read(cameraSelecionadaProvider.notifier).state = cameraId;
    const global = Duration(seconds: 4);
    e.toggleSceneCameraKeyframe(scene.id, cameraId, PropDaCamera.posX, global);
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          home: Scaffold(
            body: SingleChildScrollView(
              child: FolhaDeCamera(layerId: scene.id, tempo: global),
            ),
          ),
        ),
      ),
    );
    final field = tester
        .widgetList<CampoDeValor>(find.byType(CampoDeValor))
        .first;
    field.aoDigitar!(123);
    await tester.pump();
    final updated =
        c.read(editorControllerProvider).layers.first as Scene3DLayer;
    final selected = updated.allCameras.firstWhere((c) => c.id == cameraId);
    expect(selected.posX.valueAt(const Duration(seconds: 1)), 123);
    expect(selected.posX.keyframes, hasLength(1));
    expect(
      updated.camera.posX.valueAt(const Duration(seconds: 1)),
      scene.camera.posX.valueAt(const Duration(seconds: 1)),
    );
    expect(tester.takeException(), isNull);
  });

  testWidgets('each light including a second spot is independently editable', (
    tester,
  ) async {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addScene3DLayer(Duration.zero);
    final id = c.read(editorControllerProvider).layers.first.id;
    e.addSceneLight(id, Light3DKind.spot);
    e.addSceneLight(id, Light3DKind.spot);
    read() =>
        (c.read(editorControllerProvider).layerById(id) as Scene3DLayer).scene;
    final before = read();
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          home: Scaffold(
            body: SingleChildScrollView(child: FolhaDeLuzes(layerId: id)),
          ),
        ),
      ),
    );
    final menu = tester.widget<DropdownButton<String>>(
      find.byKey(const ValueKey('scene-light-selection')),
    );
    expect(menu.items, hasLength(before.lights.length));
    menu.onChanged!(before.lights.last.id);
    await tester.pump();
    final cone = tester.widgetList<Slider>(find.byType(Slider)).last;
    expect(cone.onChanged, isNotNull);
    cone.onChanged!(31);
    await tester.pump();
    expect(read().lights.last.coneDegrees, 31);
    expect(read().lights[before.lights.length - 2].coneDegrees, 45);
    expect(read().ambient, before.ambient);
    expect(tester.takeException(), isNull);
  });

  testWidgets('3D export forwards selected format, resolution and frame rate', (
    tester,
  ) async {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addScene3DLayer(Duration.zero);
    final id = c.read(editorControllerProvider).layers.first.id;
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          home: Builder(
            builder: (context) => Scaffold(
              body: TextButton(
                onPressed: () => showModalBottomSheet<void>(
                  context: context,
                  isScrollControlled: true,
                  builder: (_) => SingleChildScrollView(
                    child: FolhaDeExportar(layerId: id),
                  ),
                ),
                child: const Text('open'),
              ),
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.text('open'));
    await tester.pumpAndSettle();
    final menus = tester
        .widgetList<DropdownButton<String>>(find.byType(DropdownButton<String>))
        .toList();
    menus[0].onChanged!('720p (HD)');
    menus[1].onChanged!('24 FPS');
    await tester.tap(find.text('Sequência PNG'));
    await tester.pump();
    await tester.tap(find.text('Exportar'));
    await tester.pumpAndSettle();
    final screen = tester.widget<ExportVideoScreen>(
      find.byType(ExportVideoScreen),
    );
    expect(screen.settings.size, ExportSize.p720);
    expect(screen.settings.fps, 24);
    expect(screen.settings.format, ExportFormat.pngSequence);
    expect(tester.takeException(), isNull);
  });

  test('opening tools preserves preview size and total layout budget', () {
    for (final h in [320.0, 568.0, 667.0, 844.0]) {
      final normal = EditorLayoutMetrics.solve(
        totalHeight: h,
        previewFraction: .4,
        sheetFraction: .4,
        sheetVisible: false,
      );
      final tool = EditorLayoutMetrics.solve(
        totalHeight: h,
        previewFraction: .4,
        sheetFraction: .4,
        focusedLayer: true,
      );
      expect(tool.preview, normal.preview);
      expect(tool.total, closeTo(h, 1e-6));
    }
  });
  testWidgets('3D animation add and delete act on the selected Z track', (
    tester,
  ) async {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addScene3DLayer(Duration.zero);
    final scene = c.read(editorControllerProvider).layers.first as Scene3DLayer;
    final nodeId = e.addSceneNull(scene.id);
    final node = (c.read(editorControllerProvider).layers.first as Scene3DLayer)
        .scene
        .nodeById(nodeId)!;
    c.read(noSelecionadoProvider.notifier).state = node.id;
    final playback = PlaybackController(
      vsync: tester,
      durationOf: () => const Duration(seconds: 10),
    );
    addTearDown(playback.dispose);
    playback.seek(const Duration(seconds: 1));
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          home: Scaffold(
            body: FolhaDeAnimacao(layerId: scene.id, playback: playback),
          ),
        ),
      ),
    );
    final dropdown = tester.widget<DropdownButton<PropDoNo>>(
      find.byType(DropdownButton<PropDoNo>),
    );
    dropdown.onChanged!(PropDoNo.z);
    await tester.pump();
    await tester.tap(find.byKey(const ValueKey('scene-animation-add')));
    await tester.pump();
    readNode() =>
        (c.read(editorControllerProvider).layers.first as Scene3DLayer).scene
            .nodeById(node.id)!;
    expect(readNode().z.keyframes, hasLength(1));
    expect(readNode().x.keyframes, isEmpty);
    await tester.tap(find.byKey(const ValueKey('scene-animation-delete')));
    await tester.pump();
    expect(readNode().z.keyframes, isEmpty);
    e.toggleSceneNodeKeyframe(scene.id, node.id, PropDoNo.z, Duration.zero);
    e.toggleSceneNodeKeyframe(
      scene.id,
      node.id,
      PropDoNo.z,
      const Duration(seconds: 2),
    );
    await tester.pump();
    await tester.tap(find.byKey(const ValueKey('scene-animation-curve')));
    await tester.pumpAndSettle();
    expect(find.byType(EditorDeCurva).hitTestable(), findsOneWidget);
    expect(c.read(curvaEmEdicaoProvider), isNotNull);
    await tester.tap(find.bySemanticsLabel('Inverter a curva'));
    await tester.pump();
    expect(tester.takeException(), isNull);
    await tester.tap(find.bySemanticsLabel('Fechar a curva'));
    await tester.pumpAndSettle();
    expect(find.byType(EditorDeCurva), findsNothing);
    expect(c.read(curvaEmEdicaoProvider), isNull);
    expect(find.byType(FolhaDeAnimacao), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}
