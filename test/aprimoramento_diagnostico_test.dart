// DIAGNOSTICO DO APRIMORAMENTO ANTIGO (beta 77), registrado ANTES da troca.
//
// Cada teste prende um defeito real do caminho atual. Eles passam hoje
// porque o defeito existe; quando o motor novo entrar, estes testes viram
// o contrario (e sao reescritos) — e a prova de que a troca mudou o que
// estava errado.
import 'dart:io';

import 'package:aurea/src/features/enhance/application/enhance_worker.dart';
import 'package:aurea/src/features/enhance/domain/color_look.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:image/image.dart' as img;

img.Image _ruido() {
  final im = img.Image(width: 32, height: 32, numChannels: 4);
  for (final p in im) {
    final v = ((p.x * 37 + p.y * 91) % 97) * 2.0 + 30;
    p
      ..r = v
      ..g = 255 - v
      ..b = (v * 3) % 255
      ..a = 255;
  }
  return im;
}

List<num> _canais(img.Image im) => [
  for (final p in im) ...[p.r, p.g, p.b],
];

void main() {
  test('DEFEITO 1: "denoise", "detalhe" e "clareza" sao o MESMO unsharp', () {
    // applyEnhancement soma os tres num unico `amount` de unsharp mask. Dois
    // ajustes diferentes com a mesma soma dao imagens IDENTICAS: nao existe
    // reducao de ruido, so nitidez com sinal trocado.
    const a = EnhanceSettings(ai: false, detail: .5, denoise: 0, look: ColorLook.natural);
    const b = EnhanceSettings(ai: false, detail: .8, denoise: .3, look: ColorLook.natural);
    final ia = _canais(applyEnhancement(_ruido(), a));
    final ib = _canais(applyEnhancement(_ruido(), b));
    expect(ia, orderedEquals(ib), reason: 'sliders diferentes, mesmo resultado');
  });

  test('DEFEITO 2: o video e reamostrado a 30 fps fixos, qualquer que seja a fonte', () {
    final job = File('lib/src/features/enhance/application/enhancement_job.dart').readAsStringSync();
    expect(job.contains('const fps = 30'), isTrue, reason: '24/25/60 fps e VFR mudam de ritmo');
  });

  test('DEFEITO 3: quadros extraidos como PNG em disco e processados pixel a pixel em Dart', () {
    final job = File('lib/src/features/enhance/application/enhancement_job.dart').readAsStringSync();
    final worker = File('lib/src/features/enhance/application/enhance_worker.dart').readAsStringSync();
    expect(job.contains('frame-%03d.png'), isTrue);
    // getPixel/setPixelRgba por pixel do quadro ampliado: um 720p x2 sao 3,7
    // milhoes de chamadas por quadro, na CPU, num isolate.
    expect(worker.contains('result.setPixelRgba('), isTrue);
    expect(worker.contains('InterpreterOptions()..threads = 2'), isTrue);
  });

  test('DEFEITO 4: o "modelo de IA" tem 33 KB e so aceita tiles de 320x180', () {
    final bytes = File('assets/ai/compressed_esrgan.tflite').readAsBytesSync();
    expect(bytes.length, 33768, reason: 'rede x4 destilada ao extremo (GSOC 2019)');
    final readme = File('assets/ai/README.md').readAsStringSync();
    expect(readme.contains('[1,180,320,3]'), isTrue);
  });

  test('DEFEITO 5: o aprimoramento vive fora do editor e nao entra na exportacao do projeto', () {
    final export = File('lib/src/features/export/application/export_engine.dart').readAsStringSync();
    final screen = File('lib/src/features/export/presentation/export_video_screen.dart').readAsStringSync();
    expect(export.contains('enhance'), isFalse);
    expect(screen.contains('enhance'), isFalse);
  });
}
