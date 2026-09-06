import 'dart:math' as math;
import 'dart:typed_data';

import 'dsp.dart';

/// LIMPEZA E MELHORIA DE VOZ.
///
/// A promessa do nivel e "soar bem sem saber de audio": um toque resolve,
/// e quem quiser mexer encontra os numeros embaixo. Nada aqui inventa
/// qualidade — sao as tres coisas que de fato mudam a percepcao numa
/// gravacao de celular: tirar o ronco que ninguem ouve mas que ocupa
/// espaco, controlar a distancia da boca ao microfone, e devolver o
/// brilho que a compressao do celular comeu.
///
/// TODAS obedecem a neutralidade: intensidade 0 devolve a entrada.

/// COMPRESSOR SUAVE, com joelho e envelope.
///
/// O que ele corrige e a DISTANCIA: a pessoa se aproxima do microfone e
/// estoura, se afasta e some. Sem isso, normalizar so escolhe qual dos
/// dois problemas fica evidente.
Float32List softCompress(
  Float32List x, {
  required int rate,
  double thresholdDb = -18,
  double ratio = 3,
  double kneeDb = 6,
  Duration attack = const Duration(milliseconds: 10),
  Duration release = const Duration(milliseconds: 120),
  double makeupDb = 0,
  double intensity = 1,
}) {
  if (x.isEmpty || intensity <= 0 || ratio <= 1) return x;
  final aCoef = math.exp(-1 / (rate * attack.inMicroseconds / 1000000.0));
  final rCoef = math.exp(-1 / (rate * release.inMicroseconds / 1000000.0));
  final makeup = math.pow(10, makeupDb / 20).toDouble();

  final out = Float32List(x.length);
  var env = 0.0;
  for (var i = 0; i < x.length; i++) {
    final v = x[i].abs();
    // Envelope de pico: sobe rapido, desce devagar.
    env = v > env
        ? aCoef * env + (1 - aCoef) * v
        : rCoef * env + (1 - rCoef) * v;

    final db = env <= 1e-9 ? -120.0 : 20 * (math.log(env) / math.ln10);
    final acima = db - thresholdDb;
    double reducao;
    if (acima <= -kneeDb / 2) {
      reducao = 0;
    } else if (acima >= kneeDb / 2) {
      reducao = acima - acima / ratio;
    } else {
      // JOELHO: a entrada em compressao e gradual. Um joelho duro se
      // ouve como a voz grudando nas silabas mais altas.
      final t = acima + kneeDb / 2;
      reducao = (1 - 1 / ratio) * t * t / (2 * kneeDb);
    }
    final g = math.pow(10, -reducao * intensity / 20).toDouble();
    out[i] = x[i] * g * makeup;
  }
  return out;
}

/// EQUALIZADOR DE TRES BANDAS — grave, medio e agudo, em dB.
Float32List threeBandEq(
  Float32List x, {
  required int rate,
  double lowDb = 0,
  double midDb = 0,
  double highDb = 0,
}) {
  if (x.isEmpty || (lowDb == 0 && midDb == 0 && highDb == 0)) return x;
  var y = x;
  if (lowDb != 0) y = lowShelf(rate, 200, lowDb).apply(y);
  if (midDb != 0) y = peaking(rate, 1200, midDb, q: 0.9).apply(y);
  if (highDb != 0) y = highShelf(rate, 6000, highDb).apply(y);
  return y;
}

/// DE-ESSER: abaixa so a sibilancia.
///
/// Nao e um filtro fixo de agudos — isso abafaria a voz inteira. Ele
/// mede a energia da faixa do S e abaixa SO enquanto ela domina,
/// deixando o resto do brilho no lugar.
Float32List deEsser(
  Float32List x, {
  required int rate,
  double intensity = 1,
  double fc = 5500,
  double thresholdDb = -26,
}) {
  if (x.isEmpty || intensity <= 0) return x;
  final sibilancia = highPass(rate, fc).apply(x);
  final limiar = math.pow(10, thresholdDb / 20).toDouble();
  final coef = math.exp(-1 / (rate * 0.004));

  final out = Float32List(x.length);
  var env = 0.0;
  for (var i = 0; i < x.length; i++) {
    final v = sibilancia[i].abs();
    env = v > env ? v : coef * env + (1 - coef) * v;
    var g = 1.0;
    if (env > limiar) {
      g = (limiar / env).clamp(0.15, 1.0);
      g = 1 - (1 - g) * intensity;
    }
    // O ganho cai so na parte sibilante; o resto do sinal passa inteiro.
    out[i] = (x[i] - sibilancia[i]) + sibilancia[i] * g;
  }
  return out;
}

/// MELHORAR VOZ: o botao de um toque.
///
/// Passa-alta em 80 Hz (o ronco do transito e do ar-condicionado),
/// compressao leve (a distancia da boca) e um pouco de brilho (o que a
/// compressao do celular comeu). Nessa ordem: comprimir antes de tirar o
/// ronco faria o compressor reagir a energia que vai ser jogada fora.
Float32List enhanceVoice(
  Float32List x, {
  required int rate,
  double intensity = 1,
}) {
  if (x.isEmpty || intensity <= 0) return x;
  final i = intensity.clamp(0.0, 1.0);
  var y = highPass(rate, 80).apply(x);
  y = softCompress(
    y,
    rate: rate,
    thresholdDb: -18,
    ratio: 1 + 2 * i,
    makeupDb: 2.5 * i,
    intensity: i,
  );
  y = deEsser(y, rate: rate, intensity: 0.6 * i);
  return highShelf(rate, 6000, 3.0 * i).apply(y);
}

// ------------------------------------------------------------- ruido

/// Tamanho da janela da analise. 1024 amostras a 16 kHz sao 64 ms — tempo
/// suficiente para separar o chiado da fala, e curto o bastante para nao
/// borrar o ataque das consoantes.
const int kJanelaRuido = 1024;

/// O RETRATO DO CHIADO: a magnitude media de cada faixa num trecho onde
/// so ha ruido.
///
/// E isto que separa "tirar ruido" de "cortar agudo": sabendo a cara do
/// chiado daquela gravacao, da para tirar o chiado e deixar o resto.
class NoiseProfile {
  const NoiseProfile(this.magnitude, this.rate);

  final Float64List magnitude;
  final int rate;

  bool get isEmpty => magnitude.isEmpty;
}

/// Monta o perfil a partir de um trecho mudo — na pratica, o primeiro
/// silencio que o detector achou.
NoiseProfile noiseProfileFrom(Float32List trecho, int rate) {
  if (trecho.length < kJanelaRuido) {
    return NoiseProfile(Float64List(0), rate);
  }
  final janela = hannWindow(kJanelaRuido);
  final soma = Float64List(kJanelaRuido);
  var quadros = 0;
  for (var inicio = 0;
      inicio + kJanelaRuido <= trecho.length;
      inicio += kJanelaRuido ~/ 2) {
    final re = Float64List(kJanelaRuido);
    final im = Float64List(kJanelaRuido);
    for (var i = 0; i < kJanelaRuido; i++) {
      re[i] = trecho[inicio + i] * janela[i];
    }
    fft(re, im);
    for (var i = 0; i < kJanelaRuido; i++) {
      soma[i] += math.sqrt(re[i] * re[i] + im[i] * im[i]);
    }
    quadros++;
  }
  if (quadros == 0) return NoiseProfile(Float64List(0), rate);
  for (var i = 0; i < kJanelaRuido; i++) {
    soma[i] /= quadros;
  }
  return NoiseProfile(soma, rate);
}

/// TIRAR RUIDO DE FUNDO, por subtracao espectral.
///
/// Desmonta o som em faixas, tira de cada uma o tanto que o perfil diz
/// que e chiado, e remonta. O piso em [floor] existe para nao zerar faixa
/// nenhuma: zerar cria o ruido musical, aquele chiado borbulhante que soa
/// pior que o ruido original.
///
/// [oversubtraction] existe porque sinal e ruido somam em QUADRATURA, nao
/// em linha: a magnitude medida numa faixa com voz e chiado juntos fica
/// perto de raiz(voz^2 + chiado^2), e tirar so a magnitude media do
/// chiado deixa quase metade dele para tras. Tirar um pouco mais e o que
/// todo redutor faz — e o piso e o que impede que esse "um pouco mais"
/// coma a voz.
///
/// Intensidade 0 devolve a entrada SEM passar pela analise — ida e volta
/// pela transformada nao volta bit a bit, e neutro tem de ser neutro.
Float32List denoise(
  Float32List x, {
  required NoiseProfile profile,
  double intensity = 1,
  double floor = 0.12,
  double oversubtraction = 1.9,
}) {
  if (x.isEmpty || intensity <= 0 || profile.isEmpty) return x;
  if (x.length < kJanelaRuido) return x;

  final n = kJanelaRuido;
  final salto = n ~/ 2;
  final janela = hannWindow(n);
  final out = Float32List(x.length);
  var ultimoFim = 0;

  for (var inicio = 0; inicio + n <= x.length; inicio += salto) {
    final re = Float64List(n);
    final im = Float64List(n);
    for (var i = 0; i < n; i++) {
      re[i] = x[inicio + i] * janela[i];
    }
    fft(re, im);
    for (var i = 0; i < n; i++) {
      final mag = math.sqrt(re[i] * re[i] + im[i] * im[i]);
      if (mag <= 1e-12) continue;
      final tirar = profile.magnitude[i] * intensity * oversubtraction;
      final nova = math.max(mag - tirar, mag * floor);
      final g = nova / mag;
      re[i] *= g;
      im[i] *= g;
    }
    fft(re, im, inverse: true);
    for (var i = 0; i < n; i++) {
      out[inicio + i] += re[i];
    }
    ultimoFim = inicio + n;
  }

  // As pontas nao tem sobreposicao completa: ali a soma das janelas nao
  // chega a 1 e o volume cairia. Copiar o original nessas bordas e mais
  // honesto que entregar meio segundo sumindo no inicio e no fim.
  for (var i = 0; i < salto && i < x.length; i++) {
    out[i] = x[i];
  }
  for (var i = math.max(0, ultimoFim - salto); i < x.length; i++) {
    out[i] = x[i];
  }
  return out;
}
