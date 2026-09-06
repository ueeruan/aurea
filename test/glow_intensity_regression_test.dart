import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:aurea/src/features/editor/presentation/widgets/pixel_effect_engine.dart';

void main() {
  setUpAll(() async {
    TestWidgetsFlutterBinding.ensureInitialized();
    await PixelEffectEngine.warmUp();
    expect(PixelEffectEngine.ready, isTrue);
  });
  testWidgets('glow preserva threshold e responde a intensidade', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(300, 300);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final container = ProviderContainer();
    addTearDown(container.dispose);
    final time = ValueNotifier(Duration.zero);
    addTearDown(time.dispose);
    final key = GlobalKey();
    Future<Uint8List> render(Color color, double intensity) async {
      final layer = ShapeLayer(
        name: 'amostra',
        startTime: Duration.zero,
        duration: const Duration(seconds: 5),
        position: AnimatedOffset(const Offset(150, 150)),
        contents: [
          ShapePath(primitive: ShapePrimitive.rectangle, width: 60, height: 60),
          ShapeFill(color: color),
        ],
        effects: intensity == 0
            ? []
            : [
                EffectInstance(
                  type: EffectType.lightGlow,
                  color: const Color(0xFFFF8877),
                  params: {
                    'threshold': AnimatedDouble(80),
                    'raio': AnimatedDouble(20),
                    'intensity': AnimatedDouble(intensity),
                    'piramide': AnimatedDouble(2),
                    'mesclagem': AnimatedDouble(1),
                  },
                ),
              ],
      );
      container
          .read(editorControllerProvider.notifier)
          .openProject(
            VideoProject(
              name: 'teste',
              createdAt: DateTime(2026),
              aspectRatio: 1,
              resolutionHeight: 300,
              layers: [layer],
            ),
          );
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: container,
          child: MaterialApp(
            home: Material(
              color: Colors.black,
              child: RepaintBoundary(
                key: key,
                child: CompositionView(
                  time: time,
                  videos: VideoLayerManager(),
                  selectedId: null,
                  exporting: true,
                ),
              ),
            ),
          ),
        ),
      );
      await tester.pump();
      await tester.pump();
      return (await tester.runAsync(() async {
        final image =
            await (key.currentContext!.findRenderObject()
                    as RenderRepaintBoundary)
                .toImage();
        final bytes = await image.toByteData(
          format: ui.ImageByteFormat.rawRgba,
        );
        image.dispose();
        return bytes!.buffer.asUint8List();
      }))!;
    }

    double energy(Uint8List bytes) {
      var sum = 0.0;
      for (var y = 80; y < 220; y++) {
        for (var x = 80; x < 220; x++) {
          if (x >= 120 && x < 180 && y >= 120 && y < 180) continue;
          final i = (y * 300 + x) * 4;
          sum += bytes[i] + bytes[i + 1] + bytes[i + 2];
        }
      }
      return sum;
    }

    final dark = await render(const Color(0xFF222222), 0);
    final darkGlow = await render(const Color(0xFF222222), 100);
    expect(energy(darkGlow), closeTo(energy(dark), 20));
    final low = energy(await render(Colors.white, 10));
    final high = energy(await render(Colors.white, 100));
    expect(high, greaterThan(low * 1.3));
    expect(tester.takeException(), isNull);
  });
}
