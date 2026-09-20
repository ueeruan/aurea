import 'dart:ui' as ui;

import 'package:flutter/widgets.dart';
import '../../application/quadros_de_video.dart';

/// Keep a complete temporal sample while its replacement decodes. Never
/// alternate between the live player's clock and the temporal frame clock.
class TemporalFrameSet extends StatefulWidget {
  const TemporalFrameSet({super.key, required this.source, required this.times,
    required this.builder, required this.fallback});
  final String source;
  final List<Duration> times;
  final Widget Function(List<ui.Image>) builder;
  final Widget fallback;
  @override
  State<TemporalFrameSet> createState() => _TemporalFrameSetState();
}

class _TemporalFrameSetState extends State<TemporalFrameSet> {
  List<ui.Image>? _retained;
  List<ui.Image>? _borrowed;
  void _clear() {
    for (final image in _retained ?? const <ui.Image>[]) { image.dispose(); }
    _retained = null;
    _borrowed = null;
  }
  @override
  void didUpdateWidget(TemporalFrameSet oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.source != widget.source) _clear();
  }
  @override
  void dispose() { _clear(); super.dispose(); }
  @override
  Widget build(BuildContext context) => ValueListenableBuilder<int>(
    valueListenable: QuadrosDeVideo.instance.revision,
    builder: (context, _, _) {
      final ready = [for (final t in widget.times) QuadrosDeVideo.instance.quadro(widget.source, t, prefetch: false)];
      if (ready.every((f) => f != null) &&
          (_borrowed == null || ready.length != _borrowed!.length ||
           List.generate(ready.length, (i) => !identical(ready[i], _borrowed![i])).any((v) => v))) {
        final owned = [for (final f in ready) f!.clone()];
        _clear();
        _borrowed = ready.cast<ui.Image>();
        _retained = owned;
      }
      if (ready.every((f) => f != null)) {
        for (final t in widget.times) { QuadrosDeVideo.instance.antecipar(widget.source, t); }
      }
      return _retained == null ? widget.fallback : widget.builder(_retained!);
    },
  );
}

/// The cache/exporter owns its image; the displayed widget keeps a separate
/// native handle until replacement/unmount. Cache eviction is then safe.
class OwnedVideoFrame extends StatefulWidget {
  const OwnedVideoFrame({super.key, required this.image});
  final ui.Image image;
  @override
  State<OwnedVideoFrame> createState() => _OwnedVideoFrameState();
}

class _OwnedVideoFrameState extends State<OwnedVideoFrame> {
  late ui.Image _image = widget.image.clone();
  @override
  void didUpdateWidget(OwnedVideoFrame oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (!identical(oldWidget.image, widget.image)) {
      final old = _image;
      _image = widget.image.clone();
      old.dispose();
    }
  }

  @override
  void dispose() {
    _image.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => RawImage(
    image: _image,
    fit: BoxFit.fill,
    filterQuality: FilterQuality.medium,
  );
}

class OwnedRgbFrames extends StatefulWidget {
  const OwnedRgbFrames({super.key, required this.images, required this.size});
  final List<ui.Image> images;
  final Size size;
  @override
  State<OwnedRgbFrames> createState() => _OwnedRgbFramesState();
}

class _OwnedRgbFramesState extends State<OwnedRgbFrames> {
  late List<ui.Image> _images = [for (final img in widget.images) img.clone()];
  ui.FragmentShader? _shader;
  @override
  void initState() {
    super.initState();
    final loaded = RgbFramesPainter._loaded;
    if (loaded != null) {
      _shader = loaded.fragmentShader();
      return;
    }
    RgbFramesPainter.prepare().then((program) {
      if (mounted) setState(() => _shader = program.fragmentShader());
    });
  }
  @override
  void didUpdateWidget(OwnedRgbFrames oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (List.generate(3, (i) => identical(oldWidget.images[i], widget.images[i])).every((same) => same)) return;
    final old = _images;
    _images = [for (final img in widget.images) img.clone()];
    for (final img in old) { img.dispose(); }
  }
  @override
  void dispose() {
    _shader?.dispose();
    for (final img in _images) { img.dispose(); }
    super.dispose();
  }
  @override
  Widget build(BuildContext context) => _shader == null
      ? SizedBox.fromSize(size: widget.size, child: RawImage(image: _images[1], fit: BoxFit.fill))
      : CustomPaint(size: widget.size, painter: RgbFramesPainter(_images, _shader!));
}

/// One GPU pass with explicit sampler bindings. ColorFiltered + plus
/// saveLayers do not produce the same channel composition on all backends.
class RgbFramesPainter extends CustomPainter {
  const RgbFramesPainter(this.frames, this.shader);
  final List<ui.Image> frames;
  final ui.FragmentShader shader;
  static Future<ui.FragmentProgram>? _program;
  static ui.FragmentProgram? _loaded;
  static Future<ui.FragmentProgram> prepare() =>
      _program ??= ui.FragmentProgram.fromAsset('shaders/rgb_time_warp.frag')
          .then((p) => _loaded = p);
  @override
  void paint(Canvas canvas, Size size) {
    shader
      ..setFloat(0, size.width)..setFloat(1, size.height)
      ..setImageSampler(0, frames[0])..setImageSampler(1, frames[1])
      ..setImageSampler(2, frames[2]);
    canvas.drawRect(Offset.zero & size, Paint()..shader = shader);
  }
  @override
  bool shouldRepaint(covariant RgbFramesPainter old) => old.frames != frames || old.shader != shader;
}
