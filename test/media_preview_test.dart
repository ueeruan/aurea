import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/application/media_preview_service.dart';

Int16List _tone({
  required int rate,
  required double seconds,
  required double amplitude,
  double hz = 220,
}) {
  final n = (rate * seconds).round();
  final out = Int16List(n);
  for (var i = 0; i < n; i++) {
    out[i] = (math.sin(2 * math.pi * hz * i / rate) * amplitude * 32767)
        .round();
  }
  return out;
}

void main() {
  group('Forma de onda', () {
    test('gera a quantidade certa de picos por segundo', () {
      final s = _tone(rate: 8000, seconds: 3, amplitude: 0.5);
      final peaks = computePeaks(s, 8000, 100);
      expect(peaks.length, 300);
    });

    test('o pico bate com a amplitude do sinal', () {
      final s = _tone(rate: 8000, seconds: 1, amplitude: 0.8);
      final peaks = computePeaks(s, 8000, 100);
      for (final p in peaks) {
        expect(p, closeTo(0.8, 0.02));
      }
    });

    // MAXIMO, nao media: media achata transiente, e transiente e o que
    // a pessoa procura quando corta no ritmo.
    test('um estalo curto no silencio aparece', () {
      final s = Int16List(8000);
      // Silencio, com UM pico de uma amostra no meio do primeiro balde.
      s[40] = 30000;
      final peaks = computePeaks(s, 8000, 100);
      expect(peaks.first, greaterThan(0.9),
          reason: 'o estalo sumiu — isso e media, nao pico');
      expect(peaks[1], 0);
    });

    test('silencio da zero', () {
      final peaks = computePeaks(Int16List(8000), 8000, 100);
      expect(peaks.length, 100);
      expect(peaks.every((p) => p == 0), isTrue);
    });

    test('picos ficam sempre em 0..1, inclusive no extremo negativo', () {
      final s = Int16List(1600);
      for (var i = 0; i < s.length; i++) {
        s[i] = i.isEven ? 32767 : -32768;
      }
      final peaks = computePeaks(s, 8000, 100);
      expect(peaks.isNotEmpty, isTrue);
      for (final p in peaks) {
        expect(p, inInclusiveRange(0, 1));
      }
    });

    test('entrada vazia ou invalida nao explode', () {
      expect(computePeaks(Int16List(0), 8000, 100), isEmpty);
      expect(computePeaks(_tone(rate: 8000, seconds: 1, amplitude: 1),
              0, 100),
          isEmpty);
      expect(computePeaks(_tone(rate: 8000, seconds: 1, amplitude: 1),
              8000, 0),
          isEmpty);
    });

    test('sinal mais curto que um balde nao gera balde pela metade', () {
      // 40 amostras a 8 kHz com 100 picos/s = menos de um balde (80).
      final peaks = computePeaks(Int16List(40), 8000, 100);
      expect(peaks, isEmpty);
    });

    test('taxa mais alta nao muda a quantidade de picos por segundo', () {
      final a = computePeaks(
          _tone(rate: 8000, seconds: 2, amplitude: 0.5), 8000, 100);
      final b = computePeaks(
          _tone(rate: 44100, seconds: 2, amplitude: 0.5), 44100, 100);
      expect(a.length, 200);
      expect(b.length, 200);
    });
  });
}
