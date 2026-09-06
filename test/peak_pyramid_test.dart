import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/peak_pyramid.dart';

/// Uma senoide de [seconds] segundos.
Int16List _seno(double seconds, {int rate = 16000, double amp = 1.0}) {
  final n = (seconds * rate).round();
  final out = Int16List(n);
  for (var i = 0; i < n; i++) {
    out[i] = (math.sin(i * 2 * math.pi * 440 / rate) * amp * 32767).round();
  }
  return out;
}

void main() {
  group('Montar a piramide', () {
    test('sai com os seis niveis quando o audio e longo', () {
      final p = buildPeakPyramid(_seno(60), 16000);
      expect(p.levels, hasLength(peakBucketSizes.length));
      for (var i = 0; i < p.levels.length; i++) {
        expect(p.levels[i].samplesPerBucket, peakBucketSizes[i]);
      }
    });

    // Audio curto nao tem detalhe grosso para dar: melhor menos niveis
    // do que niveis vazios.
    test('audio curto para nos niveis que cabem', () {
      final p = buildPeakPyramid(_seno(0.5), 16000);
      expect(p.levels.length, lessThan(peakBucketSizes.length));
      expect(p.levels, isNotEmpty);
    });

    test('audio vazio nao monta nada', () {
      expect(buildPeakPyramid(Int16List(0), 16000).isEmpty, isTrue);
      expect(buildPeakPyramid(_seno(1), 0).isEmpty, isTrue);
    });

    test('cada nivel cobre a mesma duracao', () {
      final p = buildPeakPyramid(_seno(30), 16000);
      final base = p.levels.first.duration.inMilliseconds;
      for (final l in p.levels) {
        // Os niveis grossos perdem o resto da divisao — a diferenca
        // nunca passa de um balde.
        expect(l.duration.inMilliseconds,
            closeTo(base, l.bucketSeconds * 1000 + 1));
      }
    });
  });

  group('O que cada balde guarda', () {
    final p = buildPeakPyramid(_seno(10), 16000);

    test('a senoide vai de -1 a 1', () {
      final l = p.levels.first;
      expect(l.max[10], closeTo(1, 0.02));
      expect(l.min[10], closeTo(-1, 0.02));
    });

    // O RMS de uma senoide e 1/raiz(2). E o numero que diz o quao ALTO
    // esta, que e o que o ouvido percebe — o pico sozinho nao diz.
    //
    // Medido num balde LARGO: o mais fino cobre 4 ms, menos de duas
    // voltas da onda, e um pedaco de volta nao tem o RMS da volta
    // inteira.
    test('o RMS da senoide e 0,707', () {
      expect(p.levels[2].rms[10], closeTo(0.707, 0.01));
    });

    test('o RMS nunca passa do pico', () {
      for (final l in p.levels) {
        for (var i = 0; i < l.length; i++) {
          expect(l.rms[i], lessThanOrEqualTo(l.max[i].abs() + 1e-6),
              reason: 'balde $i de ${l.samplesPerBucket}');
        }
      }
    });

    // O nivel grosso e feito do anterior: o maximo dele tem de ser o
    // maximo dos baldes que ele resume, nunca menos.
    test('o nivel grosso nao perde o pico do fino', () {
      final fino = p.levels[0];
      final grosso = p.levels[1];
      final fator = grosso.samplesPerBucket ~/ fino.samplesPerBucket;
      for (var i = 0; i < math.min(20, grosso.length); i++) {
        var maiorFino = -1.0;
        for (var j = 0; j < fator; j++) {
          maiorFino = math.max(maiorFino, fino.max[i * fator + j]);
        }
        expect(grosso.max[i], closeTo(maiorFino, 1e-6));
      }
    });

    test('silencio da zero em tudo', () {
      final p = buildPeakPyramid(Int16List(16000 * 2), 16000);
      final l = p.levels.first;
      expect(l.max[5], 0);
      expect(l.min[5], 0);
      expect(l.rms[5], 0);
    });
  });

  group('Escolher o nivel', () {
    final p = buildPeakPyramid(_seno(60), 16000);

    // A regra que faz o zoom NUNCA recalcular: escolhe o mais fino cujo
    // balde ainda cubra um pixel.
    test('zoom fechado escolhe nivel fino', () {
      // 1 px = 1 ms.
      final l = p.levelFor(0.001);
      expect(l.samplesPerBucket, peakBucketSizes.first);
    });

    test('zoom aberto escolhe nivel grosso', () {
      // 1 px = 1 s.
      final l = p.levelFor(1.0);
      expect(l.samplesPerBucket, greaterThan(peakBucketSizes[2]));
    });

    test('o balde escolhido cobre pelo menos um pixel', () {
      for (final spp in [0.001, 0.01, 0.05, 0.2, 1.0]) {
        final l = p.levelFor(spp);
        final ehOMaisGrosso = l.samplesPerBucket == peakBucketSizes.last;
        if (!ehOMaisGrosso) {
          expect(l.bucketSeconds, greaterThanOrEqualTo(spp),
              reason: '$spp s/px');
        }
      }
    });

    // Ampliar alem do nivel mais fino nao inventa detalhe.
    test('ampliar demais para no nivel mais fino', () {
      expect(p.levelFor(0.0000001).samplesPerBucket, peakBucketSizes.first);
    });

    test('piramide vazia devolve nivel vazio em vez de estourar', () {
      const vazia = PeakPyramid([], 16000);
      expect(vazia.levelFor(0.01).length, 0);
    });
  });

  group('Achar o balde', () {
    test('o instante cai no balde certo', () {
      final p = buildPeakPyramid(_seno(10), 16000);
      final l = p.levels.first;
      // 64 amostras a 16 kHz = 4 ms por balde.
      expect(l.bucketAt(const Duration(milliseconds: 40)), 10);
      expect(l.bucketAt(Duration.zero), 0);
    });
  });
}
