// RIFE DE VERDADE (libaurea_enhance) pela ponte FFI e pelo isolate de
// trabalho que a exportacao usa.
//
// No Android a biblioteca vem do CMake do app. No host o teste precisa da
// DLL construida com o ncnn do Windows (ver native/enhance/README.md):
//   AUREA_ENHANCE_LIB=<caminho>/aurea_enhance.dll flutter test test/rife_motor_nativo_test.dart
// AUREA_RIFE_GPU=1 usa a GPU (padrao: CPU, que roda em qualquer maquina).
// Sem a DLL, o teste e pulado — nunca finge que rodou.
import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:aurea/src/features/editor/domain/plano_de_interpolacao.dart';
import 'package:aurea/src/features/enhance/application/native_interpolator.dart';
import 'package:aurea/src/features/export/application/interpolacao_rife.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:image/image.dart' as img;

const _modelo = 'assets/ai/rife-v4.6';

/// Textura suave (sem padrao periodico, que confunde qualquer fluxo) vista
/// por uma janela deslocada [dx] pixels.
Uint8List _janela(int w, int h, int dx) {
  final out = Uint8List(w * h * 3);
  for (var y = 0; y < h; y++) {
    for (var x = 0; x < w; x++) {
      final u = (x + dx).toDouble(), v = y.toDouble();
      final i = (y * w + x) * 3;
      out[i] = (128 + 90 * math.sin(u / 23.0) * math.cos(v / 31.0) + 30 * math.sin((u + 2 * v) / 9.0)).round().clamp(0, 255);
      out[i + 1] = (128 + 80 * math.cos(u / 17.0 + v / 29.0) + 35 * math.sin(u / 7.0)).round().clamp(0, 255);
      out[i + 2] = (128 + 70 * math.sin((u - v) / 19.0) + 40 * math.cos(v / 11.0)).round().clamp(0, 255);
    }
  }
  return out;
}

double _psnrMiolo(Uint8List a, Uint8List b, int w, int h, int margem) {
  var mse = 0.0;
  var n = 0;
  for (var y = margem; y < h - margem; y++) {
    for (var x = margem; x < w - margem; x++) {
      for (var c = 0; c < 3; c++) {
        final i = (y * w + x) * 3 + c;
        final d = a[i] - b[i];
        mse += d * d;
        n++;
      }
    }
  }
  mse /= n;
  return mse == 0 ? 99 : 10 * math.log(255 * 255 / mse) / math.ln10;
}

Uint8List _mistura(Uint8List a, Uint8List b, double t) => Uint8List.fromList([
  for (var i = 0; i < a.length; i++) ((1 - t) * a[i] + t * b[i]).round(),
]);

void _pngDe(Uint8List rgb, int w, int h, String caminho) {
  final image = img.Image.fromBytes(width: w, height: h, bytes: rgb.buffer, numChannels: 3);
  File(caminho).writeAsBytesSync(img.encodePng(image, level: 1));
}

Uint8List _rgbDoPng(String caminho) {
  final image = img.decodePng(File(caminho).readAsBytesSync())!;
  final out = Uint8List(image.width * image.height * 3);
  var i = 0;
  for (final p in image) {
    out[i++] = p.r.toInt();
    out[i++] = p.g.toInt();
    out[i++] = p.b.toInt();
  }
  return out;
}

void main() {
  final lib = Platform.environment['AUREA_ENHANCE_LIB'];
  final pular = lib == null || lib.isEmpty ? 'defina AUREA_ENHANCE_LIB com a DLL do host' : null;
  final gpu = Platform.environment['AUREA_RIFE_GPU'] == '1';

  test('movimento real: o quadro do meio e o deslocado, nao a mistura', () {
    final m = NativeInterpolator.open(_modelo, gpu: gpu);
    try {
      expect(m.info.gpu, gpu);
      const w = 256, h = 144, d = 16;
      final a = _janela(w, h, 0), b = _janela(w, h, d);
      for (final t in [.25, .5, .75]) {
        final meio = m.interpolate(a, b, w, h, t);
        final real = _janela(w, h, (d * t).round());
        final ia = _psnrMiolo(meio, real, w, h, 20);
        final mistura = _psnrMiolo(_mistura(a, b, t), real, w, h, 20);
        expect(ia, greaterThan(mistura + 8), reason: 't=$t: IA $ia dB x mistura $mistura dB');
        expect(ia, greaterThan(30), reason: 't=$t');
      }
      expect(m.interpolate(a, b, w, h, 0), a, reason: 't=0 copia A');
      expect(m.interpolate(a, b, w, h, 1), b, reason: 't=1 copia B');
    } finally {
      m.close();
    }
  }, skip: pular);

  test('corte de cena copia o vizinho mais perto; quadro repetido nao passa pela rede', () {
    const w = 128, h = 72;
    final dir = Directory.systemTemp.createTempSync('aurea-rife-corte-');
    final m = NativeInterpolator.open(_modelo, gpu: gpu);
    try {
      String png(String nome, Uint8List rgb) {
        final caminho = '${dir.path}/$nome.png';
        _pngDe(rgb, w, h, caminho);
        return caminho;
      }

      // Duas cenas sem nada em comum: textura contra um degrade liso escuro.
      final cenaA = png('a', _janela(w, h, 0));
      final escuro = Uint8List(w * h * 3);
      for (var i = 0; i < escuro.length; i += 3) {
        escuro[i] = 10 + (i ~/ 3) % w ~/ 8;
        escuro[i + 1] = 12;
        escuro[i + 2] = 30;
      }
      final cenaB = png('b', escuro);
      final corte = m.similarityPng(cenaA, cenaB);
      expect(corte, lessThan(.2), reason: 'semelhanca de um corte: $corte');
      m.interpolatePng(cenaA, cenaB, .25, '${dir.path}/c1.png');
      m.interpolatePng(cenaA, cenaB, .75, '${dir.path}/c3.png');
      expect(_rgbDoPng('${dir.path}/c1.png'), _janela(w, h, 0), reason: 'antes do meio: a cena A inteira');
      expect(_rgbDoPng('${dir.path}/c3.png'), escuro, reason: 'depois do meio: a cena B inteira');

      // O mesmo quadro duas vezes (taxa variavel): copia, identico.
      final igual = png('igual', _janela(w, h, 0));
      expect(m.similarityPng(cenaA, igual), greaterThan(.996));
      m.interpolatePng(cenaA, igual, .5, '${dir.path}/p.png');
      expect(_rgbDoPng('${dir.path}/p.png'), _janela(w, h, 0));

      // Movimento de verdade continua interpolado (nem corte nem parado).
      final andou = png('andou', _janela(w, h, 8));
      final s = m.similarityPng(cenaA, andou);
      expect(s, inExclusiveRange(.2, .996), reason: 'movimento: $s');

      // Com as regras desligadas, o corte volta a ser misturado pela rede.
      m.setThresholds(cut: -1, still: 2);
      m.interpolatePng(cenaA, cenaB, .25, '${dir.path}/sem.png');
      expect(_rgbDoPng('${dir.path}/sem.png'), isNot(_janela(w, h, 0)));
    } finally {
      m.close();
      dir.deleteSync(recursive: true);
    }
  }, skip: pular);

  test('modelo ausente e quadros ilegiveis falham com motivo', () {
    expect(() => NativeInterpolator.open('nao/existe', gpu: gpu), throwsA(isA<StateError>()));
    final m = NativeInterpolator.open(_modelo, gpu: gpu);
    final dir = Directory.systemTemp.createTempSync('aurea-rife-');
    try {
      expect(
        () => m.interpolatePng('${dir.path}/a.png', '${dir.path}/b.png', .5, '${dir.path}/c.png'),
        throwsA(isA<StateError>()),
      );
      expect(File('${dir.path}/c.png').existsSync(), isFalse);
    } finally {
      m.close();
      dir.deleteSync(recursive: true);
    }
  }, skip: pular);

  test('o isolate da exportacao escreve a sequencia do plano (copias exatas e meios certos)', () async {
    const w = 192, h = 108, d = 12;
    final dir = Directory.systemTemp.createTempSync('aurea-rife-seq-');
    try {
      final base = Directory('${dir.path}/base')..createSync();
      final saida = Directory('${dir.path}/saida')..createSync();
      // 4 quadros reais a 30 fps, andando d pixels por quadro.
      for (var k = 0; k < 4; k++) {
        _pngDe(_janela(w, h, k * d), w, h, '${base.path}/${k.toString().padLeft(6, '0')}.png');
      }
      final plano = planoDeInterpolacao(quadrosBase: 4, taxaBase: 30, taxaSaida: 120);
      final avancos = <int>[];
      await InterpoladorRife(modeloPronto: _modelo, exigirGpu: gpu).interpolar(
        pastaBase: base.path,
        pastaSaida: saida.path,
        plano: plano,
        aoAvancar: (feitos, total) => avancos.add(feitos),
      );
      expect(avancos.last, plano.length);
      for (final p in plano) {
        final arquivo = '${saida.path}/${p.saida.toString().padLeft(6, '0')}.png';
        expect(File(arquivo).existsSync(), isTrue, reason: 'saida ${p.saida}');
        final rgb = _rgbDoPng(arquivo);
        final real = _janela(w, h, ((p.a + p.t) * d).round());
        if (p.copia) {
          expect(rgb, _janela(w, h, p.a * d), reason: 'copia ${p.saida}');
        } else {
          expect(_psnrMiolo(rgb, real, w, h, 16), greaterThan(28), reason: 'saida ${p.saida} t=${p.t}');
        }
      }
      expect(saida.listSync().where((f) => f.path.endsWith('.tmp')), isEmpty);
    } finally {
      dir.deleteSync(recursive: true);
    }
  }, skip: pular, timeout: const Timeout(Duration(minutes: 5)));

  test('cancelar para o isolate entre quadros e avisa', () async {
    const w = 128, h = 72;
    final dir = Directory.systemTemp.createTempSync('aurea-rife-cancel-');
    try {
      final base = Directory('${dir.path}/base')..createSync();
      final saida = Directory('${dir.path}/saida')..createSync();
      for (var k = 0; k < 40; k++) {
        _pngDe(_janela(w, h, k * 4), w, h, '${base.path}/${k.toString().padLeft(6, '0')}.png');
      }
      final plano = planoDeInterpolacao(quadrosBase: 40, taxaBase: 30, taxaSaida: 120);
      var feitos = 0;
      await expectLater(
        InterpoladorRife(modeloPronto: _modelo, exigirGpu: gpu).interpolar(
          pastaBase: base.path,
          pastaSaida: saida.path,
          plano: plano,
          aoAvancar: (f, _) => feitos = f,
          cancelado: () => feitos >= 6,
        ),
        throwsA(isA<StateError>().having((e) => e.message, 'motivo', 'Cancelado')),
      );
      expect(feitos, lessThan(plano.length));
    } finally {
      dir.deleteSync(recursive: true);
    }
  }, skip: pular, timeout: const Timeout(Duration(minutes: 5)));
}
