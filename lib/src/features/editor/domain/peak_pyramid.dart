import 'dart:math' as math;
import 'dart:typed_data';

/// PIRAMIDE DE PICOS.
///
/// Uma forma de onda de dez minutos tem 26 milhoes de amostras. Desenhar
/// isso a cada quadro, em cada nivel de zoom, e o que faz a linha do
/// tempo engasgar — e recalcular ao ampliar e pior ainda.
///
/// A saida e guardar VARIOS niveis de detalhe de uma vez: 64, 256, 1024,
/// 4096, 16384 e 65536 amostras por balde. Na hora de desenhar, escolhe
/// o nivel cujo balde seja do tamanho de um pixel — e ampliar so troca
/// de nivel, nunca recalcula.
///
/// Cada balde guarda MINIMO, MAXIMO e RMS:
///
///   min/max  o contorno — e o que mostra o transiente, a batida seca
///            que a media achataria;
///   RMS      o corpo — e o que mostra o quao ALTO esta, que e o que o
///            ouvido percebe.
///
/// Desenhar so o pico da uma mancha cheia; so o RMS, uma forma sem
/// ataque. Os dois juntos sao a forma de onda que se reconhece.
class PeakLevel {
  const PeakLevel({
    required this.samplesPerBucket,
    required this.sampleRate,
    required this.min,
    required this.max,
    required this.rms,
  });

  final int samplesPerBucket;
  final int sampleRate;

  /// Todos em -1..1 (o RMS em 0..1).
  final Float32List min;
  final Float32List max;
  final Float32List rms;

  int get length => max.length;

  /// Quantos segundos cada balde cobre.
  double get bucketSeconds =>
      sampleRate <= 0 ? 0 : samplesPerBucket / sampleRate;

  Duration get duration => Duration(
      microseconds: (length * bucketSeconds * 1000000).round());

  /// O indice do balde no instante [t].
  int bucketAt(Duration t) {
    final b = bucketSeconds;
    if (b <= 0) return 0;
    return (t.inMicroseconds / 1000000.0 / b).floor();
  }
}

/// Os tamanhos de balde da piramide, do mais fino ao mais grosso.
const peakBucketSizes = [64, 256, 1024, 4096, 16384, 65536];

class PeakPyramid {
  const PeakPyramid(this.levels, this.sampleRate);

  final List<PeakLevel> levels;
  final int sampleRate;

  bool get isEmpty => levels.isEmpty || levels.first.length == 0;

  Duration get duration =>
      levels.isEmpty ? Duration.zero : levels.first.duration;

  /// O nivel certo para desenhar com [secondsPerPixel].
  ///
  /// Escolhe o mais fino cujo balde ainda seja MAIOR OU IGUAL a um
  /// pixel: assim nunca se lê mais de um balde por pixel, e nunca falta
  /// detalhe. Ampliar muito cai no nivel mais fino e para por ali — nao
  /// existe detalhe abaixo dele, e inventar seria mentira.
  PeakLevel levelFor(double secondsPerPixel) {
    if (levels.isEmpty) {
      return PeakLevel(
        samplesPerBucket: 64,
        sampleRate: sampleRate,
        min: Float32List(0),
        max: Float32List(0),
        rms: Float32List(0),
      );
    }
    for (final l in levels) {
      if (l.bucketSeconds >= secondsPerPixel) return l;
    }
    return levels.last;
  }
}

/// Monta a piramide a partir das amostras.
///
/// O nivel mais fino le as amostras; cada nivel seguinte e feito do
/// ANTERIOR — minimo dos minimos, maximo dos maximos, e o RMS somado em
/// quadrado. Reler as amostras seis vezes custaria seis vezes mais e
/// daria exatamente o mesmo resultado.
PeakPyramid buildPeakPyramid(Int16List samples, int sampleRate) {
  if (samples.isEmpty || sampleRate <= 0) {
    return PeakPyramid(const [], sampleRate);
  }

  final base = peakBucketSizes.first;
  final n = samples.length ~/ base;
  if (n == 0) return PeakPyramid(const [], sampleRate);

  final min0 = Float32List(n);
  final max0 = Float32List(n);
  final rms0 = Float32List(n);

  for (var i = 0; i < n; i++) {
    var lo = 1.0, hi = -1.0, soma = 0.0;
    final inicio = i * base;
    for (var j = 0; j < base; j++) {
      final v = samples[inicio + j] / 32768.0;
      if (v < lo) lo = v;
      if (v > hi) hi = v;
      soma += v * v;
    }
    min0[i] = lo;
    max0[i] = hi;
    rms0[i] = math.sqrt(soma / base);
  }

  final niveis = <PeakLevel>[
    PeakLevel(
      samplesPerBucket: base,
      sampleRate: sampleRate,
      min: min0,
      max: max0,
      rms: rms0,
    ),
  ];

  for (var k = 1; k < peakBucketSizes.length; k++) {
    final anterior = niveis.last;
    final fator = peakBucketSizes[k] ~/ peakBucketSizes[k - 1];
    final m = anterior.length ~/ fator;
    if (m == 0) break;

    final mn = Float32List(m);
    final mx = Float32List(m);
    final rm = Float32List(m);
    for (var i = 0; i < m; i++) {
      var lo = 1.0, hi = -1.0, soma = 0.0;
      final inicio = i * fator;
      for (var j = 0; j < fator; j++) {
        final idx = inicio + j;
        if (anterior.min[idx] < lo) lo = anterior.min[idx];
        if (anterior.max[idx] > hi) hi = anterior.max[idx];
        soma += anterior.rms[idx] * anterior.rms[idx];
      }
      mn[i] = lo;
      mx[i] = hi;
      rm[i] = math.sqrt(soma / fator);
    }
    niveis.add(PeakLevel(
      samplesPerBucket: peakBucketSizes[k],
      sampleRate: sampleRate,
      min: mn,
      max: mx,
      rms: rm,
    ));
  }

  return PeakPyramid(niveis, sampleRate);
}
