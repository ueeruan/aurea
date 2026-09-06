import 'dart:convert';
import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/application/texture_cache.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  test('clear cancels queued decodes without resurrecting images', () async {
    final cache = TextureCache.instance;
    cache.clear();
    final pending = cache.prepare('missing-texture.png');
    cache.clear();
    expect(await pending, isFalse);
    expect(cache.entryCount, 0);
    expect(cache.decodedBytes, 0);
  });
  test('decoded cache is bounded and releases on memory pressure', () async {
    final recorder = PictureRecorder();
    Canvas(recorder).drawColor(const Color(0xffabcdef), BlendMode.src);
    final picture = recorder.endRecording();
    final source = await picture.toImage(1024, 1024);
    final cache = TextureCache.instance;
    cache.clear();
    addTearDown(cache.clear);
    for (var i = 0; i < 20; i++) {
      cache.put('texture-$i', source.clone());
    }
    expect(cache.entryCount, 16);
    expect(cache.decodedBytes, TextureCache.maxBytes);
    expect(await cache.imageFor('texture-19')!.toByteData(), isNotNull);
    cache.didHaveMemoryPressure();
    expect(cache.entryCount, 0);
    expect(cache.decodedBytes, 0);
    source.dispose();
    picture.dispose();
  });
  test(
    'textura embutida decodifica antes de liberar descriptor e buffer',
    () async {
      final recorder = PictureRecorder(), canvas = Canvas(recorder);
      canvas.drawRect(
        const Rect.fromLTWH(0, 0, 8, 4),
        Paint()..color = const Color(0xFFFF4000),
      );
      final picture = recorder.endRecording(),
          image = await picture.toImage(8, 4);
      final png = await image.toByteData(format: ImageByteFormat.png);
      final uri =
          'data:image/png;base64,${base64Encode(png!.buffer.asUint8List())}';
      image.dispose();
      picture.dispose();
      final cache = TextureCache.instance;
      cache.clear();
      addTearDown(cache.clear);
      expect(await cache.prepare(uri), isTrue);
      final decoded = cache.imageFor(uri)!;
      expect(decoded.width, 8);
      expect(decoded.height, 4);
      expect(await decoded.toByteData(), isNotNull);
    },
  );
}
