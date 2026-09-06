import 'dart:io';
import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/application/panorama_cache.dart';
import 'package:aurea/src/features/editor/domain/panorama3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  test(
    'panorama filtra reflexo sem degrau nas faces e espalha com rugosidade',
    () async {
      final dir = await Directory.systemTemp.createTemp('aurea-panorama-test-');
      addTearDown(() => dir.delete(recursive: true));
      final recorder = PictureRecorder(), canvas = Canvas(recorder);
      canvas.drawRect(
        const Rect.fromLTWH(0, 0, 512, 256),
        Paint()..color = const Color(0xFF101010),
      );
      canvas.drawRect(
        const Rect.fromLTWH(300, 0, 40, 256),
        Paint()..color = const Color(0xFFFFFFFF),
      );
      final picture = recorder.endRecording(),
          image = await picture.toImage(512, 256);
      final png = await image.toByteData(format: ImageByteFormat.png);
      final file = File('${dir.path}/environment.png');
      await file.writeAsBytes(png!.buffer.asUint8List());
      image.dispose();
      picture.dispose();
      final cache = PanoramaCache.instance;
      cache.clear();
      addTearDown(cache.clear);
      final panorama = preparePanorama(
        path: file.path,
      ).copyWith(mirrorTo360: false, fillZenithNadir: false, seamSoftness: 0);
      expect(await cache.prepare(panorama), isTrue);
      final sample = cache.samplerFor(panorama)!;
      final left = sample(const Vec3(.99999, 0, 1), 0),
          right = sample(const Vec3(1, 0, .99999), 0);
      expect((left.r - right.r).abs(), lessThan(.005));
      final sharp = sample(const Vec3(1, 0, 1), 0),
          rough = sample(const Vec3(1, 0, 1), 1);
      expect(sharp.r, greaterThan(.95));
      expect(rough.r, lessThan(sharp.r));
      expect(rough.r, isNonNegative);
      expect(rough.r.isFinite, isTrue);
    },
  );
}
