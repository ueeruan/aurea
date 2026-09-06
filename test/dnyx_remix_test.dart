import 'dart:convert';
import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/application/font_service.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/domain/shape_library.dart';
import 'package:aurea/src/features/projects/application/dnyx_remix_assets.dart';
import 'package:aurea/src/features/projects/domain/dnyx_remix_template.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  test('309 quadros, assinatura editavel, sem video achatado ou watermark', () {
    final p = buildDnyxRemixTemplate({
      for (final n in dnyxAssetNames) n: 'assets/$n',
    });
    expect(p.outputWidth, 576);
    expect(p.outputHeight, 576);
    expect(p.duration, dnyxFrame(309));
    expect(p.layers.whereType<VideoLayer>(), isEmpty);
    expect(p.layers.map((l) => l.id).toSet().length, p.layers.length);
    final end = p.layerById('dnyx_signature')! as TextLayer;
    expect(end.text, 'Aurea App - RMK Dnyx');
    expect(resolveFontFamily(end.fontFamily), 'Aurea Motion Sans');
    expect(
      p.layers.any((l) => l.name.toLowerCase().contains('tiktok')),
      isFalse,
    );
    for (var q = 0; q < 309; q++) {
      for (final l in p.layers.where((l) => l.activeAt(dnyxFrame(q)))) {
        expect(
          l.position.valueAt(l.localTime(dnyxFrame(q))).dx.isFinite,
          isTrue,
        );
      }
    }
    final restored = projectFromJson(
      jsonDecode(jsonEncode(projectToJson(p))) as Map<String, dynamic>,
    );
    expect(restored.layers.length, p.layers.length);
    expect((restored.layerById('dnyx_signature')! as TextLayer).text, end.text);
  });
  test('cursores sao vetores editaveis disponiveis na biblioteca', () {
    for (final name in ['Cursor seta', 'Cursor mao']) {
      final entry = shapeLibrary.singleWhere((e) => e.nome == name);
      final items = entry.build();
      expect(items.whereType<ShapeBezier>(), isNotEmpty);
      expect(evaluateShape(items, Duration.zero), isNotEmpty);
    }
  });
  test(
    'modelo copia todas as midias e reabre sem depender de Downloads',
    () async {
      final dir = await Directory.systemTemp.createTemp('aurea-dnyx-test-');
      const channel = MethodChannel('plugins.flutter.io/path_provider');
      final messenger =
          TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;
      messenger.setMockMethodCallHandler(channel, (call) async => dir.path);
      addTearDown(() async {
        messenger.setMockMethodCallHandler(channel, null);
        await dir.delete(recursive: true);
      });
      final first = (await prepareDnyxRemix()).comIdNovo();
      final again = (await prepareDnyxRemix()).comIdNovo();
      expect(first.id, isNot(again.id));
      for (final layer in again.layers) {
        final path = switch (layer) {
          ImageLayer l => l.sourcePath,
          AudioLayer l => l.sourcePath,
          _ => null,
        };
        if (path != null) {
          expect(path, startsWith(dir.path));
          expect(await File(path).length(), greaterThan(0));
        }
      }
    },
  );
}
