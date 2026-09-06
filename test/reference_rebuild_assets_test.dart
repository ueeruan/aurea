import 'dart:io';

import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/projects/application/reference_rebuild_assets.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('modelo inclui audio local persistente e pode ser reaberto', () async {
    final directory = await Directory.systemTemp.createTemp(
      'aurea-model-test-',
    );
    const channel = MethodChannel('plugins.flutter.io/path_provider');
    final messenger =
        TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;
    messenger.setMockMethodCallHandler(channel, (call) async {
      if (call.method == 'getApplicationDocumentsDirectory') {
        return directory.path;
      }
      throw MissingPluginException(call.method);
    });
    addTearDown(() async {
      messenger.setMockMethodCallHandler(channel, null);
      await directory.delete(recursive: true);
    });

    final project = (await prepareReferenceRebuild()).comIdNovo();
    final audio = project.layers.whereType<AudioLayer>().single;
    final bundled = await rootBundle.load(
      'assets/templates/reference-rebuild-audio.m4a',
    );
    final file = File(audio.sourcePath);
    expect(await file.exists(), isTrue);
    expect(
      await file.readAsBytes(),
      bundled.buffer.asUint8List(bundled.offsetInBytes, bundled.lengthInBytes),
    );
    expect(file.path, startsWith(directory.path));
    final thumbnail = await rootBundle.load(
      'assets/templates/reference-rebuild.jpg',
    );
    expect(thumbnail.lengthInBytes, greaterThan(0));

    final restored = projectFromJson(projectToJson(project));
    expect(restored.layers.whereType<AudioLayer>().single.sourcePath, file.path);
    final reopened = (await prepareReferenceRebuild()).comIdNovo();
    expect(reopened.id, isNot(project.id));
    expect(reopened.layers.whereType<AudioLayer>().single.sourcePath, file.path);
  });
}
