import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/audio_ops.dart';

/// Um segundo de senoide em [hz], a 16 kHz.
Int16List _tom(double hz, double segundos, {double amp = 0.8}) {
  final n = (16000 * segundos).round();
  final out = Int16List(n);
  for (var i = 0; i < n; i++) {
    out[i] = (math.sin(2 * math.pi * hz * i / 16000) * amp * 32767).round();
  }
  return out;
}

void main() {
  group('faixa de frequencia', () {
    test('a faixa isola os ataques dela', () {
      // A cena que interessa: um bumbo a cada meio segundo por cima de um
      // chiado agudo continuo. No grave ha oito ataques; no agudo nao ha
      // ataque nenhum, porque o chiado nunca varia.
      const n = 16000 * 4;
      final mistura = Int16List(n);
      for (var i = 0; i < n; i++) {
        final t = i / 16000;
        // Bumbo com ENVELOPE, nao ligado e desligado no talo: som cortado
        // a seco tem estalo em toda frequencia, inclusive no agudo — e o
        // estalo, nao o bumbo, e que seria detectado.
        final local = t % 0.5;
        final ataque = (local / 0.012).clamp(0.0, 1.0);
        final queda = math.exp(-local * 22);
        final bumbo = math.sin(2 * math.pi * 60 * t) * 0.9 * ataque * queda;
        final chiado = math.sin(2 * math.pi * 6000 * t) * 0.25;
        mistura[i] = ((bumbo + chiado).clamp(-1.0, 1.0) * 32767).round();
      }

      final noGrave = detectBeats(
          bandEnvelope(mistura, 16000, 100, BeatBand.grave),
          sensitivity: 1.6);
      final noAgudo = detectBeats(
          bandEnvelope(mistura, 16000, 100, BeatBand.agudo),
          sensitivity: 1.6);

      expect(noGrave.length, greaterThanOrEqualTo(6));
      expect(noAgudo.length, lessThan(noGrave.length));
    });

    test('sinal vazio devolve envelope vazio', () {
      expect(bandEnvelope(Int16List(0), 16000, 100, BeatBand.tudo), isEmpty);
      expect(bandEnvelope(_tom(440, 1), 0, 100, BeatBand.tudo), isEmpty);
    });

    test('tudo nao filtra: o envelope acompanha a amplitude', () {
      final env = bandEnvelope(_tom(440, 0.5), 16000, 100, BeatBand.tudo);
      expect(env.length, 50);
      expect(env.every((v) => v > 0.9), isTrue);
    });
  });

  group('andamento', () {
    test('pulso regular de 120 bpm e lido como 120', () {
      final ataques = [
        for (var i = 0; i < 16; i++) Duration(milliseconds: 500 * i),
      ];
      expect(estimateBpm(ataques), closeTo(120, 0.5));
    });

    test('um ataque perdido nao desloca a leitura', () {
      final ataques = [
        for (var i = 0; i < 16; i++)
          if (i != 7) Duration(milliseconds: 500 * i),
      ];
      // A mediana ignora o intervalo dobrado; a media nao ignoraria.
      expect(estimateBpm(ataques), closeTo(120, 0.5));
    });

    test('dobra ate cair na faixa que se usa para editar', () {
      // 40 bpm e lento demais para grade; vale 80.
      final ataques = [
        for (var i = 0; i < 8; i++) Duration(milliseconds: 1500 * i),
      ];
      final bpm = estimateBpm(ataques)!;
      expect(bpm, greaterThanOrEqualTo(60));
      expect(bpm, lessThanOrEqualTo(180));
    });

    test('poucos ataques nao viram andamento', () {
      expect(estimateBpm(const []), isNull);
      expect(estimateBpm([Duration.zero, const Duration(seconds: 1)]), isNull);
    });
  });

  group('grade', () {
    test('1/4 poe uma marca em cada tempo', () {
      final g = beatGrid(
        first: Duration.zero,
        bpm: 120,
        denominador: 4,
        until: const Duration(seconds: 2),
      );
      // 120 bpm = 0,5 s por tempo: 0, 0.5, 1.0, 1.5, 2.0.
      expect(g.length, 5);
      expect(g[1], const Duration(milliseconds: 500));
    });

    test('1/8 dobra a densidade e 1/1 a divide por quatro', () {
      Duration ate(int s) => Duration(seconds: s);
      final oitavos = beatGrid(
          first: Duration.zero, bpm: 120, denominador: 8, until: ate(4));
      final inteiros = beatGrid(
          first: Duration.zero, bpm: 120, denominador: 1, until: ate(4));
      expect(oitavos[1], const Duration(milliseconds: 250));
      expect(inteiros[1], const Duration(seconds: 2));
    });

    test('comeca na primeira batida, nao no zero', () {
      final g = beatGrid(
        first: const Duration(milliseconds: 320),
        bpm: 120,
        denominador: 4,
        until: const Duration(seconds: 2),
      );
      expect(g.first, const Duration(milliseconds: 320));
      expect(g[1], const Duration(milliseconds: 820));
    });

    test('andamento invalido nao gera grade', () {
      expect(
          beatGrid(
              first: Duration.zero,
              bpm: 0,
              denominador: 4,
              until: const Duration(seconds: 2)),
          isEmpty);
    });

    test('faixa longa nao vira milhoes de marcas', () {
      final g = beatGrid(
        first: Duration.zero,
        bpm: 180,
        denominador: 8,
        until: const Duration(hours: 3),
      );
      expect(g.length, lessThanOrEqualTo(4000));
    });
  });
}
