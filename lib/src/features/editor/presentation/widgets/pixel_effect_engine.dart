import 'dart:ui' as ui;

import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';

import '../../domain/pixel_effect.dart';
import 'fx_lote2.dart';

/// Shared preview/export backend. Native filter passes on Impeller avoid
/// readback (including live video); Canvas snapshot fallback supports Skia.
/// Processing is SDR, not a claim of Adobe's 32-bpc/HDR color pipeline.
class PixelEffectEngine {
  PixelEffectEngine._();
  static ui.FragmentProgram? _program;
  static Future<void>? _loading;
  static String? failure;
  static bool get ready => _program != null;
  static Future<void> warmUp() => _loading ??= _load();
  static Future<void> _load() async {
    try {
      _program = await ui.FragmentProgram.fromAsset('shaders/effects_v2.frag');
    } catch (error) {
      failure = error.toString();
      debugPrint('AUREA FX V2 unavailable: $error');
    }
  }

  @visibleForTesting
  static ui.FragmentShader createShader(
    PixelEffectFrame frame, {
    required double width,
    required double height,
    required ui.Image image,
  }) {
    final shader = _program!.fragmentShader();
    configure(shader, frame, filter: false);
    shader
      ..setFloat(0, width)
      ..setFloat(1, height)
      ..setImageSampler(0, image);
    return shader;
  }

  static void configure(
    ui.FragmentShader shader,
    PixelEffectFrame frame, {
    required bool filter,
  }) {
    shader
      ..setFloat(2, frame.mode.toDouble())
      ..setFloat(3, frame.time)
      ..setFloat(4, frame.pixelScale)
      ..setFloat(5, filter ? 1 : 0);
    for (var i = 0; i < 32; i++) {
      shader.setFloat(6 + i, frame.values[i]);
    }
    for (var i = 0; i < 4; i++) {
      shader.setFloat(38 + i, frame.color[i]);
    }
    for (var i = 0; i < 12; i++) {
      shader.setFloat(
        42 + i,
        i < frame.extraColors.length
            ? frame.extraColors[i]
            : (i % 4 == 3 ? 1 : 0),
      );
    }
  }
}

class PixelEffectPass extends StatefulWidget {
  const PixelEffectPass({super.key, required this.frame, required this.child});
  final PixelEffectFrame frame;
  final Widget child;
  @override
  State<PixelEffectPass> createState() => _PixelEffectPassState();
}

class _PixelEffectPassState extends State<PixelEffectPass> {
  ui.FragmentShader? _shader;
  @override
  void dispose() {
    _shader?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (!PixelEffectEngine.ready) return widget.child;
    final shader = _shader ??= PixelEffectEngine._program!.fragmentShader();
    final filter = ui.ImageFilter.isShaderFilterSupported;
    PixelEffectEngine.configure(shader, widget.frame, filter: filter);
    if (filter) {
      return ImageFiltered(
        imageFilter: ui.ImageFilter.shader(shader),
        child: widget.child,
      );
    }
    return FxSnapshot(
      painter: _PixelPainter(shader, widget.frame),
      child: widget.child,
    );
  }
}

class _PixelPainter extends SnapshotPainter {
  _PixelPainter(this.shader, this.frame);
  final ui.FragmentShader shader;
  final PixelEffectFrame frame;
  @override
  void paint(
    PaintingContext context,
    Offset offset,
    Size size,
    PaintingContextCallback painter,
  ) => painter(context, offset);
  @override
  void paintSnapshot(
    PaintingContext context,
    Offset offset,
    Size size,
    ui.Image image,
    Size sourceSize,
    double pixelRatio,
  ) {
    if (size.isEmpty || image.width == 0 || image.height == 0) return;
    shader
      ..setFloat(0, image.width.toDouble())
      ..setFloat(1, image.height.toDouble())
      ..setFloat(4, frame.pixelScale * pixelRatio)
      ..setImageSampler(0, image);
    final canvas = context.canvas;
    canvas.save();
    canvas.translate(offset.dx, offset.dy);
    canvas.scale(size.width / image.width, size.height / image.height);
    canvas.drawRect(
      Rect.fromLTWH(0, 0, image.width.toDouble(), image.height.toDouble()),
      Paint()..shader = shader,
    );
    canvas.restore();
  }

  @override
  bool shouldRepaint(covariant _PixelPainter old) =>
      !identical(frame, old.frame);
}
