import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/dsp.dart';
import 'package:aurea/src/features/editor/domain/voice_ops.dart';

const rate = 16000;

Float32List _seno(double hz, double amp, double seg, {double fase = 0}) {
  final n = (seg * rate).round();
  final out = Float32List(n);
  for (var i = 0; i < n; i++) {
    out[i] = amp * math.sin(2 * math.pi * hz * i / rate + fase);
  }
  return out;
}

double _rms(Float32List x, [int inicio = 0, int? fim]) {
  final f = fim ?? x.length;
  var soma = 0.0;
  for (var i = inicio; i < f; i++) {
    soma += x[i] * x[i];
  }
  return math.sqrt(soma / (f - inicio));
}

/// Quanta energia existe acima de [corte] Hz.
double _energiaAcima(Float32List x, double corte) =>
    _rms(highPass(rate, corte).apply(x));

void main() {
  group('Transformada e janela', () {
    test('ida e volta devolve o sinal', () {
      final re = Float64List(256);
      final im = Float64List(256);
      final rnd = math.Random(7);
      final original = List<double>.generate(256, (_) => rnd.nextDouble() - 0.5);
      for (var i = 0; i < 256; i++) {
        re[i] = original[i];
      }
      fft(re, im);
      fft(re, im, inverse: true);
      for (var i = 0; i < 256; i++) {
        expect(re[i], closeTo(original[i], 1e-9));
        expect(im[i], closeTo(0, 1e-9));
      }
    });

    test('a janela de Hann com meio salto soma exatamente um', () {
      final w = hannWindow(64);
      for (var i = 0; i < 32; i++) {
        expect(w[i] + w[i + 32], closeTo(1.0, 1e-12));
      }
    });

    test('o passa-alta corta o grave e deixa o agudo', () {
      final grave = _seno(60, 0.5, 0.5);
      final agudo = _seno(4000, 0.5, 0.5);
      final f = highPass(rate, 500);
      expect(_rms(f.apply(grave)), lessThan(_rms(grave) * 0.2));
      expect(_rms(f.apply(agudo)), greaterThan(_rms(agudo) * 0.9));
    });
  });

  group('Neutralidade: intensidade 0 devolve o original', () {
    final x = _seno(440, 0.4, 0.5);

    test('compressor', () {
      expect(softCompress(x, rate: rate, intensity: 0), same(x));
    });

    test('melhorar voz', () {
      expect(enhanceVoice(x, rate: rate, intensity: 0), same(x));
    });

    test('de-esser', () {
      expect(deEsser(x, rate: rate, intensity: 0), same(x));
    });

    test('equalizador em zero dB nas tres bandas', () {
      expect(threeBandEq(x, rate: rate), same(x));
    });

    test('limpeza', () {
      final perfil = noiseProfileFrom(_seno(3000, 0.01, 0.5), rate);
      expect(denoise(x, profile: perfil, intensity: 0), same(x));
    });
  });

  group('Compressor', () {
    test('aproxima o alto do baixo em vez de so subir tudo', () {
      // Uma metade perto do microfone, outra longe.
      final x = Float32List(rate);
      for (var i = 0; i < rate ~/ 2; i++) {
        x[i] = 0.7 * math.sin(2 * math.pi * 300 * i / rate);
      }
      for (var i = rate ~/ 2; i < rate; i++) {
        x[i] = 0.08 * math.sin(2 * math.pi * 300 * i / rate);
      }
      final antes = _rms(x, 0, rate ~/ 2) / _rms(x, rate ~/ 2);
      final y = softCompress(x, rate: rate, ratio: 4, thresholdDb: -20);
      final depois = _rms(y, 0, rate ~/ 2) / _rms(y, rate ~/ 2);
      expect(depois, lessThan(antes * 0.6));
    });

    test('sinal abaixo do limiar passa intacto', () {
      final x = _seno(300, 0.02, 0.3);
      final y = softCompress(x, rate: rate, thresholdDb: -20, ratio: 4);
      for (var i = 100; i < x.length; i++) {
        expect(y[i], closeTo(x[i], 1e-4));
      }
    });
  });

  group('De-esser', () {
    test('abaixa a sibilancia e deixa a voz', () {
      final voz = _seno(220, 0.35, 1.0);
      final sss = _seno(7000, 0.35, 1.0);
      final x = Float32List(voz.length);
      for (var i = 0; i < x.length; i++) {
        x[i] = voz[i] + sss[i];
      }
      final y = deEsser(x, rate: rate, intensity: 1);
      // O agudo cai bastante...
      expect(_energiaAcima(y, 4000), lessThan(_energiaAcima(x, 4000) * 0.6));
      // ...e o corpo da voz fica onde estava.
      final grave = lowPass(rate, 800);
      expect(_rms(grave.apply(y)), closeTo(_rms(grave.apply(x)), 0.02));
    });
  });

  group('Melhorar voz', () {
    test('tira o ronco de baixa frequencia', () {
      final ronco = _seno(40, 0.3, 1.0);
      final voz = _seno(400, 0.3, 1.0);
      final x = Float32List(voz.length);
      for (var i = 0; i < x.length; i++) {
        x[i] = ronco[i] + voz[i];
      }
      final y = enhanceVoice(x, rate: rate);
      final grave = lowPass(rate, 60);
      expect(_rms(grave.apply(y)), lessThan(_rms(grave.apply(x)) * 0.5));
    });

    test('devolve brilho', () {
      final x = _seno(8000, 0.2, 0.5);
      final y = enhanceVoice(x, rate: rate);
      expect(_rms(y), greaterThan(_rms(x)));
    });
  });

  group('Tirar ruido de fundo', () {
    /// Ruido branco sempre igual: teste de audio nao pode depender de sorte.
    Float32List chiado(double amp, double seg, int semente) {
      final rnd = math.Random(semente);
      final n = (seg * rate).round();
      final out = Float32List(n);
      for (var i = 0; i < n; i++) {
        out[i] = amp * (rnd.nextDouble() * 2 - 1);
      }
      return out;
    }

    test('o chiado cai e o tom continua la', () {
      final perfil = noiseProfileFrom(chiado(0.05, 1.0, 1), rate);
      expect(perfil.isEmpty, isFalse);

      // O tom fica bem longe da faixa medida: um passa-alta de segunda
      // ordem em 3 kHz ainda deixa passar um pedaco de 1 kHz, e o teste
      // acabaria medindo o proprio tom em vez do chiado.
      final tom = _seno(200, 0.3, 2.0);
      final ruido = chiado(0.05, 2.0, 2);
      final sujo = Float32List(tom.length);
      for (var i = 0; i < sujo.length; i++) {
        sujo[i] = tom[i] + ruido[i];
      }
      final limpo = denoise(sujo, profile: perfil, intensity: 1);

      // Fora da faixa do tom sobra so o chiado: ele tem de cair.
      final foraAntes = _energiaAcima(sujo, 3000);
      final foraDepois = _energiaAcima(limpo, 3000);
      expect(foraDepois, lessThan(foraAntes * 0.6));

      // O tom em si sobrevive.
      final faixa = lowPass(rate, 400);
      final so = faixa.apply(limpo);
      final soAntes = faixa.apply(sujo);
      expect(_rms(so, rate, rate * 2),
          greaterThan(_rms(soAntes, rate, rate * 2) * 0.85));
    });

    test('trecho curto demais para analisar volta como veio', () {
      final curto = _seno(500, 0.2, 0.01);
      final perfil = noiseProfileFrom(chiado(0.05, 1.0, 3), rate);
      expect(denoise(curto, profile: perfil), same(curto));
    });

    test('sem perfil nao mexe em nada', () {
      final x = _seno(500, 0.2, 1.0);
      expect(denoise(x, profile: NoiseProfile(Float64List(0), rate)), same(x));
    });
  });
}
