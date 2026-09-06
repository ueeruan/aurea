import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/audio_mix.dart';
import 'package:aurea/src/features/editor/domain/audio_ops.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/application/duck_service.dart';
import 'package:aurea/src/features/editor/domain/loudness.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/export/application/export_engine.dart';
import 'package:aurea/src/features/export/domain/export_settings.dart';

Float32List _seno(double hz, double amplitude, double segundos, int rate) {
  final n = (segundos * rate).round();
  final out = Float32List(n);
  for (var i = 0; i < n; i++) {
    out[i] = amplitude * math.sin(2 * math.pi * hz * i / rate);
  }
  return out;
}

void main() {
  const rate = 16000;

  group('Sonoridade em LUFS', () {
    test('dobrar a amplitude sobe 6 dB', () {
      final a = integratedLufs(_seno(1000, 0.1, 3, rate), rate)!;
      final b = integratedLufs(_seno(1000, 0.2, 3, rate), rate)!;
      expect(b - a, closeTo(6.02, 0.05));
    });

    test('um seno de 1 kHz a -20 dBFS cai perto de -20 LUFS', () {
      // -20 dBFS RMS = amplitude 0,1 x raiz de 2.
      final lufs = integratedLufs(_seno(1000, 0.1 * math.sqrt2, 3, rate), rate);
      expect(lufs, isNotNull);
      expect(lufs!, greaterThan(-21.5));
      expect(lufs, lessThan(-18.5));
    });

    test('faixa muda nao tem sonoridade', () {
      expect(integratedLufs(Float32List(rate * 2), rate), isNull);
    });

    test('tres clipes de niveis diferentes medem igual depois de normalizar',
        () {
      final medidos = <double>[];
      for (final amplitude in [0.05, 0.2, 0.6]) {
        final s = _seno(440, amplitude, 4, rate);
        final ganho = normalizeGainLufs(s, rate);
        final ajustado = Float32List(s.length);
        for (var i = 0; i < s.length; i++) {
          ajustado[i] = s[i] * ganho;
        }
        medidos.add(integratedLufs(ajustado, rate)!);
      }
      for (final m in medidos) {
        expect(m, closeTo(lufsAlvoPadrao, 0.5));
      }
    });

    test('o portao relativo ignora o silencio entre as frases', () {
      // Fala de 1 s, 4 s de silencio, fala de 1 s: a sonoridade tem de
      // ficar perto da da fala sozinha, nao da media com o silencio.
      final fala = _seno(300, 0.3, 1, rate);
      final total = Float32List(rate * 6);
      total.setRange(0, fala.length, fala);
      total.setRange(rate * 5, rate * 5 + fala.length, fala);

      final soFala = integratedLufs(fala, rate)!;
      final comSilencio = integratedLufs(total, rate)!;
      expect(comSilencio, closeTo(soFala, 1.5));
    });
  });

  group('Envelope de ducking pre-calculado', () {
    Float32List vozComDuasFrases() {
      final v = Float32List(audioPeaksPerSecond * 10);
      for (var i = audioPeaksPerSecond * 1; i < audioPeaksPerSecond * 3; i++) {
        v[i] = 0.5;
      }
      for (var i = audioPeaksPerSecond * 6; i < audioPeaksPerSecond * 8; i++) {
        v[i] = 0.5;
      }
      return v;
    }

    test('abaixa exatamente o valor configurado durante a fala', () {
      final env = buildDuckEnvelope(vozComDuasFrases(), amount: 0.6);
      // No meio da primeira frase o ataque ja terminou.
      final g = env.gainAt(const Duration(milliseconds: 2500));
      expect(g, closeTo(0.4, 0.01));
    });

    test('volta ao normal entre as frases', () {
      final env = buildDuckEnvelope(vozComDuasFrases(), amount: 0.6);
      expect(env.gainAt(const Duration(milliseconds: 5500)), closeTo(1.0, 0.02));
    });

    test('a simplificacao nao muda a curva mais que a tolerancia', () {
      final voz = vozComDuasFrases();
      final cru = duckEnvelope(voz, amount: 0.6);
      final env = buildDuckEnvelope(voz, amount: 0.6, tolerance: 0.01);
      for (var i = 0; i < cru.length; i++) {
        final t = Duration(microseconds: (i * 10000));
        expect(env.gainAt(t), closeTo(cru[i], 0.0101), reason: 'ponto $i');
      }
    });

    test('guarda muito menos pontos que os cem por segundo', () {
      final env = buildDuckEnvelope(vozComDuasFrases(), amount: 0.6);
      expect(env.points.length, lessThan(60));
      expect(env.points.length, greaterThan(4));
    });

    test('reducao de 0 dB e identica ao original', () {
      final env = buildDuckEnvelope(vozComDuasFrases(), amount: 0);
      expect(env.isNeutral, isTrue);
      expect(env.gainAt(const Duration(seconds: 2)), 1.0);
    });
  });

  group('Ganho da trilha: uma conta so', () {
    AudioLayer trilha({AudioSpec audio = const AudioSpec(), double volume = 1}) =>
        AudioLayer(
          name: 'm',
          startTime: const Duration(seconds: 2),
          duration: const Duration(seconds: 10),
          sourcePath: '/tmp/m.wav',
          volume: volume,
          audio: audio,
        );

    test('sem nada configurado o ganho e o volume da camada', () {
      expect(layerAudioGainAt(trilha(volume: 0.8), const Duration(seconds: 5)),
          closeTo(0.8, 1e-9));
    });

    test('mudo zera, mesmo com ganho alto', () {
      final l = trilha(audio: const AudioSpec(gain: 4, muted: true));
      expect(layerAudioGainAt(l, const Duration(seconds: 5)), 0);
    });

    test('o fade e de tempo de clipe, nao de linha do tempo', () {
      final l = trilha(
        audio: const AudioSpec(fadeIn: Duration(seconds: 2)),
      );
      // A camada comeca em 2 s; o meio do fade e em 3 s.
      expect(layerAudioGainAt(l, const Duration(seconds: 3)),
          closeTo(math.sin(math.pi / 4), 1e-6));
      expect(layerAudioGainAt(l, const Duration(seconds: 4)), closeTo(1, 1e-6));
    });

    test('o ducking e de tempo de linha do tempo', () {
      final l = trilha(audio: const AudioSpec(duckAgainstId: 'voz'));
      const env = DuckEnvelope([
        (t: Duration(seconds: 0), g: 1.0),
        (t: Duration(seconds: 4), g: 1.0),
        (t: Duration(seconds: 5), g: 0.3),
        (t: Duration(seconds: 9), g: 0.3),
      ]);
      expect(layerAudioGainAt(l, const Duration(seconds: 6), duck: env),
          closeTo(0.3, 1e-6));
      expect(layerAudioGainAt(l, const Duration(seconds: 3), duck: env),
          closeTo(1.0, 1e-6));
    });

    test('neutro: ganho 0 dB, sem fade e sem ducking nao mexe em nada', () {
      final l = trilha();
      for (var s = 2; s < 12; s++) {
        expect(layerAudioGainAt(l, Duration(seconds: s)), 1.0);
      }
    });
  });

  group('Limitador do barramento', () {
    test('soma que cabe no teto nao e tocada', () {
      expect(busGainFor(0.7), 1.0);
      expect(busGainFor(kTetoDoBarramento), 1.0);
    });

    test('cinco trilhas altas somadas nao passam do teto', () {
      final trilhas = [
        for (var i = 0; i < 5; i++)
          (Float32List.fromList(List.filled(100, 0.8)), 1.0),
      ];
      final soma = sumPeaks(trilhas);
      final g = busGainFor(soma.reduce(math.max));
      for (final v in soma) {
        expect(v * g, lessThanOrEqualTo(kTetoDoBarramento + 1e-6));
      }
    });

    test('marca onde a soma estoura', () {
      final soma = Float32List(300);
      for (var i = 100; i < 120; i++) {
        soma[i] = 1.4;
      }
      final marcas = clippingAt(soma);
      expect(marcas, hasLength(1));
      expect(marcas.first.inMilliseconds, 1000);
    });
  });

  group('O arquivo exportado e o preview', () {
    ExportEngine motor(List<Layer> layers,
        [Map<String, DuckEnvelope> envelopes = const {}]) {
      return ExportEngine(
        VideoProject(name: 'p', createdAt: DateTime(2026, 1, 1),
            layers: layers),
        const ExportSettings(),
        envelopes,
      );
    }

    AudioLayer musica({AudioSpec audio = const AudioSpec()}) => AudioLayer(
          id: 'musica',
          name: 'M',
          startTime: Duration.zero,
          duration: const Duration(seconds: 10),
          sourcePath: '/tmp/m.wav',
          audio: audio,
        );
    AudioLayer voz() => AudioLayer(
          id: 'voz',
          name: 'V',
          startTime: Duration.zero,
          duration: const Duration(seconds: 10),
          sourcePath: '/tmp/v.wav',
        );

    test('o ducking sai como envelope, nao como sidechaincompress', () {
      const env = DuckEnvelope([
        (t: Duration.zero, g: 1.0),
        (t: Duration(seconds: 2), g: 0.4),
        (t: Duration(seconds: 5), g: 1.0),
      ]);
      final g = motor(
        [musica(audio: const AudioSpec(duckAgainstId: 'voz')), voz()],
        {'musica': env},
      ).audioGraph(1);
      expect(g.filter, isNot(contains('sidechaincompress')));
      expect(g.filter, contains("volume=volume='"));
      expect(g.filter, contains('eval=frame'));
    });

    test('a expressao do filtro desenha a mesma curva do envelope', () {
      const env = DuckEnvelope([
        (t: Duration.zero, g: 1.0),
        (t: Duration(seconds: 2), g: 0.4),
      ]);
      expect(
        ffmpegVolumeExpr(env),
        'lt(t,0.0000)*1.0000+'
        'gte(t,0.0000)*lt(t,2.0000)*(1.0000+-0.6000*(t-0.0000)/2.0000)+'
        'gte(t,2.0000)*0.4000',
      );
    });

    test('envelope neutro nao vira filtro nenhum', () {
      expect(ffmpegVolumeExpr(DuckEnvelope.neutro), isNull);
      final g = motor(
        [musica(audio: const AudioSpec(duckAgainstId: 'voz')), voz()],
        {'musica': DuckEnvelope.neutro},
      ).audioGraph(1);
      expect(g.filter, isNot(contains('volume=volume=')));
    });

    test('a soma nao e dividida pelo numero de trilhas', () {
      final g = motor([musica(), voz()]).audioGraph(1);
      expect(g.filter, contains('amix=inputs=2:normalize=0'));
    });

    test('mais de uma trilha entra no limitador', () {
      final g = motor([musica(), voz()]).audioGraph(1);
      expect(g.filter, contains('alimiter=limit=$kTetoDoBarramento'));
    });

    test('trilha unica em 0 dB sai sem limitador, amostra por amostra', () {
      final g = motor([musica()]).audioGraph(1);
      expect(g.filter, isNot(contains('alimiter')));
      expect(g.filter, contains('anull'));
    });

    test('trilha unica com ganho acima de 0 dB ganha limitador', () {
      final g = motor([musica(audio: const AudioSpec(gain: 2))]).audioGraph(1);
      expect(g.filter, contains('alimiter'));
    });
  });

  group('Envelope do projeto', () {
    test('a musica abaixa no instante da voz na LINHA, nao no do arquivo',
        () {
      // A voz comeca em 4 s da linha e usa o arquivo a partir de 2 s.
      final voz = AudioLayer(
        id: 'voz',
        name: 'V',
        startTime: const Duration(seconds: 4),
        duration: const Duration(seconds: 4),
        sourcePath: '/tmp/v.wav',
        sourceOffset: const Duration(seconds: 2),
      );
      final musica = AudioLayer(
        id: 'musica',
        name: 'M',
        startTime: Duration.zero,
        duration: const Duration(seconds: 12),
        sourcePath: '/tmp/m.wav',
        audio: const AudioSpec(duckAgainstId: 'voz', duckAmount: 0.6),
      );
      // No arquivo da voz ha som entre 2 s e 6 s.
      final picos = Float32List(audioPeaksPerSecond * 10);
      for (var i = audioPeaksPerSecond * 2;
          i < audioPeaksPerSecond * 6;
          i++) {
        picos[i] = 0.5;
      }

      final envs = buildProjectDuckEnvelopes(
        [musica, voz],
        (path) => path == '/tmp/v.wav' ? picos : null,
      );
      final env = envs['musica']!;
      expect(env.gainAt(const Duration(seconds: 2)), closeTo(1.0, 0.02));
      expect(env.gainAt(const Duration(milliseconds: 5500)),
          closeTo(0.4, 0.02));
    });

    test('sem picos da voz nao inventa envelope', () {
      final musica = AudioLayer(
        id: 'musica',
        name: 'M',
        startTime: Duration.zero,
        duration: const Duration(seconds: 5),
        sourcePath: '/tmp/m.wav',
        audio: const AudioSpec(duckAgainstId: 'voz'),
      );
      expect(buildProjectDuckEnvelopes([musica], (_) => null), isEmpty);
    });
  });
}
