// "SEMPRE QUE IMPORTAR VIDEO, DEIXAR O ESPECTRO DE AUDIO BEM VISIVEL PRA
// DECUPAR" (dono, 14/09/2026).
//
// O que estava errado e o que estes testes prendem:
//   * a piramide era montada do envelope de 100/s como se fosse a taxa de
//     amostragem: balde mais fino de 0,64 s, onda em degraus e ate 640 ms
//     fora do lugar — agora a base e de 4 ms;
//   * a onda ignorava reverso e Time Remap — agora segue o instante real;
//   * video mudo rodava o FFmpeg a cada sessao — agora fica no cache;
//   * a faixa tinha 12 px — agora a linha com midia e alta.
import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/onda_no_clipe.dart';
import 'package:aurea/src/features/editor/domain/peak_pyramid.dart';
import 'package:aurea/src/features/editor/domain/streaming_waveform.dart';
import 'package:aurea/src/features/editor/domain/waveform_cache.dart';
import 'package:aurea/src/features/editor/presentation/am/clip_preview_painters.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_test/flutter_test.dart';

/// 1 s de silencio e 1 s de seno a 440 Hz com amplitude 0,5, 16 kHz mono.
File _pcmSilencioDepoisTom() {
  final n = 32000;
  final bytes = ByteData(n * 2);
  for (var i = 0; i < n; i++) {
    final v = i < 16000 ? 0.0 : 0.5 * math.sin(2 * math.pi * 440 * i / 16000);
    bytes.setInt16(i * 2, (v * 32767).round(), Endian.little);
  }
  final dir = Directory.systemTemp.createTempSync('aurea_onda');
  addTearDown(() => dir.deleteSync(recursive: true));
  return File('${dir.path}/tom.pcm')..writeAsBytesSync(bytes.buffer.asUint8List());
}

VideoLayer _video({
  Duration offset = Duration.zero,
  bool reverse = false,
  AnimatedDouble? remap,
}) => VideoLayer(
  id: 'v',
  name: 'v',
  startTime: Duration.zero,
  duration: const Duration(seconds: 2),
  sourcePath: '/x.mp4',
  sourceOffset: offset,
  reverse: reverse,
  effects: [
    if (remap != null)
      EffectInstance(type: EffectType.timeRemap, params: {'tempo': remap}),
  ],
);

void main() {
  test('o scanner monta a base de 4 ms (e nao 0,64 s)', () async {
    final r = await scanMonoPcm(_pcmSilencioDepoisTom().path);
    expect(r.peaks.length, 200, reason: 'envelope de 100/s continua igual para os detectores');
    expect(r.baseMax.length, 32000 ~/ waveBaseBucket);
    final p = pyramidFromBase(r.baseMin, r.baseMax, r.baseRms, 16000);
    expect(p.levels.first.bucketSeconds, closeTo(0.004, 1e-9));
    // Silencio no primeiro segundo, tom com pico ~0,5 e RMS ~0,35 depois.
    final meio = p.levels.first.length ~/ 2;
    expect(p.levels.first.max[meio ~/ 2], closeTo(0, 1e-3));
    expect(p.levels.first.max[meio + 20], closeTo(0.5, 0.02));
    expect(p.levels.first.min[meio + 20], closeTo(-0.5, 0.02));
    expect(p.levels.first.rms[meio + 20], closeTo(0.5 / math.sqrt2, 0.03));
  });

  test('cache v2 vai e volta; mudo fica marcado; truncado e recusado', () async {
    final r = await scanMonoPcm(_pcmSilencioDepoisTom().path);
    final d = WaveformCacheData(
      hasAudio: true,
      sampleRate: 16000,
      samplesPerBucket: waveBaseBucket,
      lufs: -23.5,
      peaksPerSecond: 100,
      peaks: r.peaks,
      baseMin: r.baseMin,
      baseMax: r.baseMax,
      baseRms: r.baseRms,
    );
    final bytes = encodeWaveformCache(d);
    final volta = decodeWaveformCache(bytes)!;
    expect(volta.hasAudio, isTrue);
    expect(volta.lufs, -23.5);
    expect(volta.peaks, orderedEquals(r.peaks));
    expect(volta.baseMax.length, r.baseMax.length);
    for (var i = 0; i < r.baseMax.length; i += 37) {
      expect(volta.baseMax[i], closeTo(r.baseMax[i], 1 / 127 + 1e-6));
      expect(volta.baseRms[i], closeTo(r.baseRms[i], 1 / 255 + 1e-6));
    }
    final mudo = decodeWaveformCache(encodeWaveformCache(WaveformCacheData.semAudio()))!;
    expect(mudo.hasAudio, isFalse);
    expect(mudo.lufs, isNull);
    expect(decodeWaveformCache(Uint8List.sublistView(bytes, 0, bytes.length - 5)), isNull);
  });

  test('ganho de exibicao deixa fala baixa legivel sem estourar', () {
    final baixa = Float32List.fromList(List.filled(1000, 0.05));
    expect(waveformDisplayGain(baixa), closeTo(12, 1e-9));
    final normal = Float32List.fromList(List.filled(1000, 0.9));
    expect(waveformDisplayGain(normal), closeTo(1, 1e-6));
  });

  test('a onda segue o que TOCA: corte, reverso e Time Remap', () {
    final cortado = fonteAoLongoDoClipe(_video(offset: const Duration(seconds: 3)));
    expect(cortado, hasLength(2));
    expect(cortado.first, closeTo(3, 1e-6));
    expect(cortado.last, closeTo(5, 1e-6));

    final reverso = fonteAoLongoDoClipe(_video(reverse: true), amostras: 8);
    expect(reverso.first, greaterThan(reverso.last), reason: 'espelhada');

    final remap = AnimatedDouble(0)
        .withKeyframe(Duration.zero, 0, const Easing(type: EasingType.hold))
        .withKeyframe(const Duration(seconds: 1), 1)
        .withKeyframe(const Duration(seconds: 2), 2);
    final f = fonteAoLongoDoClipe(_video(remap: remap), amostras: 8);
    expect(f[1], closeTo(0, 1e-6), reason: 'congelado no primeiro segundo');
    expect(f[3], closeTo(0, 1e-6));
    expect(f[6], closeTo(1.5, 1e-6));
  });

  testWidgets('o desenho alinha: silencio a esquerda, tom a direita', (tester) async {
    final r = await tester.runAsync(() => scanMonoPcm(_pcmSilencioDepoisTom().path));
    final p = pyramidFromBase(r!.baseMin, r.baseMax, r.baseRms, 16000);
    const w = 200, h = 60;
    final rec = ui.PictureRecorder();
    ClipWaveformPainter(
      pyramid: p,
      fonte: Float64List.fromList([0, 2]),
      color: Colors.white,
    ).paint(Canvas(rec), const Size(200, 60));
    final img = await tester.runAsync(() => rec.endRecording().toImage(w, h));
    final data = await tester.runAsync(() => img!.toByteData());
    int pintados(int x0, int x1) {
      var n = 0;
      for (var y = 0; y < h; y++) {
        for (var x = x0; x < x1; x++) {
          if (data!.getUint8((y * w + x) * 4 + 3) > 40) n++;
        }
      }
      return n;
    }

    final esquerda = pintados(5, 95), direita = pintados(105, 195);
    expect(direita, greaterThan(90 * 20), reason: 'o tom ocupa altura');
    expect(esquerda, lessThan(90 * 3), reason: 'no silencio so a linha do meio');
  });
}
