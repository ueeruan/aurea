import 'dart:io';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/bloom.dart';
import 'package:aurea/src/features/editor/domain/pixel_effect.dart';
import 'package:aurea/src/features/editor/presentation/widgets/pixel_effect_engine.dart';
import 'package:flutter_test/flutter_test.dart';

Future<ui.Image> fixture({bool solid = false}) async {
  final bytes = Uint8List(128 * 64 * 4);
  for (var y = 0; y < 64; y++) {
    for (var x = 0; x < 128; x++) {
      final i = (y * 128 + x) * 4;
      final alpha = solid
          ? 255
          : (y < 16
                ? 0
                : y < 32
                ? 128
                : 255);
      bytes[i] = solid ? 100 : (x * 255 / 127).round();
      bytes[i + 1] = solid ? 100 : (y * 255 / 63).round();
      bytes[i + 2] = solid ? 100 : (x % 16 < 8 ? 40 : 230);
      bytes[i + 3] = alpha;
    }
  }
  final buffer = await ui.ImmutableBuffer.fromUint8List(bytes);
  final descriptor = ui.ImageDescriptor.raw(
    buffer,
    width: 128,
    height: 64,
    pixelFormat: ui.PixelFormat.rgba8888,
  );
  final codec = await descriptor.instantiateCodec();
  final image = (await codec.getNextFrame()).image;
  codec.dispose();
  descriptor.dispose();
  buffer.dispose();
  return image;
}

Future<ui.Image> render(ui.Image input, PixelEffectFrame frame) async {
  final recorder = ui.PictureRecorder();
  final canvas = ui.Canvas(recorder);
  final shader = PixelEffectEngine.createShader(
    frame,
    width: 128,
    height: 64,
    image: input,
  );
  canvas.drawRect(
    const ui.Rect.fromLTWH(0, 0, 128, 64),
    ui.Paint()..shader = shader,
  );
  final picture = recorder.endRecording();
  final output = await picture.toImage(128, 64);
  picture.dispose();
  shader.dispose();
  return output;
}

Future<Uint8List> rgba(ui.Image image) async =>
    (await image.toByteData(format: ui.ImageByteFormat.rawStraightRgba))!.buffer
        .asUint8List();

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUpAll(() async {
    await PixelEffectEngine.warmUp();
    expect(PixelEffectEngine.failure, isNull);
    expect(
      PixelEffectEngine.ready,
      isTrue,
      reason: 'Shaders must execute; no fallback in these tests.',
    );
  });
  test('all pixel kernel parameters match catalog contracts', () {
    expect(pixelKernels.length, 30);
    expect(
      pixelKernels.values.map((k) => k.mode).toSet().length,
      pixelKernels.length,
    );
    for (final entry in pixelKernels.entries) {
      expect(
        entry.value.keys.toSet(),
        effectSpecs[entry.key]!.params.keys.toSet(),
        reason: entry.key.name,
      );
    }
  });
  test('neutral color passes preserve top, bottom and alpha across a stack', () async {
    final input = await fixture();
    final first = await render(input, PixelEffectFrame(1, [0, 1, 1, 0, 1, 0]));
    final second = await render(first, PixelEffectFrame(9, [0]));
    final before = await rgba(input);
    final after = await rgba(second);
    // Asymmetric rows catch a vertical flip which a solid-color fixture misses.
    for (final y in [2, 20, 40, 60]) {
      for (final x in [8, 64, 120]) {
        final start = (y * 128 + x) * 4;
        for (var c = 0; c < 4; c++) {
          expect(
            after[start + c],
            closeTo(before[start + c], 2),
            reason: 'pixel ($x,$y), channel $c',
          );
        }
      }
    }
    second.dispose();
    first.dispose();
    input.dispose();
  });
  test(
    'glow threshold removes dark alpha; tone mapping and dirt are live',
    () async {
      final input = await fixture(solid: true);
      final dark = await render(
        input,
        PixelEffectFrame(21, [.8, .1, 0, 0, 1, 1, 1, 0, 0, 1]),
      );
      expect((await rgba(dark)).every((v) => v == 0), isTrue);
      for (var mode = 0; mode < 4; mode++) {
        final result = await render(
          input,
          PixelEffectFrame(22, [1, mode.toDouble(), 0]),
        );
        final bytes = await rgba(result);
        expect(bytes[0], closeTo(tonemap(100 / 255 * 2, mode) * 255, 1.1));
        expect(bytes[3], 255);
        result.dispose();
      }
      final clean = await render(input, PixelEffectFrame(22, [0, 3, 0]));
      final dirty = await render(input, PixelEffectFrame(22, [0, 3, 200]));
      expect(await rgba(clean), isNot(equals(await rgba(dirty))));
      for (final image in [input, dark, clean, dirty]) {
        image.dispose();
      }
    },
  );
  test('neutral distortion and VHS/glitch masters preserve source', () async {
    final input = await fixture();
    for (final frame in [
      PixelEffectFrame(23, [0, 45, 2, .8]),
      PixelEffectFrame(24, [0]),
      PixelEffectFrame(25, [0, 0, 1, .5]),
      PixelEffectFrame(28, [0, 20, 100, .7, 10, .4, 4]),
      PixelEffectFrame(29, [0, .5, .8, .6, .5, .6, 3]),
      PixelEffectFrame(31, [0, 12, 0, 0, 3]),
      PixelEffectFrame(32, [0, 1, .5, .8, .8, .5, .4, .5, .5, 4]),
    ]) {
      final result = await render(input, frame);
      expect(
        await rgba(result),
        await rgba(input),
        reason: 'mode ${frame.mode}',
      );
      result.dispose();
    }
    input.dispose();
  });
  test(
    'Levels gamma is nonlinear and output limits are applied afterwards',
    () {
      expect(levelsChannel(.25, 0, 1, 2, 0, 1), closeTo(.5, 1e-9));
      expect(levelsChannel(.25, 0, 1, 2, .2, .8), closeTo(.5, 1e-9));
      expect(levelsChannel(0, .5, .5, 1, 0, 1), 0);
      expect(levelsChannel(1, .5, .5, 1, 0, 1), 1);
      expect(posterizeChannel(.4, 4), closeTo(1 / 3, 1e-9));
    },
  );
  test(
    'shader Posterize produces four real tones and preserves alpha',
    () async {
      final input = await fixture();
      final result = await render(input, PixelEffectFrame(2, [4]));
      final source = await rgba(input), output = await rgba(result);
      final red = <int>{};
      for (var i = 0; i < output.length; i += 4) {
        expect(output[i + 3], source[i + 3]);
        if (output[i + 3] == 255) red.add(output[i]);
        if (output[i + 3] == 0) expect(output.sublist(i, i + 3), [0, 0, 0]);
      }
      expect(red, {0, 85, 170, 255});
      input.dispose();
      result.dispose();
    },
  );
  test(
    'Levels shader agrees with CPU reference including selected channel',
    () async {
      final input = await fixture();
      final result = await render(
        input,
        PixelEffectFrame(1, [0, 1, 2, 0, 1, 1]),
      );
      final source = await rgba(input), output = await rgba(result);
      for (var x = 0; x < 128; x++) {
        final i = (40 * 128 + x) * 4;
        expect(
          output[i],
          closeTo(levelsChannel(source[i] / 255, 0, 1, 2, 0, 1) * 255, 1.2),
        );
        expect(output[i + 1], source[i + 1]);
        expect(output[i + 2], source[i + 2]);
      }
      input.dispose();
      result.dispose();
    },
  );
  test(
    'blur kernel conserves a constant image, opacity and brightness',
    () async {
      final input = await fixture(solid: true);
      for (final frame in [
        PixelEffectFrame(15, [.8, 0, 16]),
        PixelEffectFrame(15, [.8, 1, 16]),
        PixelEffectFrame(16, [80, 45]),
      ]) {
        final result = await render(input, frame), bytes = await rgba(result);
        for (var i = 0; i < bytes.length; i += 4) {
          expect(bytes[i], closeTo(100, 1));
          expect(bytes[i + 3], 255);
        }
        result.dispose();
      }
      input.dispose();
    },
  );
  test(
    'every shader effect renders deterministically and creates visual fixtures',
    () async {
      final input = await fixture();
      final dir = Directory('build/qa/effects-v2')..createSync(recursive: true);
      for (final type in pixelKernels.keys) {
        final effect = EffectInstance(type: type);
        final frame = PixelEffectFrame.of(
          effect,
          const Duration(milliseconds: 800),
        );
        final first = await render(input, frame),
            second = await render(input, frame);
        expect(await rgba(first), await rgba(second), reason: type.name);
        if (const bool.fromEnvironment('EFFECT_SCREENSHOTS')) {
          File('${dir.path}/${effect.spec.id}.png').writeAsBytesSync(
            (await first.toByteData(format: ui.ImageByteFormat.png))!.buffer
                .asUint8List(),
          );
        }
        first.dispose();
        second.dispose();
      }
      input.dispose();
    },
  );
  test('animated inputs are evaluated at layer-local time without changing saved params', () {
    final effect = EffectInstance(
      type: EffectType.posterize,
      params: {
        'niveis': AnimatedDouble(4)
            .withKeyframe(Duration.zero, 4)
            .withKeyframe(const Duration(seconds: 2), 12),
      },
    );
    expect(
      PixelEffectFrame.of(effect, const Duration(seconds: 1)).values.first,
      8,
    );
    expect(effect.track('niveis').keyframes.length, 2);
  });
  test('effect order changes the rendered result', () async {
    final input = await fixture();
    final levels = PixelEffectFrame(1, [0, 1, 2, 0, 1, 0]),
        poster = PixelEffectFrame(2, [4]);
    final a = await render(input, levels), b = await render(input, poster);
    final ab = await render(a, poster), ba = await render(b, levels);
    expect(await rgba(ab), isNot(equals(await rgba(ba))));
    for (final image in [input, a, b, ab, ba]) {
      image.dispose();
    }
  });
}
