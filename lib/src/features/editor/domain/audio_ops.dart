import 'dart:math' as math;
import 'dart:typed_data';

/// OPERACOES DE AUDIO — as contas que fazem o som ficar montavel.
///
/// Todas trabalham sobre o ENVELOPE DE PICO ja calculado para desenhar a
/// forma de onda (100 picos por segundo). Reaproveitar esse envelope em
/// vez de reabrir o arquivo e o que deixa "remover silencio" responder
/// no toque, em vez de rodar o FFmpeg de novo.

/// Picos por segundo do envelope — tem de bater com quem gerou.
const audioPeaksPerSecond = 100;

Duration _atIndex(int i) =>
    Duration(microseconds: (i * 1000000 / audioPeaksPerSecond).round());

/// SILENCIO: trechos abaixo de [threshold] por pelo menos
/// [minSilence].
///
/// O limiar e em AMPLITUDE (0..1), nao em dB, porque e o mesmo numero
/// que a forma de onda desenha — a pessoa ve a linha baixinha e sabe
/// onde o corte vai cair.
///
/// [padding] encolhe cada trecho nas duas pontas: cortar exatamente no
/// limiar decepa o comeco da consoante e o final da vogal, e a fala sai
/// picotada. Uns 120 ms de folga resolvem.
List<(Duration, Duration)> detectSilence(
  Float32List peaks, {
  double threshold = 0.035,
  Duration minSilence = const Duration(milliseconds: 350),
  Duration padding = const Duration(milliseconds: 120),
}) {
  if (peaks.isEmpty) return const [];
  final minLen =
      (minSilence.inMilliseconds * audioPeaksPerSecond / 1000).round();
  final pad = (padding.inMilliseconds * audioPeaksPerSecond / 1000).round();

  final out = <(Duration, Duration)>[];
  var start = -1;
  for (var i = 0; i <= peaks.length; i++) {
    final quieto = i < peaks.length && peaks[i] < threshold;
    if (quieto) {
      if (start < 0) start = i;
      continue;
    }
    if (start >= 0) {
      final len = i - start;
      if (len >= minLen) {
        final a = start + pad;
        final b = i - pad;
        if (b > a) out.add((_atIndex(a), _atIndex(b)));
      }
      start = -1;
    }
  }
  return out;
}

/// O contrario do silencio: onde HA som. E o que sobra depois de
/// remover silencio, e o que a decupagem realmente mantem.
List<(Duration, Duration)> detectSpeech(
  Float32List peaks, {
  double threshold = 0.035,
  Duration minSilence = const Duration(milliseconds: 350),
  Duration padding = const Duration(milliseconds: 120),
}) {
  if (peaks.isEmpty) return const [];
  final silencios = detectSilence(peaks,
      threshold: threshold, minSilence: minSilence, padding: padding);
  final total = _atIndex(peaks.length);

  final out = <(Duration, Duration)>[];
  var cursor = Duration.zero;
  for (final s in silencios) {
    if (s.$1 > cursor) out.add((cursor, s.$1));
    cursor = s.$2;
  }
  if (cursor < total) out.add((cursor, total));
  return out;
}

/// BATIDAS: onde a energia SOBE de repente.
///
/// Nao e detector de tempo musical — e detector de ataque, que e o que
/// serve para encaixar corte no ritmo. Compara cada pico com a media
/// dos anteriores: se estourou [sensitivity] vezes a media, e ataque.
///
/// [minGap] evita marcar a mesma batida duas vezes por causa do
/// sustain.
List<Duration> detectBeats(
  Float32List peaks, {
  double sensitivity = 1.6,
  Duration minGap = const Duration(milliseconds: 200),
  int window = 43,
}) {
  if (peaks.length < window + 2) return const [];
  final gap = (minGap.inMilliseconds * audioPeaksPerSecond / 1000).round();
  final out = <Duration>[];
  var ultimo = -gap;

  for (var i = window; i < peaks.length; i++) {
    var soma = 0.0;
    for (var j = i - window; j < i; j++) {
      soma += peaks[j];
    }
    final media = soma / window;
    if (media < 0.01) continue;
    if (peaks[i] > media * sensitivity && i - ultimo >= gap) {
      out.add(_atIndex(i));
      ultimo = i;
    }
  }
  return out;
}

/// GANHO DE NORMALIZACAO: quanto multiplicar para o pico mais alto
/// chegar em [target].
///
/// Usa o percentil 99, nao o maximo absoluto: um estalo isolado nao
/// pode decidir o volume da faixa inteira.
double normalizeGain(Float32List peaks, {double target = 0.89}) {
  if (peaks.isEmpty) return 1;
  final ordenados = Float32List.fromList(peaks)..sort();
  final idx = ((ordenados.length - 1) * 0.99).floor();
  final pico = ordenados[idx];
  if (pico <= 0.0001) return 1;
  return (target / pico).clamp(0.1, 12.0);
}

/// Ganho -> decibeis, para mostrar na interface. Silencio vira -inf, que
/// a UI escreve como "mudo".
double gainToDb(double gain) =>
    gain <= 0 ? double.negativeInfinity : 20 * (math.log(gain) / math.ln10);

double dbToGain(double db) =>
    db.isFinite ? math.pow(10, db / 20).toDouble() : 0;

/// ENVELOPE DE FADE em [t], dentro de um clipe de [duration].
///
/// A curva e igual-potencia (seno), nao linear: fade linear de volume
/// soa como um buraco no meio, porque o ouvido responde a potencia.
double fadeGainAt(
  Duration t,
  Duration duration, {
  Duration fadeIn = Duration.zero,
  Duration fadeOut = Duration.zero,
}) {
  final us = t.inMicroseconds;
  final total = duration.inMicroseconds;
  if (total <= 0) return 1;
  if (us < 0 || us > total) return 0;

  var g = 1.0;
  final inUs = fadeIn.inMicroseconds;
  if (inUs > 0 && us < inUs) {
    g *= math.sin((us / inUs) * math.pi / 2);
  }
  final outUs = fadeOut.inMicroseconds;
  if (outUs > 0 && us > total - outUs) {
    final f = (total - us) / outUs;
    g *= math.sin(f.clamp(0.0, 1.0) * math.pi / 2);
  }
  return g.clamp(0.0, 1.0);
}

/// DUCKING: quanto a trilha tem de abaixar por causa da voz.
///
/// Devolve 1 quando nao ha voz e (1 - amount) no meio dela, com subida
/// e descida suaves — o corte seco chama mais atencao que a musica alta.
///
/// [attack] e curto porque a musica precisa sair da frente ANTES da
/// silaba; [release] e longo porque voltar rapido soa como bombeamento.
Float32List duckEnvelope(
  Float32List voicePeaks, {
  double amount = 0.7,
  double threshold = 0.05,
  Duration attack = const Duration(milliseconds: 120),
  Duration release = const Duration(milliseconds: 450),
}) {
  final n = voicePeaks.length;
  final out = Float32List(n);
  if (n == 0) return out;

  final piso = (1 - amount).clamp(0.0, 1.0);
  final aPassos =
      math.max(1, (attack.inMilliseconds * audioPeaksPerSecond / 1000).round());
  final rPassos = math.max(
      1, (release.inMilliseconds * audioPeaksPerSecond / 1000).round());

  var g = 1.0;
  for (var i = 0; i < n; i++) {
    final alvo = voicePeaks[i] >= threshold ? piso : 1.0;
    if (alvo < g) {
      g = math.max(alvo, g - (1 - piso) / aPassos);
    } else if (alvo > g) {
      g = math.min(alvo, g + (1 - piso) / rPassos);
    }
    out[i] = g;
  }
  return out;
}

/// Junta trechos que ficaram perto demais um do outro.
///
/// Depois de remover silencio, dois pedacos separados por 80 ms viram
/// dois cortes que ninguem percebe — e duas camadas a mais para
/// gerenciar. Colar e mais honesto.
List<(Duration, Duration)> mergeClose(
  List<(Duration, Duration)> ranges, {
  Duration maxGap = const Duration(milliseconds: 200),
}) {
  if (ranges.length < 2) return ranges;
  final ordenados = [...ranges]..sort((a, b) => a.$1.compareTo(b.$1));
  final out = <(Duration, Duration)>[ordenados.first];
  for (final r in ordenados.skip(1)) {
    final ultimo = out.last;
    if (r.$1 - ultimo.$2 <= maxGap) {
      out[out.length - 1] = (ultimo.$1, r.$2 > ultimo.$2 ? r.$2 : ultimo.$2);
    } else {
      out.add(r);
    }
  }
  return out;
}

// --------------------------------------------------- batidas por faixa

/// A FAIXA DE FREQUENCIA que decide o que conta como batida.
///
/// Numa musica o bumbo e o chimbal atacam em instantes diferentes, e
/// cortar no bumbo ou no chimbal da montagens diferentes. Detectar na
/// mistura inteira acha "o mais alto", que costuma ser nenhum dos dois.
enum BeatBand { grave, medio, agudo, tudo }

/// Filtro passa-baixa de um polo. Simples de proposito: para SEPARAR
/// bumbo de chimbal a inclinacao de 6 dB por oitava basta, e ela custa
/// uma multiplicacao por amostra — cabe em audio longo no celular.
Float32List _passaBaixa(Float32List x, int rate, double corte) {
  if (x.isEmpty || rate <= 0) return x;
  final a = 1 - math.exp(-2 * math.pi * corte / rate);
  final out = Float32List(x.length);
  var y = 0.0;
  for (var i = 0; i < x.length; i++) {
    y += a * (x[i] - y);
    out[i] = y;
  }
  return out;
}

/// Passa-alta = o sinal menos a parte grave dele.
Float32List _passaAlta(Float32List x, int rate, double corte) {
  final grave = _passaBaixa(x, rate, corte);
  final out = Float32List(x.length);
  for (var i = 0; i < x.length; i++) {
    out[i] = x[i] - grave[i];
  }
  return out;
}

/// ENVELOPE DE ENERGIA de uma faixa, na mesma resolucao dos picos.
///
/// Devolve o pico absoluto por balde, normalizado em 0..1 — a mesma
/// forma que [computePeaks] entrega, para o detector de ataque nao
/// precisar saber de onde veio.
Float32List bandEnvelope(
  Int16List samples,
  int rate,
  int perSecond,
  BeatBand band,
) {
  if (samples.isEmpty || rate <= 0 || perSecond <= 0) {
    return Float32List(0);
  }
  var x = Float32List(samples.length);
  for (var i = 0; i < samples.length; i++) {
    x[i] = samples[i] / 32768.0;
  }
  switch (band) {
    case BeatBand.grave:
      // Duas passadas: 12 dB por oitava separa bumbo de caixa.
      x = _passaBaixa(_passaBaixa(x, rate, 150), rate, 150);
    case BeatBand.agudo:
      x = _passaAlta(x, rate, 4000);
    case BeatBand.medio:
      x = _passaAlta(_passaBaixa(x, rate, 4000), rate, 250);
    case BeatBand.tudo:
      break;
  }

  final perBucket = rate ~/ perSecond;
  if (perBucket < 1) return Float32List(0);
  final count = x.length ~/ perBucket;
  final out = Float32List(count);
  var maiorDeTodos = 0.0;
  for (var i = 0; i < count; i++) {
    final start = i * perBucket;
    var pico = 0.0;
    for (var j = 0; j < perBucket; j++) {
      final v = x[start + j].abs();
      if (v > pico) pico = v;
    }
    out[i] = pico;
    if (pico > maiorDeTodos) maiorDeTodos = pico;
  }
  // Normaliza: filtrar tira energia, e um limiar relativo comparado com
  // envelope encolhido acharia batida em tudo ou em nada.
  if (maiorDeTodos > 0.0001) {
    for (var i = 0; i < count; i++) {
      out[i] = out[i] / maiorDeTodos;
    }
  }
  return out;
}

/// O ANDAMENTO, em batidas por minuto, a partir dos ataques.
///
/// Usa a MEDIANA dos intervalos, nao a media: um ataque perdido dobra um
/// intervalo, e a media inteira escorrega atras dele. Depois dobra ou
/// divide ate cair na faixa que se usa para editar (60..180) — o mesmo
/// pulso pode ser lido como 75 ou 150, e as duas leituras sao a mesma
/// musica.
double? estimateBpm(List<Duration> onsets) {
  if (onsets.length < 3) return null;
  final intervalos = <int>[];
  for (var i = 1; i < onsets.length; i++) {
    final d = (onsets[i] - onsets[i - 1]).inMicroseconds;
    if (d > 60000) intervalos.add(d);
  }
  if (intervalos.isEmpty) return null;
  intervalos.sort();
  final mediana = intervalos[intervalos.length ~/ 2];
  var bpm = 60000000 / mediana;
  while (bpm < 60 && bpm > 0) {
    bpm *= 2;
  }
  while (bpm > 180) {
    bpm /= 2;
  }
  return bpm;
}

/// A GRADE do ritmo, a partir de um andamento.
///
/// Os ataques detectados tremem alguns milissegundos; encaixar corte
/// neles herda o tremor. A grade e regular por construcao, entao o corte
/// cai no tempo — que e o que se quer ouvir.
///
/// [denominador] e a figura em compasso 4/4: 1 marca por compasso, 2 a
/// cada dois tempos, 4 em cada tempo (o comum), 8 duas vezes por tempo.
List<Duration> beatGrid({
  required Duration first,
  required double bpm,
  required int denominador,
  required Duration until,
}) {
  if (bpm <= 0 || until <= first) return const [];
  final d = denominador < 1 ? 4 : denominador;
  final passoUs = (60000000 / bpm) * (4 / d);
  if (passoUs < 1000) return const [];
  final out = <Duration>[];
  var t = first.inMicroseconds.toDouble();
  final fim = until.inMicroseconds;
  // Teto de seguranca: grade densa em faixa longa nao pode virar milhoes
  // de marcas e travar o desenho da regua.
  while (t <= fim && out.length < 4000) {
    out.add(Duration(microseconds: t.round()));
    t += passoUs;
  }
  return out;
}
