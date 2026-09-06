import 'dart:io';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/am_timeline.dart';
import 'package:aurea/src/features/editor/presentation/am/am_widgets.dart';
import 'package:aurea/src/features/editor/presentation/am/curve_panel.dart';
import 'package:aurea/src/features/editor/presentation/am/layer_menu.dart';
import 'package:aurea/src/features/editor/presentation/am/param_sheet_shell.dart'
    show ParamSheetShell;
import 'package:aurea/src/features/editor/presentation/am/property_keyframe_context.dart';
import 'package:aurea/src/features/editor/presentation/am/transform_panel.dart';
import 'package:aurea/src/features/editor/presentation/am/scene3d_studio.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

class _MemoryProjects extends ProjectsController {
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
    overrides: [projectsControllerProvider.overrideWith(_MemoryProjects.new)],
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

void main() {
  setUpAll(() async {
    TestWidgetsFlutterBinding.ensureInitialized();
    for (final family in [
      'Aurea Motion Sans',
      '.SF Pro Text',
      '.SF Pro Display',
      '.SF UI Text',
      '.SF UI Display',
    ]) {
      final font = FontLoader(family)
        ..addFont(rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'));
      await font.load();
    }
    final icons = FontLoader('packages/cupertino_icons/CupertinoIcons')
      ..addFont(
        rootBundle.load('packages/cupertino_icons/assets/CupertinoIcons.ttf'),
      );
    await icons.load();
    final materialIcons = FontLoader('MaterialIcons')
      ..addFont(rootBundle.load('fonts/MaterialIcons-Regular.otf'));
    await materialIcons.load();
  });
  for (final size in [const Size(375, 667), const Size(430, 932)]) {
    testWidgets(
      'select, transform, curve, back preserves context at ${size.width}',
      (tester) async {
        final c = await openEditor(tester, size: size);
        expect(find.byType(LayerToolsDock), findsNothing);
        final layer = c.read(editorControllerProvider).layers.first;
        // Um toque na barra já revela todas as categorias aplicáveis.
        final bar = find
            .descendant(
              of: find.byType(AmTimeline),
              matching: find.text(layer.name),
            )
            .first;
        await tester.tap(bar);
        await tester.pumpAndSettle();
        expect(c.read(selectedLayerProvider), layer.id);
        expect(find.byType(LayerToolsDock), findsOneWidget);
        expect(find.byType(BottomSheet), findsNothing);
        await capture(tester, 'selected-${size.width.toInt()}');
        expect(tester.takeException(), isNull, reason: 'selected layer');
        await tester.tap(find.text('Mover e\ntransf.'));
        await tester.pumpAndSettle();
        final transform = tester.widget<TransformPanel>(
          find.byType(TransformPanel),
        );
        expect(transform.tool, TransformTool.position);
        expect(find.text('Transformar · Posição'), findsOneWidget);
        expect(
          tester
              .widget<Text>(find.byKey(const ValueKey('editor-context')))
              .data,
          contains(layer.name),
        );
        transform.playback.seek(const Duration(seconds: 1));
        final controller = c.read(editorControllerProvider.notifier);
        controller.toggleKeyframe(layer.id, Duration.zero, LayerProp.position);
        controller.toggleKeyframe(
          layer.id,
          const Duration(seconds: 2),
          LayerProp.position,
        );
        await tester.pumpAndSettle();
        await capture(tester, 'property-${size.width.toInt()}');
        expect(tester.takeException(), isNull, reason: 'transform property');
        await tester.tap(find.byTooltip('Editar curva da propriedade'));
        await tester.pumpAndSettle();
        expect(find.byType(CurvePanel), findsOneWidget);
        expect(tester.takeException(), isNull, reason: 'curve panel');
        final project = c.read(editorControllerProvider);
        await tester.binding.handlePopRoute();
        await tester.pumpAndSettle();
        expect(find.byType(TransformPanel), findsOneWidget);
        expect(transform.playback.time.value, const Duration(seconds: 1));
        await tester.tap(find.byKey(const ValueKey('editor-back')));
        await tester.pumpAndSettle();
        expect(find.byType(LayerToolsDock), findsOneWidget);
        expect(c.read(selectedLayerProvider), layer.id);
        expect(identical(c.read(editorControllerProvider), project), isTrue);
        await tester.binding.handlePopRoute();
        await tester.pumpAndSettle();
        expect(c.read(selectedLayerProvider), isNull);
        expect(find.byType(EditorScreen), findsOneWidget);
        await tester.binding.handlePopRoute();
        await tester.pumpAndSettle();
        expect(find.text('abrir projeto'), findsOneWidget);
        expect(tester.takeException(), isNull);
      },
    );
  }

  testWidgets(
    'back closes persistent sheet only; changing layer invalidates recents',
    (tester) async {
      final c = await openEditor(tester);
      final layers = c.read(editorControllerProvider).layers;
      c.read(selectedLayerProvider.notifier).state = layers.first.id;
      await tester.pumpAndSettle();
      void sheet(String title) => showParamSheet(
        tester.element(find.byType(EditorScreen)),
        title: title,
        builder: (_) => Text('painel $title'),
      );
      sheet('Detalhes da camada com um nome muito longo para caber inteiro');
      await tester.pumpAndSettle();
      expect(find.byType(ParamSheetShell), findsOneWidget);
      await tester.binding.handlePopRoute();
      await tester.pumpAndSettle();
      expect(find.byType(ParamSheetShell), findsNothing);
      expect(c.read(selectedLayerProvider), layers.first.id);
      expect(find.byType(EditorScreen), findsOneWidget);
      sheet('Primeira camada');
      await tester.pumpAndSettle();
      expect(RecentSheets.instance.items, isNotEmpty);
      c.read(selectedLayerProvider.notifier).state = layers.last.id;
      await tester.pumpAndSettle();
      expect(RecentSheets.instance.items, isEmpty);
      expect(find.byType(ParamSheetShell), findsNothing);
      expect(find.byType(LayerToolsDock), findsOneWidget);
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets(
    'selection change exits old property and multi back keeps primary',
    (tester) async {
      final c = await openEditor(tester);
      final layers = c.read(editorControllerProvider).layers;
      c.read(selectedLayerProvider.notifier).state = layers.first.id;
      await tester.pumpAndSettle();
      await tester.tap(find.text('Mover e\ntransf.'));
      await tester.pumpAndSettle();
      c.read(selectedLayerProvider.notifier).state = layers.last.id;
      await tester.pumpAndSettle();
      expect(find.byType(TransformPanel), findsNothing);
      c.read(multiSelectProvider.notifier).state = layers
          .map((l) => l.id)
          .toSet();
      await tester.pumpAndSettle();
      expect(find.byType(LayerToolsDock), findsNothing);
      await tester.binding.handlePopRoute();
      await tester.pumpAndSettle();
      expect(c.read(multiSelectProvider), isEmpty);
      expect(c.read(selectedLayerProvider), layers.last.id);
      expect(find.byType(LayerToolsDock), findsOneWidget);
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets('all transform properties fit a small phone', (tester) async {
    final c = await openEditor(tester, size: const Size(375, 667));
    c.read(selectedLayerProvider.notifier).state = c
        .read(editorControllerProvider)
        .layers
        .first
        .id;
    await tester.pumpAndSettle();
    await tester.tap(find.text('Mover e\ntransf.'));
    await tester.pumpAndSettle();
    for (final tool in TransformTool.values) {
      tester
          .widget<TransformPanel>(find.byType(TransformPanel))
          .onToolChanged(tool);
      await tester.pumpAndSettle();
      expect(tester.takeException(), isNull, reason: tool.name);
    }
  });

  testWidgets(
    '3D and Grid open directly from the dock without popping editor',
    (tester) async {
      final c = await openEditor(tester);
      final editor = c.read(editorControllerProvider.notifier);
      editor.addNullLayer(Duration.zero);
      await tester.pumpAndSettle();
      await tester.tap(find.text('Clonar'));
      await tester.pumpAndSettle();
      expect(find.byType(ParamSheetShell), findsOneWidget);
      closeParamSheet(tester.element(find.byType(ParamSheetShell)));
      await tester.pumpAndSettle();
      expect(find.byType(EditorScreen), findsOneWidget);
      editor.addScene3DLayer(Duration.zero);
      await tester.pumpAndSettle();
      await tester.tap(find.text('Cena 3D'));
      await tester.pumpAndSettle();
      // A cena 3D abre o ESTUDIO — e la que ela se edita.
      expect(find.byType(Scene3DStudio), findsOneWidget);
      await tester.binding.handlePopRoute();
      await tester.pumpAndSettle();
      expect(find.byType(Scene3DStudio), findsNothing);
      expect(find.byType(EditorScreen), findsOneWidget);
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets(
    'property navigation uses Y keys and layer-local time, not other properties',
    (tester) async {
      final layer = ShapeLayer(
        name: 'Alvo',
        startTime: const Duration(seconds: 5),
        duration: const Duration(seconds: 4),
        scaleY: AnimatedDouble(1, const [
          Keyframe(time: Duration.zero, value: 1.0),
          Keyframe(time: Duration(seconds: 2), value: 2.0),
        ]),
        rotation: AnimatedDouble(0, const [
          Keyframe(time: Duration(seconds: 1), value: 90.0),
        ]),
      );
      Duration? target;
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: PropertyKeyframeContext(
              layer: layer,
              prop: LayerProp.scale,
              time: const Duration(seconds: 6),
              onSeek: (t) => target = t,
            ),
          ),
        ),
      );
      expect(find.text('2 keyframes · entre marcas'), findsOneWidget);
      await tester.tap(
        find.byKey(const ValueKey('previous-property-keyframe')),
      );
      expect(target, const Duration(seconds: 5));
      await tester.tap(find.byKey(const ValueKey('next-property-keyframe')));
      expect(target, const Duration(seconds: 7));
      expect(keyframeTimesForProp(layer, LayerProp.scale), {0, 2000000});
    },
  );
}
