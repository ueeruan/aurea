import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:file_picker/file_picker.dart';
import 'package:image/image.dart' as img;
import 'package:aurea/src/features/enhance/domain/color_look.dart';
import 'package:aurea/src/features/enhance/application/enhance_worker.dart';
import 'package:aurea/src/features/enhance/presentation/enhance_screen.dart';

class _Picker extends FilePicker {
  @override
  Future<FilePickerResult?> pickFiles({
    String? dialogTitle,
    String? initialDirectory,
    FileType type = FileType.any,
    List<String>? allowedExtensions,
    Function(FilePickerStatus)? onFileLoading,
    bool allowCompression = true,
    int compressionQuality = 30,
    bool allowMultiple = false,
    bool withData = false,
    bool withReadStream = false,
    bool lockParentWindow = false,
    bool readSequential = false,
  }) async => FilePickerResult([
    PlatformFile(name: 'sample.png', size: 1, path: 'sample.png'),
  ]);
}

void main() {
  test('color recipes support neutral strength, mono, limits and alpha', () {
    for (final look in ColorLook.values) {
      expect(look.apply(51, 100, 231, 0), (51.0, 100.0, 231.0));
      final c = look.apply(0, 255, 240, 1);
      for (final v in [c.$1, c.$2, c.$3]) {
        expect(v, inInclusiveRange(0, 255));
      }
    }
    final mono = ColorLook.mono.apply(51, 100, 231, 1);
    expect(mono.$1, closeTo(mono.$2, 1e-9));
    expect(mono.$2, closeTo(mono.$3, 1e-9));
    final original = img.Image(width: 2, height: 2, numChannels: 4);
    original.setPixelRgba(0, 0, 30, 80, 120, 47);
    final converted = applyEnhancement(
      original,
      const EnhanceSettings(ai: false, look: ColorLook.warm, detail: 0),
    );
    expect(converted.getPixel(0, 0).a, 47);
    expect(converted.getPixel(0, 0).r, 42);
    expect(const EnhanceSettings().outputSize(321, 181), (642, 362));
    expect(const EnhanceSettings(ai: false).outputSize(321, 181, video: true), (
      320,
      180,
    ));
  });
  test('modelo Real-ESRGAN ncnn empacotado e o verificado (tamanho e cabecalho)', () {
    final param = File('assets/ai/realesr-animevideov3/x4.param').readAsStringSync();
    expect(param.startsWith('7767517'), isTrue, reason: 'magic do .param do ncnn');
    expect(param.contains(' data'), isTrue);
    expect(param.contains(' output'), isTrue);
    expect(File('assets/ai/realesr-animevideov3/x4.bin').lengthSync(), 1247368);
    expect(File('assets/ai/compressed_esrgan.tflite').existsSync(), isFalse);
  });
  testWidgets('standalone enhancement controls fit a small phone', (
    tester,
  ) async {
    FilePicker.platform = _Picker();
    tester.view.devicePixelRatio = 1;
    tester.view.physicalSize = const Size(320, 568);
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    await tester.pumpWidget(
      MaterialApp(theme: ThemeData.dark(), home: const EnhanceScreen()),
    );
    await tester.tap(find.text('Escolher foto ou vídeo'));
    await tester.pumpAndSettle();
    await tester.scrollUntilVisible(find.text('CCs • Correção de cor'), 200);
    expect(find.text('CCs • Correção de cor'), findsOneWidget);
    await tester.scrollUntilVisible(find.text('Gerar resultado'), 200);
    expect(find.text('Comparar antes e depois'), findsOneWidget);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });
}
