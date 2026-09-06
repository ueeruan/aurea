import 'dart:async';
import 'dart:convert';
import 'dart:isolate';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/application/renderer3d/adaptive_quality.dart';
import 'package:aurea/src/features/editor/application/renderer3d/renderer3d.dart';
import 'package:aurea/src/features/editor/application/renderer3d/scene_glb.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/glb_import.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/camera3d.dart';
import 'package:aurea/src/features/editor/presentation/widgets/scene3d_painter.dart';

void main() {
  test('native editor helpers do not render the CPU scene a second time', () {
    final recorder = ui.PictureRecorder();
    var geometryPasses = 0;
    final painter = Scene3DPainter(
      scene: Scene3D.demo,
      camera: Camera3D(),
      view: SceneView.camera,
      time: Duration.zero,
      showHelpers: true,
      helpersOnly: true,
      onMetrics: (_) => geometryPasses++,
    );
    painter.paint(ui.Canvas(recorder), const ui.Size(640, 360));
    recorder.endRecording().dispose();
    expect(geometryPasses, 0);
  });
  test('dynamic resolution sheds sustained pressure and restores slowly', () {
    final q = AdaptiveQuality();
    for (var i = 0; i < 11; i++) {
      expect(q.sample(25, source: TimingSource.gpu), isFalse);
    }
    expect(q.sample(25, source: TimingSource.gpu), isTrue);
    expect(q.scale, .9);
    for (var i = 0; i < 120; i++) {
      q.sample(5, source: TimingSource.gpu);
    }
    expect(q.scale, .9); // EMA warm-down plus the recovery window.
    for (var i = 0; i < 30; i++) {
      q.sample(5, source: TimingSource.gpu);
    }
    expect(q.scale, 1);
  });
  test(
    'quality is bounded, rejects invalid timing and never reduces export',
    () {
      final q = AdaptiveQuality();
      for (var i = 0; i < 1000; i++) {
        q.sample(60, source: TimingSource.gpu);
      }
      expect(q.scale, .5);
      expect(q.resolution(exporting: true), 1);
      q.reset();
      for (final ms in [double.nan, double.infinity, -1.0, 0.0]) {
        expect(q.sample(ms, source: TimingSource.gpu), isFalse);
      }
      for (var i = 0; i < 1000; i++) {
        q.sample(100, source: TimingSource.submission);
      }
      expect(
        q.scale,
        1,
      ); // CPU command submission must not pretend to be GPU work.
    },
  );
  test('queue coalesces scrubs and waits before disposing resources', () async {
    final release = Completer<void>(), started = Completer<void>();
    final calls = <int>[];
    final queue = LatestFrameQueue<int>((frame) async {
      calls.add(frame);
      if (frame == 1) {
        started.complete();
        await release.future;
      }
    }, (e, s) => fail('$e'));
    queue.submit(1);
    await started.future;
    for (var i = 2; i <= 1000; i++) {
      queue.submit(i);
    }
    release.complete();
    await Future<void>.delayed(Duration.zero);
    expect(calls, [1, 1000]);
    await queue.close();
    queue.submit(1001);
    expect(calls, [1, 1000]);
  });
  test(
    'queue survives synchronous errors without becoming permanently busy',
    () async {
      final errors = <Object>[], calls = <int>[];
      final queue = LatestFrameQueue<int>((i) {
        if (i == 1) throw StateError('load failed');
        calls.add(i);
        return Future.value();
      }, (e, _) => errors.add(e));
      queue.submit(1);
      await Future<void>.delayed(Duration.zero);
      queue.submit(2);
      await Future<void>.delayed(Duration.zero);
      await queue.close();
      expect(errors, hasLength(1));
      expect(calls, [2]);
    },
  );
  test(
    'native bridge makes a valid GLB without moving source geometry',
    () async {
      final node = SceneNode(id: 'cube', kind: Element3DKind.cube, size: 80);
      final bytes = await Isolate.run(() => encodeNodeGlb(node));
      final parsed = parseGlb(bytes);
      final sourceTriangles = element3DMesh(node.kind).faces
          .fold<int>(0, (n, face) => n + face.length - 2);
      expect(parsed.triangles, sourceTriangles);
      final header = ByteData.sublistView(bytes);
      expect(header.getUint32(8, Endian.little), bytes.length);
      final jsonLength = header.getUint32(12, Endian.little);
      final doc = jsonDecode(utf8.decode(bytes.sublist(20, 20 + jsonLength)));
      final primitive = doc['meshes'][0]['primitives'][0];
      final positionCount =
          doc['accessors'][primitive['attributes']['POSITION']]['count'] as int;
      final indexCount = doc['accessors'][primitive['indices']]['count'] as int;
      expect(positionCount, lessThan(indexCount));
      expect(doc['nodes'][0]['name'], 'cube');
      expect(
        doc['nodes'][0].containsKey('scale'),
        false,
      ); // transforms stay editable.
      expect(node.size, 80);
    },
  );
  test('bridge rejects an empty mesh explicitly', () {
    final node = SceneNode(mesh: Element3DMesh([], []));
    expect(() => encodeNodeGlb(node), throwsStateError);
  });
}
