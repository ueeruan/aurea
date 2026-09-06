import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/media/application/media_import_service.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:image_picker/image_picker.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  late Directory root;
  late MediaImportService service;
  setUp(() async {
    root = await Directory.systemTemp.createTemp('aurea-import-test-');
    service = MediaImportService(null, () async => root);
  });
  tearDown(() async => root.delete(recursive: true));

  test('gallery photo survives deletion of picker cache and preserves original bytes', () async {
    final recorder = ui.PictureRecorder();
    final canvas = ui.Canvas(recorder);
    canvas.drawRect(
      const ui.Rect.fromLTWH(0, 0, 12, 6),
      ui.Paint()..color = const ui.Color(0xffff0000),
    );
    canvas.drawRect(
      const ui.Rect.fromLTWH(0, 6, 12, 6),
      ui.Paint()..color = const ui.Color(0xff0000ff),
    );
    final picture = recorder.endRecording();
    final image = await picture.toImage(12, 12);
    final png = (await image.toByteData(format: ui.ImageByteFormat.png))!.buffer
        .asUint8List();
    image.dispose();
    picture.dispose();
    final source = await File('${root.path}/picker.png').writeAsBytes(png);
    final saved = await service.persist(XFile(source.path), image: true);
    await source.delete();
    expect(File(saved.path).existsSync(), isTrue);
    expect(await File(saved.path).readAsBytes(), png);
    expect(saved.path, contains('imported_media'));
  });

  test('invalid image is rejected and partial copy is removed', () async {
    final source = await File('${root.path}/broken.jpg')
        .writeAsBytes(utf8.encode('not an image'));
    await expectLater(
      service.persist(XFile(source.path), image: true),
      throwsA(anything),
    );
    expect(
      await Directory('${root.path}/imported_media').list().toList(),
      isEmpty,
    );
    expect(source.existsSync(), isTrue);
  });

  test(
    'empty imports are rejected and identical names do not overwrite',
    () async {
      final empty = await File('${root.path}/empty.mp4').writeAsBytes([]);
      await expectLater(
        service.persist(XFile(empty.path)),
        throwsFormatException,
      );
      final source = await File('${root.path}/clip.mp4')
          .writeAsBytes(Uint8List.fromList([1, 2, 3]));
      final first = await service.persist(XFile(source.path));
      final second = await service.persist(XFile(source.path));
      expect(first.path, isNot(second.path));
      expect(await File(first.path).readAsBytes(), [1, 2, 3]);
    },
  );
}
