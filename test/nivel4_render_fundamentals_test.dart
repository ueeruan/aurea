import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart' show RenderRepaintBoundary;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/mask.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/blend_mask.dart';
import 'package:aurea/src/features/editor/presentation/widgets/masked_box.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';

class _RawFrame {
  const _RawFrame(this.width, this.bytes);

  final int width;
  final Uint8List bytes;

  int alphaAt(int x, int y) => bytes[(y * width + x) * 4 + 3];

  Color colorAt(int x, int y) {
    final offset = (y * width + x) * 4;
    return Color.fromARGB(
      bytes[offset + 3],
      bytes[offset],
      bytes[offset + 1],
      bytes[offset + 2],
    );
  }
}

Future<_RawFrame> _capture(
  WidgetTester tester,
  Widget child, {
  Size size = const Size(64, 64),
}) async {
  final key = GlobalKey();
  tester.view.physicalSize = size;
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);

  await tester.pumpWidget(
    MaterialApp(
      debugShowCheckedModeBanner: false,
      home: Material(
        type: MaterialType.transparency,
        child: Align(
          alignment: Alignment.topLeft,
          child: RepaintBoundary(
            key: key,
            child: SizedBox.fromSize(size: size, child: child),
          ),
        ),
      ),
    ),
  );
  await tester.pump();
  await tester.pump();

  late ui.Image image;
  late ByteData data;
  await tester.runAsync(() async {
    final boundary =
        key.currentContext!.findRenderObject() as RenderRepaintBoundary;
    image = await boundary.toImage(pixelRatio: 1);
    data = (await image.toByteData(format: ui.ImageByteFormat.rawRgba))!;
  });
  final bytes = Uint8List.fromList(
    data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes),
  );
  final frame = _RawFrame(image.width, bytes);
  image.dispose();
  return frame;
}

MaskSpec _mask({
  required BezierPath path,
  required MaskMode mode,
  required double opacity,
  required double feather,
}) {
  return MaskSpec(
    path: path.build(),
    closed: path.closed,
    mode: mode,
    inverted: false,
    opacity: opacity,
    feather: feather,
    expansion: 0,
  );
}

Widget _masked(List<MaskSpec> specs) => MaskedBox(
  specs: specs,
  child: const ColoredBox(color: Color(0xFFFFFFFF)),
);

void main() {
  testWidgets('isolate srcOver contem dstIn sem apagar o fundo externo', (
    tester,
  ) async {
    final frame = await _capture(
      tester,
      Stack(
        fit: StackFit.expand,
        children: [
          const ColoredBox(color: Color(0xFFFF0000)),
          BlendMask(
            blendMode: BlendMode.srcOver,
            isolate: true,
            child: Stack(
              fit: StackFit.expand,
              children: [
                const ColoredBox(color: Color(0xFF0000FF)),
                BlendMask(
                  blendMode: BlendMode.dstIn,
                  child: Align(
                    alignment: Alignment.centerLeft,
                    child: const SizedBox(
                      width: 32,
                      height: 64,
                      child: ColoredBox(color: Color(0xFFFFFFFF)),
                    ),
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );

    expect(frame.colorAt(16, 32), const Color(0xFF0000FF));
    expect(frame.colorAt(48, 32), const Color(0xFFFF0000));
  });

  testWidgets('MaskedBox com caminho aberto e pixel-neutro', (tester) async {
    Widget content() => const Row(
      children: [
        Expanded(child: ColoredBox(color: Color(0xFF14B8A6))),
        Expanded(child: ColoredBox(color: Color(0xFFF97316))),
      ],
    );
    final reference = await _capture(tester, content());
    final openPath = BezierPath(
      closed: false,
      vertices: const [
        PathVertex(p: Offset(-24, 20)),
        PathVertex(p: Offset(0, -24)),
        PathVertex(p: Offset(24, 20)),
      ],
    );
    final masked = await _capture(
      tester,
      MaskedBox(
        specs: [
          MaskSpec(
            path: openPath.build(),
            closed: openPath.closed,
            mode: MaskMode.add,
            inverted: false,
            opacity: 0.7,
            feather: 12,
            expansion: 6,
          ),
        ],
        child: content(),
      ),
    );

    expect(masked.bytes, orderedEquals(reference.bytes));
  });

  testWidgets('Lighten usa max e Darken usa min com feather e opacidade', (
    tester,
  ) async {
    const size = Size(96, 64);
    final firstPath = BezierPath.rect(44, 48, center: const Offset(-12, 0));
    final secondPath = BezierPath.rect(44, 48, center: const Offset(12, 0));
    final first = _mask(
      path: firstPath,
      mode: MaskMode.add,
      opacity: 0.35,
      feather: 10,
    );
    final second = _mask(
      path: secondPath,
      mode: MaskMode.add,
      opacity: 0.8,
      feather: 14,
    );

    final firstOnly = await _capture(tester, _masked([first]), size: size);
    final secondOnly = await _capture(tester, _masked([second]), size: size);
    final lighten = await _capture(
      tester,
      _masked([
        first,
        _mask(
          path: secondPath,
          mode: MaskMode.lighten,
          opacity: 0.8,
          feather: 14,
        ),
      ]),
      size: size,
    );
    final darken = await _capture(
      tester,
      _masked([
        first,
        _mask(
          path: secondPath,
          mode: MaskMode.darken,
          opacity: 0.8,
          feather: 14,
        ),
      ]),
      size: size,
    );

    for (final x in [32, 48, 64]) {
      final a = firstOnly.alphaAt(x, 32);
      final b = secondOnly.alphaAt(x, 32);
      final lighter = lighten.alphaAt(x, 32);
      final darker = darken.alphaAt(x, 32);

      expect(lighter, closeTo(math.max(a, b), 4), reason: 'Lighten em x=$x');
      expect(darker, closeTo(math.min(a, b), 4), reason: 'Darken em x=$x');
      expect(lighter, greaterThan(darker + 10), reason: 'distincao em x=$x');
    }
  });

  testWidgets('CompositionView preserva alfa no matte Luma', (tester) async {
    const size = Size(64, 64);
    List<ShapeItem> rectangle(Color color) => [
      ShapeParametric(
        kind: ParamShapeKind.rect,
        sizeX: AnimatedDouble(48),
        sizeY: AnimatedDouble(48),
      ),
      ShapeFill(color: color),
    ];

    final source = ShapeLayer(
      name: 'Matte branco 50%',
      startTime: Duration.zero,
      duration: const Duration(seconds: 1),
      contents: rectangle(const Color(0xFFFFFFFF)),
      position: AnimatedOffset(const Offset(32, 32)),
      opacity: AnimatedDouble(0.5),
    );
    final target = ShapeLayer(
      name: 'Alvo',
      startTime: Duration.zero,
      duration: const Duration(seconds: 1),
      contents: rectangle(const Color(0xFFFF0000)),
      position: AnimatedOffset(const Offset(32, 32)),
      matteMode: MatteMode.luma,
      matteSourceId: source.id,
    );
    final container = ProviderContainer();
    final time = ValueNotifier<Duration>(Duration.zero);
    final videos = VideoLayerManager();
    addTearDown(container.dispose);
    addTearDown(time.dispose);
    addTearDown(videos.dispose);
    container
        .read(editorControllerProvider.notifier)
        .openProject(
          VideoProject(
            name: 'Luma x alfa',
            createdAt: DateTime(2026, 9, 2),
            aspectRatio: 1,
            resolutionHeight: 64,
            layers: [source, target],
          ),
        );

    final frame = await _capture(
      tester,
      UncontrolledProviderScope(
        container: container,
        child: CompositionView(
          time: time,
          videos: videos,
          selectedId: null,
          exporting: true,
        ),
      ),
      size: size,
    );

    expect(frame.alphaAt(32, 32), closeTo(128, 5));
  });
}
