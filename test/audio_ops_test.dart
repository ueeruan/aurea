import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/audio_ops.dart';

/// Constroi um envelope de picos a partir de trechos (segundos, nivel).
Float32List _env(List<(double, double)> trechos) {
  final total = trechos.fold<double>(0, (a, t) => a + t.$1);
  final n = (total * audioPeaksPerSecond).round();
  final out = Float32List(n);
  var i = 0;
  for (final t in trechos) {
    final passos = (t.$1 * audioPeaksPerSecond).round();
    for (var j = 0; j < passos && i < n; j++, i++) {
      out[i] = t.$2;
    }
  }
  return out;
}

double _s(Duration d) => d.inMilliseconds / 1000;

void main() {
  group('Silencio', () {
    test('acha a pausa entre duas falas', () {
      // 1 s de fala, 1 s de silencio, 1 s de fala.
      final e = _env([(1, 0.6), (1, 0.0), (1, 0.6)]);
      final s = detectSilence(e, padding: Duration.zero);
      expect(s.length, 1);
      expect(_s(s.first.$1), closeTo(1.0, 0.02));
      expect(_s(s.first.$2), closeTo(2.0, 0.02));
    });

    test('pausa curta demais nao conta como silencio', () {
      // 200 ms de pausa, abaixo do minimo de 350 ms.
      final e = _env([(1, 0.6), (0.2, 0.0), (1, 0.6)]);
      expect(detectSilence(e), isEmpty);
    });

    // Cortar exatamente no limiar decepa a consoante: a folga existe
    // para a fala nao sair picotada.
    test('a folga encolhe o trecho nas duas pontas', () {
      final e = _env([(1, 0.6), (1, 0.0), (1, 0.6)]);
      final semFolga = detectSilence(e, padding: Duration.zero);
      final comFolga = detectSilence(e,
          padding: const Duration(milliseconds: 120));
      expect(comFolga.first.$1, greaterThan(semFolga.first.$1));
      expect(comFolga.first.$2, lessThan(semFolga.first.$2));
    });

    test('faixa toda em silencio devolve um trecho so', () {
      final s = detectSilence(_env([(3, 0.0)]), padding: Duration.zero);
      expect(s.length, 1);
      expect(_s(s.first.$2), closeTo(3.0, 0.02));
    });

    test('faixa sem silencio nenhum devolve vazio', () {
      expect(detectSilence(_env([(3, 0.5)])), isEmpty);
    });

    test('envelope vazio nao explode', () {
      expect(detectSilence(Float32List(0)), isEmpty);
      expect(detectSpeech(Float32List(0)), isEmpty);
    });
  });

  group('Fala', () {
    test('e o complemento do silencio', () {
      final e = _env([(1, 0.6), (1, 0.0), (1, 0.6)]);
      final fala = detectSpeech(e, padding: Duration.zero);
      expect(fala.length, 2);
      expect(_s(fala[0].$1), closeTo(0.0, 0.02));
      expect(_s(fala[0].$2), closeTo(1.0, 0.02));
      expect(_s(fala[1].$1), closeTo(2.0, 0.02));
      expect(_s(fala[1].$2), closeTo(3.0, 0.02));
    });

    test('faixa cheia de som vira um trecho unico', () {
      final fala = detectSpeech(_env([(2, 0.5)]));
      expect(fala.length, 1);
      expect(_s(fala.first.$1), 0);
      expect(_s(fala.first.$2), closeTo(2.0, 0.02));
    });
  });

  group('Batidas', () {
    test('marca o ataque, nao o sustain', () {
      // Fundo baixo com quatro estouros de 50 ms.
      final e = _env([
        (0.6, 0.05), (0.05, 0.9),
        (0.55, 0.05), (0.05, 0.9),
        (0.55, 0.05), (0.05, 0.9),
        (0.55, 0.05), (0.05, 0.9),
      ]);
      final b = detectBeats(e);
      expect(b.length, 4);
      // Espacadas em ~0,6 s.
      for (var i = 1; i < b.length; i++) {
        expect(_s(b[i] - b[i - 1]), closeTo(0.6, 0.08));
      }
    });

    test('som parado nao gera batida', () {
      expect(detectBeats(_env([(3, 0.5)])), isEmpty);
    });

    test('silencio nao gera batida', () {
      expect(detectBeats(_env([(3, 0.0)])), isEmpty);
    });

    test('o intervalo minimo evita marcar duas vezes', () {
      final e = _env([(0.6, 0.05), (0.4, 0.9), (0.6, 0.05)]);
      final b = detectBeats(e, minGap: const Duration(milliseconds: 300));
      expect(b.length, 1);
    });
  });

  group('Normalizar', () {
    test('leva o pico ao alvo', () {
      final e = _env([(2, 0.25)]);
      final g = normalizeGain(e, target: 0.9);
      expect(g, closeTo(3.6, 0.1));
      expect(0.25 * g, closeTo(0.9, 0.02));
    });

    // Um estalo isolado nao pode decidir o volume da faixa inteira.
    test('estalo isolado nao derruba o ganho', () {
      final normal = _env([(2, 0.25)]);
      final comEstalo = Float32List.fromList(normal);
      comEstalo[10] = 1.0;
      final a = normalizeGain(normal);
      final b = normalizeGain(comEstalo);
      expect(b, closeTo(a, a * 0.15));
    });

    test('silencio nao vira ganho infinito', () {
      expect(normalizeGain(_env([(1, 0.0)])), 1);
      expect(normalizeGain(Float32List(0)), 1);
    });

    test('ganho fica dentro de limites sensatos', () {
      expect(normalizeGain(_env([(1, 0.0001)])), lessThanOrEqualTo(12.0));
      expect(normalizeGain(_env([(1, 1.0)])), greaterThanOrEqualTo(0.1));
    });
  });

  group('Decibeis', () {
    test('ganho 1 e zero dB, e a volta bate', () {
      expect(gainToDb(1), closeTo(0, 1e-9));
      expect(dbToGain(0), closeTo(1, 1e-9));
      expect(gainToDb(dbToGain(-6)), closeTo(-6, 1e-6));
    });

    test('metade da amplitude e cerca de -6 dB', () {
      expect(gainToDb(0.5), closeTo(-6.02, 0.02));
    });

    test('mudo vira menos infinito', () {
      expect(gainToDb(0), double.negativeInfinity);
      expect(dbToGain(double.negativeInfinity), 0);
    });
  });

  group('Fade', () {
    const total = Duration(seconds: 4);

    test('sem fade, o ganho e cheio em qualquer ponto', () {
      for (final ms in [0, 1000, 3999]) {
        expect(fadeGainAt(Duration(milliseconds: ms), total), 1);
      }
    });

    test('entrada sobe de 0 a 1', () {
      const f = Duration(seconds: 1);
      expect(fadeGainAt(Duration.zero, total, fadeIn: f), closeTo(0, 1e-9));
      expect(fadeGainAt(const Duration(seconds: 1), total, fadeIn: f),
          closeTo(1, 1e-9));
    });

    test('saida desce de 1 a 0', () {
      const f = Duration(seconds: 1);
      expect(fadeGainAt(const Duration(seconds: 3), total, fadeOut: f),
          closeTo(1, 1e-9));
      expect(fadeGainAt(total, total, fadeOut: f), closeTo(0, 1e-9));
    });

    // Fade linear de volume soa como buraco no meio: o ouvido responde
    // a potencia, nao a amplitude.
    test('a curva e de igual potencia, nao reta', () {
      const f = Duration(seconds: 2);
      final meio =
          fadeGainAt(const Duration(seconds: 1), total, fadeIn: f);
      expect(meio, closeTo(0.7071, 0.001));
      expect(meio, greaterThan(0.5));
    });

    test('fora do clipe o ganho e zero', () {
      expect(fadeGainAt(const Duration(seconds: 5), total), 0);
      expect(fadeGainAt(const Duration(seconds: -1), total), 0);
    });
  });

  group('Ducking', () {
    test('abaixa na voz e volta no silencio', () {
      // 1 s calado, 1 s de voz, 1 s calado.
      final voz = _env([(1, 0.0), (1, 0.6), (1, 0.0)]);
      final env = duckEnvelope(voz, amount: 0.7);

      expect(env.first, closeTo(1, 0.01));
      // No meio da voz ja desceu ate o piso.
      expect(env[150], closeTo(0.3, 0.02));
      // No fim ja voltou.
      expect(env.last, closeTo(1, 0.05));
    });

    test('a descida e suave, nao um degrau', () {
      final voz = _env([(1, 0.0), (1, 0.6)]);
      final env = duckEnvelope(voz,
          amount: 0.7, attack: const Duration(milliseconds: 200));
      // Logo depois da voz comecar ainda esta descendo.
      expect(env[101], lessThan(1));
      expect(env[101], greaterThan(0.3));
    });

    test('sem voz, o ganho fica cheio o tempo todo', () {
      final env = duckEnvelope(_env([(2, 0.0)]));
      expect(env.every((g) => g > 0.99), isTrue);
    });

    test('quantidade zero nao mexe em nada', () {
      final env = duckEnvelope(_env([(1, 0.8)]), amount: 0);
      expect(env.every((g) => g > 0.99), isTrue);
    });

    test('envelope vazio devolve vazio', () {
      expect(duckEnvelope(Float32List(0)), isEmpty);
    });
  });

  group('Juntar trechos', () {
    test('cola o que ficou perto demais', () {
      final r = mergeClose([
        (Duration.zero, const Duration(seconds: 1)),
        (const Duration(milliseconds: 1080), const Duration(seconds: 2)),
      ]);
      expect(r.length, 1);
      expect(r.first.$2, const Duration(seconds: 2));
    });

    test('deixa em paz o que esta longe', () {
      final r = mergeClose([
        (Duration.zero, const Duration(seconds: 1)),
        (const Duration(seconds: 3), const Duration(seconds: 4)),
      ]);
      expect(r.length, 2);
    });

    test('lista de um ou vazia passa direto', () {
      expect(mergeClose(const []), isEmpty);
      final um = [(Duration.zero, const Duration(seconds: 1))];
      expect(mergeClose(um).length, 1);
    });
  });
}
