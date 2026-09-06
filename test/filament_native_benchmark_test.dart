import 'dart:convert';
import 'dart:io';
import 'dart:math' as math;
import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';
import 'package:thermion_flutter/thermion_flutter.dart' as f;
// Headless native setup is intentionally isolated to the benchmark harness.
// ignore: implementation_imports
import 'package:thermion_dart/src/filament/src/implementation/ffi_filament_app.dart'
    as native;
import 'package:aurea/src/features/editor/application/renderer3d/adaptive_quality.dart';
import 'package:aurea/src/features/editor/application/renderer3d/filament_renderer.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';

/// Opt-in native GPU test: deliberately separate from unit-test results.
/// Wall time measures CPU command submission; GPU timestamps are separate.
void main() {
  test(
    'native Filament retains meshes across deterministic camera scrubs',
    () async {
      await native.FFIFilamentApp.create(
        config: native.FFIFilamentConfig(
          backend: f.Backend.VULKAN,
          loadResource: (path) =>
              File(path.replaceFirst('file://', '')).readAsBytes(),
        ),
      );
      final app = f.FilamentApp.instance! as native.FFIFilamentApp;
      final records = <Map<String, Object?>>[];
      try {
        for (final count in [1, 100, 500, 1000, 2000]) {
          late f.SwapChain swap;
          final renderer = FilamentRenderer(
            AdaptiveQuality(),
            viewerFactory: () async {
              swap = await app.createHeadlessSwapChain(640, 360);
              final viewer = f.ThermionViewerFFI(app: app);
              await viewer.initialized;
              await app.renderManager.attach(viewer.view, swap);
              await viewer.view.setViewport(640, 360);
              return viewer;
            },
          );
          try {
            final columns = math.min(40, count);
            final rows = (count / columns).ceil();
            final distance = math
                .max(300, math.max(columns * 30, rows * 55))
                .toDouble();
            final scene = Scene3D(
              nodes: [
                for (var i = 0; i < count; i++)
                  SceneNode(
                    id: 'n$i',
                    size: 6,
                    x: AnimatedDouble((i % columns - (columns - 1) / 2) * 18),
                    y: AnimatedDouble((i ~/ columns - (rows - 1) / 2) * 18),
                  ),
              ],
              lights: [Light3D()],
              background: const Color(0xff222222),
            );
            final load = Stopwatch()..start();
            await renderer.initialize();
            await renderer.synchronize(
              scene,
              RenderCamera(position: Vec3(0, 0, distance)),
              Duration.zero,
              const Size(640, 360),
            );
            load.stop();
            final samples = <double>[];
            var submitted = 0, skipped = 0;
            for (var frame = 0; frame < 40; frame++) {
              final watch = Stopwatch()..start();
              await renderer.synchronize(
                scene,
                RenderCamera(position: Vec3(frame * .2, 0, distance)),
                Duration(microseconds: frame * 16667),
                const Size(640, 360),
              );
              watch.stop();
              if (renderer.frameSubmitted) {
                submitted++;
              } else {
                skipped++;
              }
              if (frame >= 10) samples.add(watch.elapsedMicroseconds / 1000);
              await Future<void>.delayed(const Duration(milliseconds: 16));
            }
            samples.sort();
            expect(
              renderer.residentAssetCount,
              1,
            ); // Shared native geometry for every identical cube.
            final pixels = await app.capture(
              swap,
              view: renderer.viewer.view,
              pixelDataType: f.PixelDataType.UBYTE,
            );
            expect(pixels.single.$2.length, 640 * 360 * 4);
            expect(
              pixels.single.$2.toSet().length,
              greaterThan(8),
            ); // Actually rendered, not a blank success.
            records.add({
              'objects': count,
              'loadingMs': load.elapsedMilliseconds,
              'nativeFrameWallP50Ms': samples[samples.length ~/ 2],
              'nativeFrameWallP95Ms': samples[(samples.length * .95).floor()],
              'submittedFrames': submitted,
              'skippedFrames': skipped,
              'lastGpuTimestampMs': renderer.lastGpuMilliseconds,
              'nativeResolutionScale': renderer.nativeResolutionScale,
              'drawCalls': null,
              'residentAssets': renderer.residentAssetCount,
              'processRssBytes': ProcessInfo.currentRss,
              'platform': Platform.operatingSystem,
              'mode': 'Flutter test / Vulkan headless',
            });
          } catch (error, stack) {
            // Preserve the primary Dart failure before native teardown runs.
            // ignore: avoid_print
            print('Filament benchmark failure: $error\n$stack');
            rethrow;
          } finally {
            await renderer.dispose();
            await app.destroySwapChain(swap);
          }
        }
      } finally {
        await app.destroy();
        final dir = Directory('output/renderer-audit')
          ..createSync(recursive: true);
        File('${dir.path}/filament-desktop.json').writeAsStringSync(
          const JsonEncoder.withIndent('  ').convert(records),
        );
      }
    },
    skip: Platform.environment['AUREA_FILAMENT_BENCH'] != '1',
    timeout: const Timeout(Duration(minutes: 10)),
  );
}
