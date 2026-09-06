import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:thermion_flutter/thermion_flutter.dart' as f;

import '../../application/renderer3d/adaptive_quality.dart';
import '../../application/renderer3d/filament_renderer.dart';
import '../../application/renderer3d/renderer3d.dart';
import '../../application/motor3d_modo.dart';
import '../../domain/scene3d.dart';

class FilamentViewport extends StatefulWidget {
  const FilamentViewport({
    super.key,
    required this.scene,
    required this.camera,
    required this.time,
    required this.fallback,
  });
  final Scene3D scene;
  final RenderCamera camera;
  final Duration time;
  final Widget fallback;
  @override
  State<FilamentViewport> createState() => _FilamentViewportState();
}

typedef _Frame = ({
  Scene3D scene,
  RenderCamera camera,
  Duration time,
  Size size,
});

class _FilamentViewportState extends State<FilamentViewport>
    with WidgetsBindingObserver {
  final _quality = AdaptiveQuality();
  late final _renderer = FilamentRenderer(_quality);
  late final _queue = LatestFrameQueue<_Frame>(_render, _error);
  late final Future<void> _initialization;
  f.PlatformTextureDescriptor? _texture;
  Object? _failure;
  Size _size = Size.zero;
  bool _resumed = true, _closing = false;
  Timer? _retry;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    SchedulerBinding.instance.addTimingsCallback(_timings);
    MarcaGpuViva.entrou();
    _initialization = _renderer.initialize();
    _initialization.then((_) {
      if (mounted) setState(() {});
    }, onError: _error);
  }

  void _error(Object error, StackTrace stack) {
    debugPrint('Filament viewport: $error\n$stack');
    if (mounted) setState(() => _failure = error);
    unawaited(_close());
  }

  void _timings(List<FrameTiming> timings) {
    if (!_resumed || _texture == null || _failure != null) return;
    var changed = false;
    for (final timing in timings) {
      // This is compositor/UI pressure, not a fabricated native GPU counter.
      changed |= _quality.sample(
        math.max(
              timing.buildDuration.inMicroseconds,
              timing.rasterDuration.inMicroseconds,
            ) /
            1000,
        source: TimingSource.flutterFrame,
      );
    }
    if (changed && mounted) setState(() {});
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    _resumed = state == AppLifecycleState.resumed;
    if (_resumed && mounted) setState(() {});
  }

  @override
  void didHaveMemoryPressure() {
    // Reclaim this viewport's scene/surface safely, without touching documents.
    if (mounted) {
      setState(
        () => _failure = StateError(
          'Memory pressure; compatibility renderer active',
        ),
      );
    }
    unawaited(_close());
  }

  Future<void> _render(_Frame frame) async {
    await _initialization;
    if (_closing || !_resumed || _failure != null) return;
    final factor =
        math.min(1.0, 1080 / math.max(frame.size.width, frame.size.height)) *
        _quality.scale;
    final width = math.max(2, (frame.size.width * factor).round());
    final height = math.max(2, (frame.size.height * factor).round());
    final plugin = f.ThermionFlutterPlugin.instance;
    if (_texture == null) {
      _texture = await plugin.createTextureAndBindToView(
        _renderer.viewer.view,
        width,
        height,
      );
      if (_texture == null) throw StateError('Native surface unavailable');
    } else if (_texture!.width != width ||
        _texture!.height != height ||
        !_texture!.isSurfaceAvailable) {
      _texture = await plugin.resizeTexture(
        _texture!,
        _renderer.viewer.view,
        width,
        height,
      );
    }
    await _renderer.viewer.view.setViewport(width, height);
    await _renderer.synchronize(
      frame.scene,
      frame.camera,
      frame.time,
      frame.size,
    );
    if (!_renderer.frameSubmitted) {
      // Keep the previous surface and retry the current document next frame.
      _retry?.cancel();
      _retry = Timer(const Duration(milliseconds: 16), () {
        if (mounted && !_closing && _resumed) setState(() {});
      });
      return;
    }
    _texture!.markTextureFrameAvailable();
    // Only publish a new surface; never setState per rendered frame (feedback loop).
    if (mounted && _publishedTexture != _texture!.flutterTextureId) {
      setState(() => _publishedTexture = _texture!.flutterTextureId);
    }
  }

  int? _publishedTexture;
  Future<void> _close() async {
    if (_closing) return;
    _closing = true;
    _retry?.cancel();
    await _queue.close();
    try {
      await _initialization;
    } catch (_) {
      /* cleanup partial initialization */
    }
    await _renderer.dispose();
    MarcaGpuViva.saiu();
    _texture = null;
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    SchedulerBinding.instance.removeTimingsCallback(_timings);
    unawaited(_close());
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => LayoutBuilder(
    builder: (context, constraints) {
      _size = constraints.biggest;
      if (_failure != null) return widget.fallback;
      if (_size.isFinite && !_size.isEmpty && _resumed) {
        final frame = (
          scene: widget.scene,
          camera: widget.camera,
          time: widget.time,
          size: _size,
        );
        WidgetsBinding.instance.addPostFrameCallback((_) {
          if (mounted) _queue.submit(frame);
        });
      }
      return _publishedTexture == null
          ? const Center(child: CircularProgressIndicator())
          : Texture(
              textureId: _publishedTexture!,
              filterQuality: FilterQuality.low,
            );
    },
  );
}
