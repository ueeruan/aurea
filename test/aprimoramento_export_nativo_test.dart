// APRIMORAMENTO DA EXPORTACAO DE VERDADE (libaurea_enhance, ae_process_png)
// pelo isolate de trabalho que o motor de exportacao usa.
//
//   AUREA_ENHANCE_LIB=<caminho>/aurea_enhance.dll flutter test test/aprimoramento_export_nativo_test.dart
// AUREA_IA_GPU=1 usa a GPU (padrao: CPU). Sem a DLL, o teste e pulado.
import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:aurea/src/features/editor/domain/aprimoramento_ia.dart';
import 'package:aurea/src/features/enhance/application/native_enhancer.dart';
import 'package:aurea/src/features/export/application/aprimoramento_export.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:image/image.dart' as img;

const _modelo = 'assets/ai/realesr-animevideov3';

/// A pasta do perfil de video real como o app monta: x4.param, x4.bin e
/// x4-wdn.bin juntos (os assets moram em duas pastas).
String _pastaDoVideoReal() {
  final dir = Directory.systemTemp.createTempSync('aurea-ia-geral-');
  File('assets/ai/realesr-general-x4v3/x4.param').copySync('${dir.path}/x4.param');
  File('assets/ai/realesr-general-x4v3/x4.bin').copySync('${dir.path}/x4.bin');
  File('assets/ai/realesr-general-wdn-x4v3/x4.bin').copySync('${dir.path}/x4-wdn.bin');
  return dir.path;
}

/// Quadro com bordas, gradiente e texto de blocos: tem o que ampliar.
img.Image _quadro(int w, int h, int semente) {
  final r = math.Random(semente);
  final im = img.Image(width: w, height: h);
  for (final p in im) {
    final borda = ((p.x ~/ 5) + (p.y ~/ 5)).isEven ? 190 : 50;
    p
      ..r = borda
      ..g = (p.x * 255 ~/ w)
      ..b = ((p.y * 255 ~/ h) + r.nextInt(24)).clamp(0, 255);
  }
  return im;
}

Uint8List _rgb(img.Image im) {
  final out = Uint8List(im.width * im.height * 3);
  var i = 0;
  for (final p in im) {
    out[i++] = p.r.toInt();
    out[i++] = p.g.toInt();
    out[i++] = p.b.toInt();
  }
  return out;
}

double _psnr(Uint8List a, Uint8List b) {
  var mse = 0.0;
  for (var i = 0; i < a.length; i++) {
    final d = a[i] - b[i];
    mse += d * d;
  }
  mse /= a.length;
  return mse == 0 ? 99 : 10 * math.log(255 * 255 / mse) / math.ln10;
}

void main() {
  final lib = Platform.environment['AUREA_ENHANCE_LIB'];
  final pular = lib == null || lib.isEmpty ? 'defina AUREA_ENHANCE_LIB com a DLL do host' : null;
  final gpu = Platform.environment['AUREA_IA_GPU'] == '1';

  test('cada quadro sai aprimorado, no encaixe da composicao e no lugar', () async {
    final dir = Directory.systemTemp.createTempSync('aurea-ia-export-');
    try {
      // Fonte 160x90 numa composicao 640x360: x4 exato.
      final arquivos = <String>[];
      for (var k = 0; k < 3; k++) {
        final caminho = '${dir.path}/${k.toString().padLeft(6, '0')}.png';
        File(caminho).writeAsBytesSync(img.encodePng(_quadro(160, 90, k)));
        arquivos.add(caminho);
      }
      // E um quadro de proporcao diferente (em pe), que encaixa na altura.
      final emPe = '${dir.path}/000003.png';
      File(emPe).writeAsBytesSync(img.encodePng(_quadro(90, 160, 9)));
      arquivos.add(emPe);

      final avancos = <int>[];
      await AprimoradorIa(modelosProntos: (_) => _modelo, exigirGpu: gpu).aprimorar(
        arquivos: arquivos,
        forca: 1,
        larguraDaComposicao: 640,
        alturaDaComposicao: 360,
        perfil: PerfilDoAprimoramento.animacao,
        aoAvancar: (f, _) => avancos.add(f),
      );
      expect(avancos.last, 4);
      // Referencia: a ampliacao convencional do proprio motor (o que forca 0
      // daria). A rede tem de mudar a imagem, sem virar outra imagem.
      final motor = NativeEnhancer.open(_modelo, gpu: false);
      try {
        for (var k = 0; k < 3; k++) {
          final saida = img.decodePng(File(arquivos[k]).readAsBytesSync())!;
          expect((saida.width, saida.height), (640, 360));
          final convencional = motor.resize(_rgb(_quadro(160, 90, k)), 160, 90, 640, 360);
          final p = _psnr(_rgb(saida), convencional);
          expect(p, lessThan(40), reason: 'a rede muda a imagem (quadro $k: $p dB)');
          expect(p, greaterThan(18), reason: 'mas continua sendo o mesmo quadro (quadro $k: $p dB)');
        }
      } finally {
        motor.close();
      }
      final pe = img.decodePng(File(emPe).readAsBytesSync())!;
      expect((pe.width, pe.height), encaixar(90, 160, 640, 360));
      expect(dir.listSync().where((f) => f.path.endsWith('.tmp')), isEmpty);
    } finally {
      dir.deleteSync(recursive: true);
    }
  }, skip: pular, timeout: const Timeout(Duration(minutes: 5)));

  test('forca 0 devolve a ampliacao convencional do motor (sem inventar detalhe)', () async {
    final dir = Directory.systemTemp.createTempSync('aurea-ia-forca-');
    try {
      final caminho = '${dir.path}/000000.png';
      File(caminho).writeAsBytesSync(img.encodePng(_quadro(160, 90, 3)));
      await AprimoradorIa(modelosProntos: (_) => _modelo, exigirGpu: gpu).aprimorar(
        arquivos: [caminho],
        forca: 0,
        larguraDaComposicao: 320,
        alturaDaComposicao: 180,
        perfil: PerfilDoAprimoramento.animacao,
      );
      final saida = img.decodePng(File(caminho).readAsBytesSync())!;
      expect((saida.width, saida.height), (320, 180));
      // A referencia e o Catmull-Rom do proprio motor (ae_resize): forca 0
      // e a mistura inteira para o original ampliado.
      final motor = NativeEnhancer.open(_modelo, gpu: false);
      try {
        final convencional = motor.resize(_rgb(_quadro(160, 90, 3)), 160, 90, 320, 180);
        expect(_psnr(_rgb(saida), convencional), 99);
      } finally {
        motor.close();
      }
    } finally {
      dir.deleteSync(recursive: true);
    }
  }, skip: pular, timeout: const Timeout(Duration(minutes: 5)));

  test('video real: a reducao de ruido muda o quadro de verdade, e 50% fica entre as pontas', () async {
    final modelo = _pastaDoVideoReal();
    final dir = Directory.systemTemp.createTempSync('aurea-ia-ruido-');
    try {
      Future<Uint8List> com(double ruido) async {
        final caminho = '${dir.path}/r${(ruido * 100).round()}.png';
        File(caminho).writeAsBytesSync(img.encodePng(_quadro(160, 90, 5)));
        await AprimoradorIa(modelosProntos: (_) => modelo, exigirGpu: gpu).aprimorar(
          arquivos: [caminho],
          forca: 1,
          larguraDaComposicao: 640,
          alturaDaComposicao: 360,
          perfil: PerfilDoAprimoramento.videoReal,
          reducaoDeRuido: ruido,
        );
        final saida = img.decodePng(File(caminho).readAsBytesSync())!;
        expect((saida.width, saida.height), (640, 360));
        return _rgb(saida);
      }

      final limpa = await com(1), grao = await com(0), meio = await com(.5);
      final pontas = _psnr(limpa, grao);
      expect(pontas, lessThan(45), reason: 'os dois modelos dao quadros diferentes ($pontas dB)');
      // A mistura dos pesos cai perto do meio: mais parecida com cada ponta
      // do que as pontas entre si.
      expect(_psnr(meio, limpa), greaterThan(pontas));
      expect(_psnr(meio, grao), greaterThan(pontas));
    } finally {
      dir.deleteSync(recursive: true);
      Directory(modelo).deleteSync(recursive: true);
    }
  }, skip: pular, timeout: const Timeout(Duration(minutes: 5)));

  test('quadro ilegivel para a exportacao com motivo e nao apaga o arquivo', () async {
    final dir = Directory.systemTemp.createTempSync('aurea-ia-ruim-');
    try {
      final ruim = File('${dir.path}/000000.png')..writeAsBytesSync([1, 2, 3, 4]);
      await expectLater(
        AprimoradorIa(modelosProntos: (_) => _modelo, exigirGpu: gpu).aprimorar(
          arquivos: [ruim.path],
          forca: 1,
          larguraDaComposicao: 640,
          alturaDaComposicao: 360,
          perfil: PerfilDoAprimoramento.animacao,
        ),
        throwsA(isA<StateError>()),
      );
      expect(ruim.readAsBytesSync(), [1, 2, 3, 4]);
    } finally {
      dir.deleteSync(recursive: true);
    }
  }, skip: pular);
}
