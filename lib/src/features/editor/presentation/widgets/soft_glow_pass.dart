import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';

import 'passe_de_cor.dart';

/// One source capture, with continuous native Gaussian kernels. Unlike sparse
/// offset sampling this cannot produce recognizable displaced silhouettes.
class SoftGlowPass extends StatefulWidget {
  const SoftGlowPass({
    super.key,
    required this.values,
    required this.color,
    required this.child,
    this.pixelRatio = 1,
  });
  final List<double> values;
  final Color color;
  final double pixelRatio;
  final Widget child;
  static const asset = 'shaders/glow_source.frag';

  @override
  State<SoftGlowPass> createState() => _SoftGlowPassState();
}

class _SoftGlowPassState extends State<SoftGlowPass> {
  ui.FragmentShader? _shader;
  @override
  void initState() {
    super.initState();
    _shader = MotorSapphire.programa(SoftGlowPass.asset)?.fragmentShader();
    if (_shader == null) {
      MotorSapphire.carregar(SoftGlowPass.asset).then((_) {
        if (mounted) {
          setState(() {
            _shader = MotorSapphire.programa(SoftGlowPass.asset)
                ?.fragmentShader();
          });
        }
      });
    }
  }

  @override
  void dispose() {
    _shader?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => _GlowSurface(
    shader: _shader,
    values: widget.values,
    color: widget.color,
    pixelRatio: widget.pixelRatio,
    child: widget.child,
  );
}

class _GlowSurface extends SingleChildRenderObjectWidget {
  const _GlowSurface({
    required this.shader,
    required this.values,
    required this.color,
    required this.pixelRatio,
    required super.child,
  });
  final ui.FragmentShader? shader;
  final List<double> values;
  final Color color;
  final double pixelRatio;
  @override
  RenderObject createRenderObject(BuildContext context) =>
      _GlowRender()..configure(this);
  @override
  void updateRenderObject(BuildContext context, covariant _GlowRender render) =>
      render.configure(this);
}

class _GlowRender extends RenderProxyBox {
  ui.FragmentShader? shader;
  List<double> values = const [2, 200, 1, 0, .2, 0];
  Color color = const Color(0xffffffff);
  double pixelRatio = 1;
  void configure(_GlowSurface widget) {
    shader = widget.shader;
    values = widget.values;
    color = widget.color;
    pixelRatio = widget.pixelRatio;
    markNeedsPaint();
  }

  double get radius => values[1].clamp(0, 1000);
  @override
  Rect get paintBounds => super.paintBounds.inflate(radius * 2.1);
  @override
  bool get isRepaintBoundary => true;
  @override
  void paint(PaintingContext context, Offset offset) {
    final input = child;
    final fx = shader;
    if (input == null || size.isEmpty) return;
    if (fx == null || values[2] <= 0) {
      context.paintChild(input, offset);
      return;
    }
    final bounds = Offset.zero & size;
    final ratio = math
        .min(pixelRatio, math.sqrt(2e6 / (size.width * size.height)))
        .clamp(.05, 3.0);
    final layer = OffsetLayer();
    final capture = PaintingContext(layer, bounds);
    capture.paintChild(input, Offset.zero);
    // ignore: invalid_use_of_protected_member
    capture.stopRecordingIfNeeded();
    final image = layer.toImageSync(bounds, pixelRatio: ratio);
    layer.dispose();
    fx
      ..setFloat(0, size.width)
      ..setFloat(1, size.height)
      ..setFloat(2, values[3])
      ..setFloat(3, values[4])
      ..setFloat(4, values[5])
      ..setFloat(5, color.r)
      ..setFloat(6, color.g)
      ..setFloat(7, color.b)
      ..setImageSampler(0, image);
    final canvas = context.canvas;
    canvas.save();
    canvas.translate(offset.dx, offset.dy);
    // Isolate screen compositing from layers behind this effect.
    canvas.saveLayer(paintBounds, Paint());
    canvas.drawImageRect(
      image,
      Rect.fromLTWH(0, 0, image.width.toDouble(), image.height.toDouble()),
      bounds,
      Paint()..filterQuality = FilterQuality.low,
    );
    final deep = values[0] == 2;
    final kernels = deep ? const [.12, .35, .7] : const [.35];
    final weights = deep ? const [.5, .3, .2] : const [1.0];
    for (var i = 0; i < kernels.length; i++) {
      // The Gaussian kernels already integrate to one. Multiplying by the
      // old sparse sampler's compensation burns an ordinary photo to white.
      final gain = values[2] * (deep ? 1.2 : 1.0) * weights[i];
      canvas.saveLayer(
        paintBounds,
        Paint()
          ..blendMode = BlendMode.screen
          ..colorFilter = ColorFilter.matrix([
            gain,
            0,
            0,
            0,
            0,
            0,
            gain,
            0,
            0,
            0,
            0,
            0,
            gain,
            0,
            0,
            0,
            0,
            0,
            gain,
            0,
          ]),
      );
      final sigma = radius * kernels[i];
      canvas.drawRect(
        bounds,
        Paint()
          ..shader = fx
          ..imageFilter = ui.ImageFilter.blur(
            sigmaX: sigma,
            sigmaY: sigma,
            tileMode: TileMode.decal,
          ),
      );
      canvas.restore();
    }
    canvas.restore();
    canvas.restore();
    image.dispose();
  }
}
