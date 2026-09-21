import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/presentation/widgets/passe_de_cor.dart';
import 'package:aurea/src/features/editor/presentation/widgets/soft_glow_pass.dart';

import 'apoio/print_da_ui.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUpAll(() async {
    await carregarFontesReais();
    await MotorSapphire.carregar(SoftGlowPass.asset);
    expect(MotorSapphire.programa(SoftGlowPass.asset), isNotNull);
  });
  for (final mode in [1.0, 2.0]) {
    testWidgets('glow $mode has a smooth symmetric halo outside a small ring', (
      tester,
    ) async {
      final key = GlobalKey();
      Future<Uint8List> render(double exposure, Color color) async {
        await tester.pumpWidget(
          Directionality(
            textDirection: TextDirection.ltr,
            child: Center(
              child: RepaintBoundary(
                key: key,
                child: SizedBox(
                  width: 256,
                  height: 256,
                  child: ColoredBox(
                    color: Colors.black,
                    child: Center(
                      child: SizedBox(
                        width: 80,
                        height: 80,
                        child: SoftGlowPass(
                          values: [mode, 80, exposure, 0, .2, 0],
                          color: Colors.white,
                          child: CustomPaint(painter: _Ring(color)),
                        ),
                      ),
                    ),
                  ),
                ),
              ),
            ),
          ),
        );
        await tester.pump();
        final image = tester
            .renderObject<RenderRepaintBoundary>(find.byKey(key))
            .toImageSync();
        final data = await tester.runAsync(
          () => image.toByteData(format: ui.ImageByteFormat.rawRgba),
        );
        image.dispose();
        return Uint8List.fromList(data!.buffer.asUint8List());
      }

      final raw = await render(0, Colors.greenAccent);
      final glow = await render(1, Colors.greenAccent);
      int green(Uint8List pixels, int x, int y) =>
          pixels[(y * 256 + x) * 4 + 1];
      final around = [
        for (var i = 0; i < 16; i++)
          green(
            glow,
            (127.5 + 52 * math.cos(i * math.pi / 8)).round(),
            (127.5 + 52 * math.sin(i * math.pi / 8)).round(),
          ),
      ];
      expect(green(raw, 180, 128), 0);
      expect(
        around.reduce(math.min),
        greaterThan(2),
        reason: 'halo clipped to source box',
      );
      expect(
        around.reduce(math.max) - around.reduce(math.min),
        lessThan(12),
        reason: 'displaced ring copies must not create angular ghosts',
      );
      expect(green(glow, 156, 128), greaterThanOrEqualTo(green(raw, 156, 128)));
      final changed = await render(1, const Color(0xffff0000));
      expect(
        green(changed, 156, 128),
        lessThan(10),
        reason: 'source capture must update in the same frame',
      );
      await tester.pumpWidget(const SizedBox.shrink());
    });
  }
}

class _Ring extends CustomPainter {
  const _Ring(this.color);
  final Color color;
  @override
  void paint(Canvas canvas, Size size) => canvas.drawCircle(
    size.center(Offset.zero),
    28,
    Paint()
      ..color = color
      ..style = PaintingStyle.stroke
      ..strokeWidth = 8,
  );
  @override
  bool shouldRepaint(_Ring old) => old.color != color;
}
