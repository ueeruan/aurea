import 'dart:io';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/presentation/am/am_widgets.dart';
import 'package:aurea/src/features/editor/presentation/am/transform_panel.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_hierarchy_test.dart' show openEditor;

void main() {
  setUpAll(() async {
    for (final family in [
      'Aurea Motion Sans',
      'Roboto',
      '.SF Pro Text',
      '.SF Pro Display',
      '.SF UI Text',
      '.SF UI Display',
    ]) {
      await (FontLoader(family)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
    await (FontLoader('packages/cupertino_icons/CupertinoIcons')..addFont(
          rootBundle.load('packages/cupertino_icons/assets/CupertinoIcons.ttf'),
        ))
        .load();
    await (FontLoader(
      'MaterialIcons',
    )..addFont(rootBundle.load('fonts/MaterialIcons-Regular.otf'))).load();
  });
  for (final size in [const Size(375, 667), const Size(430, 844)]) {
    testWidgets('roomy transform, linked rulers and curve menu at $size', (
      tester,
    ) async {
      final c = await openEditor(tester, size: size);
      final editor = c.read(editorControllerProvider.notifier);
      final id = c.read(editorControllerProvider).layers.first.id;
      c.read(selectedLayerProvider.notifier).state = id;
      await tester.pumpAndSettle();
      final preview = tester.getRect(find.byType(PreviewStage));
      await tester.tap(find.text('Mover e\ntransf.'));
      await tester.pumpAndSettle();
      Future<void> capture(String name) async {
        if (size.width != 430 ||
            !const bool.fromEnvironment('AUREA_CAPTURE_TRANSFORM')) {
          return;
        }
        await tester.runAsync(() async {
          final boundary = tester.renderObject<RenderRepaintBoundary>(
            find.byKey(const ValueKey('editor-capture')),
          );
          final image = await boundary.toImage(pixelRatio: 2);
          final bytes = (await image.toByteData(
            format: ui.ImageByteFormat.png,
          ))!.buffer.asUint8List();
          final file = File('output/transform-ui/$name.png');
          await file.parent.create(recursive: true);
          await file.writeAsBytes(bytes);
          image.dispose();
        });
      }

      final pad = find.byKey(const ValueKey('position-drag-pad'));
      expect(tester.getSize(pad).width, greaterThan(size.width - 145));
      expect(tester.getRect(find.byType(PreviewStage)), preview);
      await tester.tap(find.byTooltip('Opções de transformação'));
      await tester.pumpAndSettle();
      final autoBefore = c.read(autoKeyframeProvider);
      await tester.tap(
        find.byWidgetPredicate(
          (widget) =>
              widget is CheckedPopupMenuItem<String> && widget.value == 'auto',
        ),
      );
      await tester.pumpAndSettle();
      expect(c.read(autoKeyframeProvider), !autoBefore);
      c.read(autoKeyframeProvider.notifier).state = false;
      tester
          .widget<TransformPanel>(find.byType(TransformPanel))
          .onToolChanged(TransformTool.pivot);
      await tester.pumpAndSettle();
      expect(
        tester.getSize(find.byKey(const ValueKey('pivot-drag-pad'))).height,
        greaterThan(100),
      );
      await capture('pivot');
      tester
          .widget<TransformPanel>(find.byType(TransformPanel))
          .onToolChanged(TransformTool.scale);
      await tester.pumpAndSettle();
      final width = find.byKey(const ValueKey('scale-width-ruler'));
      final height = find.byKey(const ValueKey('scale-height-ruler'));
      expect(width.hitTestable(), findsOneWidget);
      expect(height.hitTestable(), findsOneWidget);
      tester.widget<AmTickRuler>(height).onChanged(125);
      await tester.pumpAndSettle();
      var layer = c.read(editorControllerProvider).layerById(id)!;
      expect(layer.scaleX.base, 1.25);
      expect(layer.scaleY.base, 1.25);
      await capture('scale');
      editor.toggleKeyframe(id, Duration.zero, LayerProp.scale);
      editor.toggleKeyframe(id, const Duration(seconds: 2), LayerProp.scale);
      editor.setSegmentEase(id, LayerProp.scale, Duration.zero, Easing.bounce);
      layer = c.read(editorControllerProvider).layerById(id)!;
      expect(layer.scaleX.valueAt(Duration.zero), 1.25);
      expect(layer.scaleY.valueAt(Duration.zero), 1.25);
      await tester.pumpAndSettle();
      await tester.tap(find.byTooltip('Editar curva da propriedade'));
      await tester.pumpAndSettle();
      final graph = tester.getRect(
        find.byKey(const ValueKey('curve-edit-area')),
      );
      expect(graph.height, greaterThan(140));
      expect(graph.width, greaterThan(size.width - 130));
      expect(tester.getRect(find.byType(PreviewStage)), preview);
      await tester.runAsync(
        () => Future<void>.delayed(const Duration(milliseconds: 50)),
      );
      await tester.pumpAndSettle();
      await capture('curve');
      await tester.tap(find.byTooltip('Opções da curva'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Copiar curva'));
      await tester.pumpAndSettle();
      expect(tester.takeException(), isNull);
    });
  }
}
