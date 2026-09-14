// MOTOR NATIVO DE VERDADE (libaurea_enhance) chamado pela ponte FFI do app.
//
// No Android a biblioteca vem do CMake do app. No host o teste precisa da
// DLL construida com o ncnn do Windows (ver native/enhance/README.md):
//   AUREA_ENHANCE_LIB=<caminho>/aurea_enhance.dll flutter test test/aprimoramento_motor_nativo_test.dart
// Sem a variavel, o teste e pulado — nunca finge que rodou.
import 'dart:ffi';
import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:aurea/src/features/enhance/application/native_enhancer.dart';
import 'package:flutter_test/flutter_test.dart';

Uint8List _cena(int w, int h) {
  final out = Uint8List(w * h * 3);
  for (var y = 0; y < h; y++) {
    for (var x = 0; x < w; x++) {
      final i = (y * w + x) * 3;
      final borda = ((x ~/ 6) + (y ~/ 6)).isEven ? 200 : 40;
      out[i] = borda;
      out[i + 1] = (x * 255 ~/ w);
      out[i + 2] = (y * 255 ~/ h);
    }
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

  test('carrega o modelo, processa, respeita intensidade e cancela', () {
    final e = NativeEnhancer.open('assets/ai/realesr-animevideov3');
    try {
      final info = e.info;
      expect(info.tile, greaterThan(0));
      const w = 96, h = 64;
      final src = _cena(w, h);
      final ia = e.process(src, w, h, scale: 4);
      expect(ia.length, w * 4 * h * 4 * 3);
      expect(ia.reduce((a, b) => a > b ? a : b), greaterThan(100), reason: 'nao pode sair preto');
      final base = e.resize(src, w, h, w * 4, h * 4);
      expect(_psnr(e.process(src, w, h, scale: 4, strength: 0), base), 99, reason: 'intensidade 0 = original ampliado');
      expect(_psnr(ia, base), lessThan(60), reason: 'a IA muda a imagem');
      final dois = e.process(src, w, h, scale: 2);
      expect(dois.length, w * 2 * h * 2 * 3);
      e.cancel.value = 1;
      expect(() => e.process(src, w, h, scale: 4), throwsA(isA<StateError>()));
      e.cancel.value = 0;
    } finally {
      e.close();
    }
  }, skip: pular);

  test('mistura de pesos (reducao de ruido): abre, processa e recusa o que nao sabe misturar', () {
    const param = 'assets/ai/realesr-general-x4v3/x4.param';
    const limpa = 'assets/ai/realesr-general-x4v3/x4.bin';
    const grao = 'assets/ai/realesr-general-wdn-x4v3/x4.bin';
    const w = 48, h = 32;
    final src = _cena(w, h);
    Uint8List com(double peso) {
      final e = NativeEnhancer.openMixed(param: param, binA: limpa, binB: grao, weightA: peso, gpu: false);
      try {
        return e.process(src, w, h, scale: 4);
      } finally {
        e.close();
      }
    }

    final a = com(1), b = com(0);
    expect(_psnr(a, b), lessThan(60), reason: 'as duas pontas sao modelos diferentes');
    // A ponta 1 da mistura e o proprio modelo que limpa, carregado do disco.
    final direto = NativeEnhancer.open('assets/ai/realesr-general-x4v3', gpu: false);
    try {
      expect(_psnr(direto.process(src, w, h, scale: 4), a), greaterThan(50));
    } finally {
      direto.close();
    }
    expect(
      () => NativeEnhancer.openMixed(param: param, binA: limpa, binB: grao, weightA: double.nan, gpu: false),
      throwsA(isA<StateError>()),
    );
    // Bin de outra arquitetura (16 camadas contra 32): recusa, nao mistura lixo.
    expect(
      () => NativeEnhancer.openMixed(
        param: param,
        binA: limpa,
        binB: 'assets/ai/realesr-animevideov3/x4.bin',
        weightA: .5,
        gpu: false,
      ),
      throwsA(isA<StateError>()),
    );
  }, skip: pular);

  test('modelo invalido falha com motivo (nunca "IA ativada" sem modelo)', () {
    expect(() => NativeEnhancer.open('nao/existe'), throwsA(isA<StateError>()));
  }, skip: pular);
}
