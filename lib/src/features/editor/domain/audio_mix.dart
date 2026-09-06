import 'dart:math' as math;
import 'dart:typed_data';

import 'audio_ops.dart';
import 'layer.dart';

/// A MIXAGEM — a unica conta de ganho que o preview e a exportacao tem
/// direito de usar.
///
/// O contrato do editor e que o arquivo exportado soe como o preview. Ate
/// aqui isso nao valia para o audio: o preview aplicava so o volume da
/// camada, a exportacao aplicava ganho, fades e um compressor de cadeia
/// lateral do FFmpeg. Tres coisas diferentes, tres resultados diferentes.
///
/// A saida e esta: o ducking vira ENVELOPE PRE-CALCULADO — uma lista de
/// pontos no tempo — e tanto o tocador quanto o filtro de exportacao leem
/// os mesmos pontos. Deixa de ser uma decisao em tempo real (que cada
/// lado tomaria do seu jeito) e vira um numero combinado.

/// Um envelope de ganho: pontos no tempo, reta entre eles.
class DuckEnvelope {
  const DuckEnvelope(this.points);

  /// Neutro: nao abaixa nada em lugar nenhum.
  static const DuckEnvelope neutro = DuckEnvelope([]);

  /// Ordenados por tempo. Ganho em 0..1.
  final List<({Duration t, double g})> points;

  bool get isNeutral => points.isEmpty || points.every((p) => p.g >= 0.999);

  /// O ganho em [t], com reta entre os pontos. Antes do primeiro e
  /// depois do ultimo, segura o valor da ponta — a musica nao pode
  /// saltar so porque o envelope acabou.
  double gainAt(Duration t) {
    if (points.isEmpty) return 1;
    if (t <= points.first.t) return points.first.g;
    if (t >= points.last.t) return points.last.g;

    var lo = 0;
    var hi = points.length - 1;
    while (hi - lo > 1) {
      final mid = (lo + hi) ~/ 2;
      if (points[mid].t <= t) {
        lo = mid;
      } else {
        hi = mid;
      }
    }
    final a = points[lo];
    final b = points[hi];
    final span = (b.t - a.t).inMicroseconds;
    if (span <= 0) return b.g;
    final f = (t - a.t).inMicroseconds / span;
    return a.g + (b.g - a.g) * f;
  }
}

/// PRE-CALCULA o envelope a partir dos picos da voz.
///
/// [duckEnvelope] entrega 100 valores por segundo; guardar isso para uma
/// musica de tres minutos seriam 18 mil pontos, e a exportacao teria de
/// escrever os 18 mil numa expressao de filtro. A curva, porem, e quase
/// toda reta: ou esta em cima, ou esta no piso, ou esta numa rampa entre
/// os dois. [tolerance] descarta os pontos que a reta ja explica.
DuckEnvelope buildDuckEnvelope(
  Float32List voicePeaks, {
  double amount = 0.7,
  double threshold = 0.05,
  Duration attack = const Duration(milliseconds: 120),
  Duration release = const Duration(milliseconds: 450),
  Duration offset = Duration.zero,
  double tolerance = 0.01,
}) {
  if (voicePeaks.isEmpty || amount <= 0) return DuckEnvelope.neutro;
  final env = duckEnvelope(
    voicePeaks,
    amount: amount,
    threshold: threshold,
    attack: attack,
    release: release,
  );
  if (env.isEmpty) return DuckEnvelope.neutro;

  final us = 1000000 / audioPeaksPerSecond;
  Duration tempo(int i) =>
      offset + Duration(microseconds: (i * us).round());

  final mantidos = _simplificar(env, tolerance);
  final pontos = <({Duration t, double g})>[
    // ANTES DA VOZ nao ha abaixamento. A ancora fica COLADA no inicio
    // dela, nao no inicio do projeto: no zero, a reta ate o primeiro
    // ponto viraria um fade lento de minutos, e a musica ja entraria
    // cedendo para uma locucao que ainda nao comecou.
    if (offset > const Duration(milliseconds: 1))
      (t: offset - const Duration(milliseconds: 1), g: 1.0),
    for (final i in mantidos) (t: tempo(i), g: env[i].toDouble()),
  ];
  // DEPOIS DA VOZ tambem nao ha. Se a locucao foi cortada no meio de uma
  // frase, o envelope pararia abaixado e a musica nunca mais voltaria;
  // ela volta no mesmo tempo de repouso do resto.
  if (pontos.isNotEmpty && pontos.last.g < 0.999) {
    pontos.add((t: pontos.last.t + release, g: 1.0));
  }
  return DuckEnvelope(pontos);
}

/// Douglas-Peucker sobre a curva (indice, ganho): guarda so os pontos
/// que a reta entre as pontas erraria por mais de [tolerance].
List<int> _simplificar(Float32List v, double tolerance) {
  if (v.length < 3) return [for (var i = 0; i < v.length; i++) i];
  final manter = List<bool>.filled(v.length, false);
  manter[0] = true;
  manter[v.length - 1] = true;

  final pilha = <(int, int)>[(0, v.length - 1)];
  while (pilha.isNotEmpty) {
    final (a, b) = pilha.removeLast();
    if (b - a < 2) continue;
    final ga = v[a];
    final gb = v[b];
    final span = (b - a).toDouble();
    var pior = 0.0;
    var idx = -1;
    for (var i = a + 1; i < b; i++) {
      final reta = ga + (gb - ga) * ((i - a) / span);
      final erro = (v[i] - reta).abs();
      if (erro > pior) {
        pior = erro;
        idx = i;
      }
    }
    if (idx >= 0 && pior > tolerance) {
      manter[idx] = true;
      pilha.add((a, idx));
      pilha.add((idx, b));
    }
  }
  return [
    for (var i = 0; i < v.length; i++)
      if (manter[i]) i,
  ];
}

/// O GANHO DESTA CAMADA NESTE INSTANTE — volume, ganho, mudo, fade e
/// ducking, na ordem em que se multiplicam.
///
/// [timelineTime] e tempo de linha do tempo, nao de clipe: o envelope de
/// ducking e da linha (ele fala da voz, que e outra camada), enquanto o
/// fade e do clipe. Misturar os dois foi um bug de verdade.
double layerAudioGainAt(
  Layer layer,
  Duration timelineTime, {
  DuckEnvelope duck = DuckEnvelope.neutro,
}) {
  final spec = audioSpecOf(layer);
  if (spec == null) return 1;
  if (spec.muted) return 0;

  final base = switch (layer) {
    AudioLayer a => a.volume,
    VideoLayer v => v.volume,
    _ => 1.0,
  };
  var g = base * spec.gain;
  if (g <= 0) return 0;

  final local = timelineTime - layer.startTime;
  g *= fadeGainAt(
    local,
    layer.duration,
    fadeIn: spec.fadeIn,
    fadeOut: spec.fadeOut,
  );
  if (spec.duckAgainstId != null) g *= duck.gainAt(timelineTime);
  return g.clamp(0.0, 12.0);
}

/// A ficha de audio da camada, se ela tiver som.
AudioSpec? audioSpecOf(Layer layer) => switch (layer) {
      AudioLayer a => a.audio,
      VideoLayer v => v.audio,
      _ => null,
    };

// ------------------------------------------------------------ limitador

/// O TETO do barramento. Nao e 1,0: a conversao para inteiro e a
/// recodificacao podem passar alguns milesimos do que a soma tinha, e
/// clipping digital nao tem volta.
const double kTetoDoBarramento = 0.95;

/// Quanto o barramento precisa abaixar para caber no teto.
///
/// Estatico de proposito: um limitador com ataque e repouso decide em
/// tempo real, e a decisao do tocador nao seria a mesma do FFmpeg. Um
/// ganho constante por projeto e reproduzivel dos dois lados.
double busGainFor(double picoDaSoma, {double ceiling = kTetoDoBarramento}) {
  if (!picoDaSoma.isFinite || picoDaSoma <= ceiling) return 1;
  return ceiling / picoDaSoma;
}

/// ONDE A SOMA ESTOURA — os instantes que a forma de onda marca em
/// vermelho na altura de decupagem.
///
/// E o unico lugar onde preview e arquivo exportado podem divergir: o
/// limitador da exportacao segura, o tocador do sistema nao. Marcar e
/// mais honesto que esconder.
List<Duration> clippingAt(
  Float32List somaDePicos, {
  double ceiling = 1.0,
  int perSecond = audioPeaksPerSecond,
}) {
  final out = <Duration>[];
  if (somaDePicos.isEmpty || perSecond <= 0) return out;
  final us = 1000000 / perSecond;
  var dentro = false;
  for (var i = 0; i < somaDePicos.length; i++) {
    final estoura = somaDePicos[i] >= ceiling;
    if (estoura && !dentro) {
      out.add(Duration(microseconds: (i * us).round()));
    }
    dentro = estoura;
  }
  return out;
}

/// A SOMA DOS PICOS das trilhas, ja com o ganho de cada uma aplicado.
///
/// E uma estimativa por cima: dois picos no mesmo balde podem ter sinais
/// opostos e se cancelar. Errar para o lado seguro e o certo aqui — o
/// limitador entra por precaucao, nao por susto.
Float32List sumPeaks(List<(Float32List peaks, double gain)> trilhas) {
  var n = 0;
  for (final t in trilhas) {
    n = math.max(n, t.$1.length);
  }
  final out = Float32List(n);
  for (final (peaks, gain) in trilhas) {
    for (var i = 0; i < peaks.length; i++) {
      out[i] += peaks[i] * gain;
    }
  }
  return out;
}


/// A CURVA DO ENVELOPE COMO EXPRESSAO DO FILTRO `volume` DO FFMPEG.
///
/// E a transcricao de [DuckEnvelope.gainAt] para a linguagem do filtro —
/// e o ponto exato onde preview e arquivo poderiam divergir, entao ela
/// mora aqui, no dominio, onde da para testar sem rodar FFmpeg.
///
/// Soma de trechos, nao `if` encaixado: com cinquenta pontos o encaixe
/// vira cinquenta niveis de parenteses. Cada trecho e meio aberto
/// (`gte` na entrada, `lt` na saida) para o ponto de junta nao contar
/// duas vezes e dobrar o ganho por um instante.
String? ffmpegVolumeExpr(DuckEnvelope env) {
  final p = env.points;
  if (p.length < 2) return null;
  String n(double v) => v.toStringAsFixed(4);
  double seg(Duration d) => d.inMicroseconds / 1000000.0;

  final partes = <String>[
    'lt(t,${n(seg(p.first.t))})*${n(p.first.g)}',
  ];
  for (var i = 0; i < p.length - 1; i++) {
    final t0 = seg(p[i].t);
    final t1 = seg(p[i + 1].t);
    final span = t1 - t0;
    if (span <= 0) continue;
    final g0 = p[i].g;
    final dg = p[i + 1].g - g0;
    partes.add(
      'gte(t,${n(t0)})*lt(t,${n(t1)})*'
      '(${n(g0)}+${n(dg)}*(t-${n(t0)})/${n(span)})',
    );
  }
  partes.add('gte(t,${n(seg(p.last.t))})*${n(p.last.g)}');
  return partes.join('+');
}
