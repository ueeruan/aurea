import 'dart:convert';
import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/projects/application/vhf_motion_assets.dart';
import 'package:aurea/src/features/projects/domain/vhf_motion_template.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  test(
    'gradient interpolates colors, supports editing, preserves legacy fills',
    () {
      const red = Color(0xffff0000), blue = Color(0xff0000ff);
      final base = ShapeGradientFill(colorA: red, colorB: blue);
      expect(base.colorsAt(const Duration(seconds: 4)), [red, blue]);
      final animated = base
          .withColorsAt(Duration.zero, [red, blue])
          .withColorsAt(const Duration(seconds: 2), [blue, red]);
      expect(
        animated.colorsAt(const Duration(seconds: 1)).first,
        Color.lerp(red, blue, .5),
      );
      expect(animated.colorsAt(const Duration(seconds: 5)), [blue, red]);
      expect(animated.copyWith(angleDeg: 90).colorFrames, hasLength(2));
      final updated = animated.withColorsAt(Duration.zero, [blue, blue]);
      expect(updated.colorFrames, hasLength(2));
      expect(updated.colorsAt(Duration.zero), [blue, blue]);
      expect(animated.colorsAt(Duration.zero), [red, blue]);
      final malformed = base.copyWith(
        colorFrames: [
          Keyframe(time: Duration.zero, value: [red]),
        ],
      );
      expect(malformed.colorsAt(Duration.zero), [red, blue]);
    },
  );

  test('234 source timestamps and 12 scenes are native editable geometry', () {
    final p = buildVhfMotionTemplate();
    expect(p.outputWidth, 720);
    expect(p.outputHeight, 1280);
    expect(p.duration, vhfFrame(234));
    expect(p.markers, hasLength(12));
    expect(p.layers.whereType<VideoLayer>(), isEmpty);
    expect(p.layers.whereType<ImageLayer>(), isEmpty);
    expect(p.layers.map((l) => l.id).toSet().length, p.layers.length);
    expect(p.layers.whereType<ShapeLayer>().length, greaterThan(100));
    for (var q = 0; q < 234; q++) {
      final t = vhfFrame(q);
      final active = p.layers.where((l) => l.activeAt(t)).toList();
      expect(active, isNotEmpty, reason: 'frame $q');
      for (final l in active) {
        expect(l.position.valueAt(l.localTime(t)).dx.isFinite, isTrue);
        expect(l.position.valueAt(l.localTime(t)).dy.isFinite, isTrue);
      }
    }
    final encoded = jsonEncode(projectToJson(p));
    expect(
      encoded.length,
      lessThan(12000000),
      reason: 'native project must remain practical on iPhone',
    );
    final restored = projectFromJson(
      jsonDecode(encoded) as Map<String, dynamic>,
    );
    expect(restored.layers, hasLength(p.layers.length));
    final gradient = (restored.layerById('vhf_s05_bg')! as ShapeLayer).contents
        .whereType<ShapeGradientFill>()
        .single;
    expect(gradient.colorFrames, hasLength(20));
    expect(
      restored.layerById('vhf_s05_bg')!.moduleTimesUs,
      contains((vhfFrame(87) - vhfFrame(86)).inMicroseconds),
    );
    for (final q in [0, 1, 9, 19]) {
      final original = (p.layerById('vhf_s05_bg')! as ShapeLayer).contents
          .whereType<ShapeGradientFill>()
          .single;
      expect(gradient.colorsAt(vhfFrame(q)), original.colorsAt(vhfFrame(q)));
    }
  });

  test(
    'bundled motion opens offline from durable audio and can be reopened',
    () async {
      final directory = await Directory.systemTemp.createTemp(
        'aurea-vhf-test-',
      );
      const channel = MethodChannel('plugins.flutter.io/path_provider');
      final messenger =
          TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;
      messenger.setMockMethodCallHandler(
        channel,
        (call) async => directory.path,
      );
      addTearDown(() async {
        messenger.setMockMethodCallHandler(channel, null);
        await directory.delete(recursive: true);
      });
      final a = (await prepareVhfMotion()).comIdNovo(),
          b = (await prepareVhfMotion()).comIdNovo();
      expect(a.id, isNot(b.id));
      final audio = b.layers.whereType<AudioLayer>().single;
      expect(File(audio.sourcePath).existsSync(), isTrue);
      expect(audio.sourcePath, startsWith(directory.path));
      final bundled = await rootBundle.load('assets/templates/vhf/audio.m4a');
      expect(
        File(audio.sourcePath).readAsBytesSync(),
        bundled.buffer.asUint8List(),
      );
    },
  );
}
