import 'dart:math' as math;
import 'dart:typed_data';

import 'dsp.dart';

/// SONORIDADE EM LUFS (ITU-R BS.1770-4).
///
/// Normalizar por PICO e o que quase todo editor de celular faz, e e por
/// isso que uma playlist de clipes normalizados continua soando desigual:
/// o pico diz o quanto o sinal chega perto de estourar, nao o quanto ele
/// SOA alto. Uma locucao seca e um trecho de bateria podem ter o mesmo
/// pico e uma diferenca de 10 dB na percepcao.
///
/// O LUFS mede sonoridade: filtra o sinal como a cabeca humana filtra
/// (a curva K), mede energia em blocos de 400 ms e joga fora os blocos
/// silenciosos antes de tirar a media. E o que faz "normalizar" entregar
/// tres clipes que soam iguais.
///
/// Aqui e MONO — que e a forma como o app ja guarda as amostras para
/// desenhar a onda. Estereo somaria os canais com peso 1,0 cada.

/// O sinal depois da curva K. Exposto porque o medidor ao vivo da onda
/// usa o mesmo filtro — medir com filtros diferentes daria dois numeros
/// para a mesma coisa.
Float32List kWeight(Float32List samples, int rate) {
  if (samples.isEmpty || rate <= 0) return samples;
  // Os numeros sao os da norma: prateleira alta de 4 dB em 1682 Hz e
  // passa-alta em 38 Hz.
  final prateleira = highShelf(rate, 1681.974450955533, 3.999843853973347,
      q: 0.7071752369554196);
  final corte = highPass(rate, 38.13547087602444, q: 0.5003270373238773);
  return corte.apply(prateleira.apply(samples));
}

/// O silencio absoluto do padrao: bloco abaixo disso nao conta.
const double kPortaoAbsoluto = -70.0;

/// Sonoridade de cada bloco de 400 ms, com 75% de sobreposicao.
///
/// A sobreposicao existe para que uma frase curta nao caia inteira na
/// junta entre dois blocos e desapareca da conta.
List<double> blockLoudness(Float32List samples, int rate) {
  if (samples.isEmpty || rate <= 0) return const [];
  final y = kWeight(samples, rate);
  final bloco = (rate * 0.4).round();
  final passo = (rate * 0.1).round();
  if (bloco <= 0 || passo <= 0 || y.length < bloco) return const [];

  final out = <double>[];
  for (var inicio = 0; inicio + bloco <= y.length; inicio += passo) {
    var soma = 0.0;
    for (var i = inicio; i < inicio + bloco; i++) {
      soma += y[i] * y[i];
    }
    final z = soma / bloco;
    if (z <= 0) continue;
    out.add(-0.691 + 10 * (math.log(z) / math.ln10));
  }
  return out;
}

/// SONORIDADE INTEGRADA, com os dois portoes do padrao.
///
/// Devolve `null` quando nao sobrou bloco nenhum — faixa muda nao tem
/// sonoridade, e responder um numero seria inventar.
double? integratedLufs(Float32List samples, int rate) {
  final blocos = blockLoudness(samples, rate);
  if (blocos.isEmpty) return null;

  // De volta para energia: a media tem de ser de energia, nao de dB.
  double energia(double lufs) => math.pow(10, (lufs + 0.691) / 10).toDouble();

  final acimaDoAbsoluto = [
    for (final l in blocos)
      if (l > kPortaoAbsoluto) l,
  ];
  if (acimaDoAbsoluto.isEmpty) return null;

  var soma = 0.0;
  for (final l in acimaDoAbsoluto) {
    soma += energia(l);
  }
  final medio = -0.691 + 10 * (math.log(soma / acimaDoAbsoluto.length) /
      math.ln10);

  // PORTAO RELATIVO: 10 LU abaixo da media preliminar. E o que impede
  // que o silencio entre as frases puxe a locucao inteira para baixo.
  final portao = medio - 10;
  final valem = [
    for (final l in acimaDoAbsoluto)
      if (l > portao) l,
  ];
  if (valem.isEmpty) return medio;

  soma = 0.0;
  for (final l in valem) {
    soma += energia(l);
  }
  return -0.691 + 10 * (math.log(soma / valem.length) / math.ln10);
}

/// O ALVO padrao: -14 LUFS e o que as plataformas de video usam. Quem
/// entrega mais alto que isso e abaixado por elas de qualquer jeito.
const double lufsAlvoPadrao = -14.0;

/// O ganho que leva uma faixa JA MEDIDA ao alvo.
///
/// Separado de [normalizeGainLufs] porque quem ja tem a medida guardada
/// nao pode ser obrigado a decodificar o arquivo de novo so para
/// converter um numero.
double normalizeGainForLufs(double lufs, {double target = lufsAlvoPadrao}) {
  if (!lufs.isFinite) return 1;
  return math.pow(10, (target - lufs) / 20).toDouble().clamp(0.05, 12.0);
}

/// GANHO DE NORMALIZACAO por sonoridade.
///
/// O teto de 12x nao e capricho: uma gravacao quase muda pediria ganho
/// de 40x e traria o chiado do microfone junto.
double normalizeGainLufs(
  Float32List samples,
  int rate, {
  double target = lufsAlvoPadrao,
}) {
  final lufs = integratedLufs(samples, rate);
  if (lufs == null || !lufs.isFinite) return 1;
  return math.pow(10, (target - lufs) / 20).toDouble().clamp(0.05, 12.0);
}
