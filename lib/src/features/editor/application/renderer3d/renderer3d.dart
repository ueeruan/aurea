import 'dart:ui';

import '../../domain/scene3d.dart';

/// Editor-facing contract. No engine handles cross this boundary.
abstract interface class Renderer3D {
  String get name;
  Future<void> initialize();
  Future<void> synchronize(
    Scene3D scene,
    RenderCamera camera,
    Duration time,
    Size size,
  );
  Future<void> suspend();
  Future<void> dispose();
}

/// Missing GPU counters stay null; submission time is not GPU execution time.
class RendererSample {
  const RendererSample({
    required this.submissionMs,
    this.gpuMs,
    this.residentBytes,
  });
  final double submissionMs;
  final double? gpuMs;
  final int? residentBytes;
}

/// One in-flight frame and one replaceable pending frame. Scrubbing cannot
/// accumulate obsolete work. Errors remain observable by the viewport.
class LatestFrameQueue<T> {
  LatestFrameQueue(this.execute, this.onError);
  final Future<void> Function(T) execute;
  final void Function(Object, StackTrace) onError;
  T? _pending;
  Future<void>? _running;
  bool _closed = false;
  void submit(T value) {
    if (_closed) return;
    _pending = value;
    _running ??= Future<void>.microtask(_drain);
  }

  Future<void> _drain() async {
    try {
      while (!_closed && _pending != null) {
        final next = _pending as T;
        _pending = null;
        await execute(next);
      }
    } catch (error, stack) {
      _pending = null;
      onError(error, stack);
    } finally {
      _running = null;
    }
  }

  Future<void> close() async {
    _closed = true;
    _pending = null;
    await _running;
  }
}
