import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/posterize_time.dart';
import 'package:aurea/src/features/editor/domain/text_anim.dart';
import 'package:aurea/src/features/editor/presentation/widgets/animated_text.dart';
import 'package:aurea/src/features/editor/presentation/widgets/motion_tile_pass.dart';
import 'package:aurea/src/features/editor/presentation/widgets/owned_video_frame.dart';

import 'apoio/print_da_ui.dart';

Future<Uint8List> paintPixels(
  CustomPainter painter, {
  int width = 600,
  int height = 240,
}) async {
  final recorder = ui.PictureRecorder();
  final canvas = Canvas(recorder)..translate(20, 60);
  painter.paint(canvas, Size(width.toDouble(), height.toDouble()));
  final picture = recorder.endRecording();
  final image = await picture.toImage(width, height);
  final data = await image.toByteData(format: ui.ImageByteFormat.rawRgba);
  image.dispose();
  picture.dispose();
  return Uint8List.fromList(data!.buffer.asUint8List());
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUpAll(carregarFontesReais);

  test('posterize is idempotent at rounded microsecond frame boundaries', () {
    for (final fps in [1.0, 6.0, 12.0, 24.0, 29.97, 30.0]) {
      for (var n = 0; n < 100; n++) {
        final t = Duration(microseconds: (n * 1e6 / fps).round());
        expect(quantizarTempo(t, fps), t);
        expect(quantizarTempo(quantizarTempo(t, fps), fps), t);
      }
    }
  });

  test('Seu texto: the second word remains visible after entrance, including reverse scrub', () async {
    final layer = TextLayer(
      name: 't',
      text: 'Seu texto',
      fontSize: 64,
      color: Colors.white,
      startTime: Duration.zero,
      duration: const Duration(seconds: 6),
      anims: [TextAnim(specId: 'fade', slot: TextAnimSlot.entrada)],
    );
    final late = await paintPixels(
      painterDoTextoAnimado(layer, const Duration(seconds: 4)),
    );
    await paintPixels(
      painterDoTextoAnimado(layer, const Duration(milliseconds: 90)),
    );
    final repeat = await paintPixels(
      painterDoTextoAnimado(layer, const Duration(seconds: 4)),
    );
    expect(repeat, orderedEquals(late));
    // "texto" begins beyond x=140 at this font size. The old renderer
    // painted every word at zero, so no pixels survived this region.
    var secondWord = 0;
    for (var y = 0; y < 240; y++) {
      for (var x = 165; x < 340; x++) {
        if (late[(y * 600 + x) * 4 + 3] > 100) secondWord++;
      }
    }
    expect(secondWord, greaterThan(600));
  });

  testWidgets(
    'Motion Tile 25% raster covers every corner after the actual widget transform',
    (tester) async {
      final capture = GlobalKey();
      final fx = EffectInstance(type: EffectType.motionTile);
      await tester.pumpWidget(
        MaterialApp(
          home: Center(
            child: RepaintBoundary(
              key: capture,
              child: SizedBox(
                width: 160,
                height: 240,
                child: ClipRect(
                  child: Stack(
                    clipBehavior: Clip.none,
                    children: [
                      Positioned(
                        left: 80,
                        top: 120,
                        child: FractionalTranslation(
                          translation: const Offset(-.5, -.5),
                          child: Transform.scale(
                            scale: .25,
                            child: MotionTilePass(
                              effect: fx,
                              time: Duration.zero,
                              escalaX: .25,
                              escalaY: .25,
                              posicao: const Offset(80, 120),
                              composicao: const Size(160, 240),
                              child: const SizedBox(
                                width: 160,
                                height: 240,
                                child: ColoredBox(color: Color(0xFFED7834)),
                              ),
                            ),
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ),
          ),
        ),
      );
      await tester.pumpAndSettle();
      await tester.runAsync(() async {
        final boundary =
            capture.currentContext!.findRenderObject() as RenderRepaintBoundary;
        final image = await boundary.toImage();
        final data = (await image.toByteData(
          format: ui.ImageByteFormat.rawRgba,
        ))!.buffer.asUint8List();
        for (final p in [
          const Offset(1, 1),
          const Offset(158, 1),
          const Offset(1, 238),
          const Offset(158, 238),
        ]) {
          final i = (p.dy.toInt() * 160 + p.dx.toInt()) * 4;
          expect(data[i + 3], 255, reason: 'empty corner $p');
          expect(data[i], greaterThan(220));
        }
        image.dispose();
      });
    },
  );

  test('RGB production painter combines independent temporal images', () async {
    Future<ui.Image> solid(Color color) async {
      final r = ui.PictureRecorder();
      Canvas(r)
          .drawRect(const Rect.fromLTWH(0, 0, 8, 8), Paint()..color = color);
      final p = r.endRecording();
      final image = await p.toImage(8, 8);
      p.dispose();
      return image;
    }

    final images = await Future.wait([
      solid(const Color(0xFFC80A14)),
      solid(const Color(0xFF1EB428)),
      solid(const Color(0xFF323CC8)),
    ]);
    final r = ui.PictureRecorder();
    final shader = (await RgbFramesPainter.prepare()).fragmentShader();
    RgbFramesPainter(images, shader).paint(Canvas(r), const Size(8, 8));
    final p = r.endRecording();
    final image = await p.toImage(8, 8);
    final data = (await image.toByteData(format: ui.ImageByteFormat.rawRgba))!
        .buffer
        .asUint8List();
    expect(data[0], closeTo(200, 2));
    expect(data[1], closeTo(180, 2));
    expect(data[2], closeTo(200, 2));
    expect(data[3], 255);
    image.dispose();
    p.dispose();
    shader.dispose();
    for (final img in images) {
      img.dispose();
    }
  });
}
