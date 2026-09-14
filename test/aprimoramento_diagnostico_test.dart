// O APRIMORAMENTO ANTIGO (beta 77) e o que a troca corrigiu.
//
// Os defeitos 1 a 4 foram presos em teste no commit 4f3f380 e agora estao
// invertidos: o teste prova que o caminho novo nao os tem. O defeito 5
// (efeito fora do editor e da exportacao do projeto) CONTINUA — o motor
// novo ainda so atende a tela de aprimoramento.
import 'dart:io';

import 'package:aurea/src/features/enhance/application/enhancement_job.dart';
import 'package:aurea/src/features/enhance/domain/color_look.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('CORRIGIDO 1: nao ha mais "reduzir ruido" falso somado ao unsharp', () {
    final worker = File('lib/src/features/enhance/application/enhance_worker.dart').readAsStringSync();
    expect(worker.contains('denoise'), isFalse);
    const s = EnhanceSettings();
    expect(s.aiStrength, 1, reason: 'intensidade da IA e um controle proprio (mistura com o original)');
    expect(s.detail, 0, reason: 'nitidez desligada por padrao');
  });

  test('CORRIGIDO 2: o video usa a taxa real da fonte (sem 30 fps fixos)', () {
    final job = File('lib/src/features/enhance/application/enhancement_job.dart').readAsStringSync();
    expect(job.contains('const fps = 30'), isFalse);
    final p = EnhancementJob.parseProbe('{"streams":[{"width":1920,"height":1080,"avg_frame_rate":"24000/1001","side_data_list":[{"rotation":-90}]}],"format":{"duration":"12.5"}}');
    expect(p.fps, 24);
    expect((p.width, p.height), (1080, 1920), reason: 'rotacao de 90 graus troca os lados');
    expect(p.seconds, 12.5);
    final vfr = EnhancementJob.parseProbe('{"streams":[{"width":640,"height":360,"avg_frame_rate":"0/0","r_frame_rate":"60/1"}],"format":{"duration":"3"}}');
    expect(vfr.fps, 60);
  });

  test('CORRIGIDO 3: sem PNG por quadro — quadros crus por pipe e inferencia nativa', () {
    final job = File('lib/src/features/enhance/application/enhancement_job.dart').readAsStringSync();
    final worker = File('lib/src/features/enhance/application/enhance_worker.dart').readAsStringSync();
    expect(job.contains('frame-%03d.png'), isFalse);
    expect(job.contains("'rawvideo'"), isTrue);
    expect(job.contains('registerNewFFmpegPipe'), isTrue);
    expect(worker.contains('tflite'), isFalse);
    expect(worker.contains('NativeEnhancer.open'), isTrue);
  });

  test('CORRIGIDO 4: modelo real do Real-ESRGAN (ncnn), nao o TFLite de 33 KB', () {
    expect(File('assets/ai/compressed_esrgan.tflite').existsSync(), isFalse);
    expect(File('assets/ai/realesr-animevideov3/x4.bin').lengthSync(), 1247368);
    final pubspec = File('pubspec.yaml').readAsStringSync();
    expect(pubspec.contains('tflite_flutter'), isFalse);
  });

  test('AINDA ABERTO 5: o aprimoramento nao entra na exportacao do projeto do editor', () {
    final export = File('lib/src/features/export/application/export_engine.dart').readAsStringSync();
    expect(export.contains('enhance'), isFalse);
  });
}
