import 'dart:math' as math;

import 'fx.dart' show fxHash01;

/// "ONE FRAME EDITS": efeitos de um ou dois quadros no ritmo da musica.
///
/// Na comunidade de edits isso se faz com uma camada de ajuste cortada no
/// beat e um efeito que dura um quadro (flash, negativo, zoom de soco,
/// fatias de glitch). O que torna isso rapido e o efeito saber CONTAR
/// QUADROS: segurar N quadros, cair em M, repetir a cada P ou disparar ao
/// acaso — sem keyframe nenhum.
///
/// Tudo aqui e funcao PURA de (quadro, parametros, semente): o scrub cai
/// sempre no mesmo resultado e a exportacao sai igual a previa.

/// O quadro da composicao em que o instante local cai (-1 antes do inicio).
int quadroLocal(Duration local, int fps) {
  if (local.isNegative) return -1;
  final f = fps < 1 ? 30 : fps;
  // Um microssegundo de folga: o relogio encaixa o tempo no primeiro
  // microssegundo do quadro, e a conta de volta nao pode cair no anterior.
  return ((local.inMicroseconds + 1) * f) ~/ 1000000;
}

/// Gatilhos de disparo.
abstract final class Gatilho {
  static const inicioDaCamada = 0;
  static const aCadaN = 1;
  static const aleatorio = 2;
  static const sempre = 3;
}

/// QUANTOS QUADROS SE PASSARAM desde o disparo mais recente (ou null,
/// se nao houve disparo ainda).
///
/// * inicio da camada: o disparo e o quadro zero;
/// * a cada N quadros: dispara em 0, N, 2N...;
/// * aleatorio: o tempo e dividido em celulas de N quadros; cada celula
///   dispara com [probabilidade], num quadro sorteado dentro dela — o
///   espaco minimo entre dois disparos e de uma celula;
/// * sempre: o efeito fica no primeiro quadro do envelope o tempo todo.
///
/// [alcance] limita quantas celulas para tras o aleatorio procura: nenhum
/// envelope de um quadro dura mais do que isso.
int? quadrosDesdeODisparo({
  required int f,
  required int gatilho,
  int periodo = 8,
  double probabilidade = .5,
  int semente = 0,
  int alcance = 96,
}) {
  if (f < 0) return null;
  switch (gatilho) {
    case Gatilho.inicioDaCamada:
      return f;
    case Gatilho.aCadaN:
      return f % math.max(1, periodo);
    case Gatilho.aleatorio:
      final g = math.max(1, periodo);
      final c = f ~/ g;
      final celulas = (alcance / g).ceil() + 1;
      for (var k = c; k >= math.max(0, c - celulas); k--) {
        if (fxHash01(semente, 9001, k) >= probabilidade) continue;
        final dentro = (fxHash01(semente, 9002, k) * g).floor().clamp(0, g - 1);
        final disparo = k * g + dentro;
        if (disparo <= f) return f - disparo;
      }
      return null;
    default:
      return 0;
  }
}

/// ENVELOPE SEGURA-E-CAI: 1 durante [hold] quadros, depois desce ate 0 em
/// [decay] quadros com a curva [gama] (1 = reta; maior = cai mais cedo).
double envelopeHoldDecay(
  int tau, {
  required int hold,
  required int decay,
  double gama = 1,
}) {
  if (tau < 0) return 0;
  final h = math.max(1, hold);
  if (tau < h) return 1;
  if (decay <= 0) return 0;
  final x = (tau - h) / decay;
  if (x >= 1) return 0;
  return math.pow(1 - x, gama.clamp(.2, 8)).toDouble();
}

double _easeOutCubic(double x) {
  final t = 1 - x.clamp(0.0, 1.0);
  return 1 - t * t * t;
}

double _smoothstep(double a, double b, double x) {
  if (b <= a) return x >= b ? 1 : 0;
  final t = ((x - a) / (b - a)).clamp(0.0, 1.0);
  return t * t * (3 - 2 * t);
}

/// ESCALA DO ZOOM DE SOCO [tau] quadros depois do disparo.
///
/// Sobe ate [pico] (%) em [ataque] quadros, segura [hold] e volta a 100%
/// em [soltura] quadros: suave (ease out), exponencial ou com quique.
double escalaDoSoco(
  int tau, {
  required double pico,
  int ataque = 0,
  int hold = 1,
  int soltura = 6,
  int curva = 0,
  int fps = 30,
}) {
  if (tau < 0) return 1;
  final s = pico.clamp(100.0, 300.0) / 100;
  if (ataque > 0 && tau < ataque) {
    return 1 + (s - 1) * _easeOutCubic((tau + 1) / (ataque + 1));
  }
  final t2 = tau - math.max(0, ataque);
  if (t2 < hold) return s;
  final t3 = t2 - hold;
  switch (curva) {
    case 1:
      // Exponencial: um terco da soltura por meia-vida; a cauda some.
      final meiaVida = math.max(1, soltura) / 3;
      final v = math.pow(2, -(t3 + 1) / meiaVida).toDouble();
      return v < .002 ? 1 : 1 + (s - 1) * v;
    case 2:
      // Quique: cosseno amortecido (6 Hz, 12/s), cortado ao fim de duas
      // solturas.
      if (soltura > 0 && t3 >= soltura * 2) return 1;
      final segundos = (t3 + 1) / (fps < 1 ? 30 : fps);
      final v = math.exp(-12 * segundos) * math.cos(2 * math.pi * 6 * segundos);
      return 1 + (s - 1) * v;
    default:
      if (soltura <= 0 || t3 >= soltura) return 1;
      return 1 + (s - 1) * (1 - _easeOutCubic((t3 + 1) / soltura));
  }
}

/// STROBE: o quadro [f] esta "aceso"?
///
/// Periodico acende [duracao] quadros a cada [periodo]; aleatorio sorteia
/// blocos de [duracao] quadros com [probabilidade].
bool strobeAceso({
  required int f,
  required bool aleatorio,
  int periodo = 2,
  int duracao = 1,
  double probabilidade = .5,
  int semente = 0,
}) {
  if (f < 0) return false;
  final d = math.max(1, duracao);
  if (aleatorio) return fxHash01(semente, 3101, f ~/ d) < probabilidade;
  return f % math.max(1, periodo) < d;
}

/// ENVELOPE DE IMPACTO do Shake: sobe em [ataque] quadros e cai pela
/// metade a cada [meiaVida] quadros.
double envelopeDeImpacto(int tau, {double ataque = 0, double meiaVida = 6}) {
  if (tau < 0) return 0;
  final a = ataque.clamp(0.0, 60.0);
  if (a > 0 && tau < a) return _smoothstep(0, a, tau + 1);
  final h = meiaVida.clamp(.5, 240.0);
  return math.pow(2, -(tau - a) / h).toDouble();
}

/// O ZOOM DE SOCO do Shake: quanto da escala extra ainda resta [tau]
/// quadros depois da batida (1 no quadro da batida, 0 ao fim de [quadros]).
double socoDoShake(int tau, double quadros) {
  if (tau < 0) return 0;
  final n = quadros.clamp(1.0, 60.0);
  if (tau >= n) return 0;
  return 1 - _easeOutCubic(tau / n);
}

// ----------------------------------------------------------- TWITCH

/// Um pulso de um operador do Twitch: a intensidade [v] (0..1) e os
/// sorteios do evento que venceu (sinal, angulo, cor...).
class PulsoTwitch {
  const PulsoTwitch(this.v, this.sorteio3, this.sorteio4, this.sorteio7);
  final double v;

  /// Sorteios do evento em -1..1 (3 e 4) e 0..1 (7).
  final double sorteio3, sorteio4, sorteio7;

  static const nenhum = PulsoTwitch(0, 0, 0, 0);
}

/// "UM TWITCH E UM VALOR ALEATORIO NUM INSTANTE ALEATORIO."
///
/// O tempo e dividido em celulas de 1/[velocidade] segundos. Cada celula
/// dispara (ou fica quieta, com chance [quietude]) num instante sorteado,
/// preso ao quadro. O pulso dura [duracaoSeg], sobe com [easeIn] e desce
/// com [easeOut]; a forca sorteada vai de [minimo] a 1. Cada operador usa
/// um [fluxo] proprio: a luz e o deslize nao pulsam juntos.
PulsoTwitch pulsoTwitch({
  required int semente,
  required int fluxo,
  required double t,
  required double velocidade,
  required double quietude,
  required double minimo,
  required double duracaoSeg,
  double easeIn = .1,
  double easeOut = .5,
  int fps = 30,
}) {
  if (t < 0) return PulsoTwitch.nenhum;
  final taxa = fps < 1 ? 30 : fps;
  final delta = 1 / velocidade.clamp(.1, 30.0);
  final dur = math.max(duracaoSeg, 1e-3);
  // O pulso e medido no MEIO do quadro: medido no comeco, um twitch de
  // dois quadros com subida perdia o primeiro inteiro.
  final amostra = t + .5 / taxa;
  final atual = (amostra / delta).floor();
  final alcance = (dur / delta).ceil() + 1;
  var melhor = 0.0;
  int? vencedor;
  for (var j = atual - alcance; j <= atual; j++) {
    if (j < 0) continue;
    if (fxHash01(semente, fluxo, j) < quietude) continue;
    final bruto = (j + fxHash01(semente, fluxo + 1, j)) * delta;
    final inicio = (bruto * taxa).floor() / taxa;
    final tau = (amostra - inicio) / dur;
    if (tau < 0 || tau >= 1) continue;
    final forca = minimo + (1 - minimo) * fxHash01(semente, fluxo + 2, j);
    final sobe = easeIn <= 1e-6 ? 1.0 : _smoothstep(0, easeIn, tau);
    final desce = easeOut <= 1e-6 ? 1.0 : _smoothstep(0, easeOut, 1 - tau);
    final v = math.min(sobe, desce) * forca;
    if (v > melhor) {
      melhor = v;
      vencedor = j;
    }
  }
  if (vencedor == null || melhor <= 0) return PulsoTwitch.nenhum;
  return PulsoTwitch(
    melhor.clamp(0.0, 1.0),
    fxHash01(semente, fluxo + 3, vencedor) * 2 - 1,
    fxHash01(semente, fluxo + 4, vencedor) * 2 - 1,
    fxHash01(semente, fluxo + 7, vencedor),
  );
}

// ------------------------------------------------------ MATRIZES

/// Matriz de cor do Flash: [modo] 0 normal, 1 somar, 2 tela, 3 exposicao,
/// 4 negativo; [k] e a forca ja multiplicada pelo envelope.
List<double> matrizDoFlash(
  int modo,
  double k, {
  required double r,
  required double g,
  required double b,
  double stops = 0,
}) {
  final f = k.clamp(0.0, 1.0);
  List<double> diagonal(double gr, double gg, double gb, double or, double og, double ob) => [
    gr, 0, 0, 0, or, //
    0, gg, 0, 0, og,
    0, 0, gb, 0, ob,
    0, 0, 0, 1, 0,
  ];
  switch (modo) {
    case 1:
      return diagonal(1, 1, 1, r * f * 255, g * f * 255, b * f * 255);
    case 2:
      return diagonal(
        1 - r * f,
        1 - g * f,
        1 - b * f,
        r * f * 255,
        g * f * 255,
        b * f * 255,
      );
    case 3:
      final ganho = math.pow(2, stops.clamp(0.0, 8.0)).toDouble();
      return diagonal(ganho, ganho, ganho, 0, 0, 0);
    case 4:
      return diagonal(1 - 2 * f, 1 - 2 * f, 1 - 2 * f, f * 255, f * 255, f * 255);
    default:
      return diagonal(1 - f, 1 - f, 1 - f, r * f * 255, g * f * 255, b * f * 255);
  }
}

/// Colorir preservando a luminancia (a conta linear do tingir): mistura
/// [k] da cor original com a luminancia dela na cor [r],[g],[b].
List<double> matrizDeColorir(double k, {required double r, required double g, required double b}) {
  final f = k.clamp(0.0, 1.0);
  final y = math.max(.2126 * r + .7152 * g + .0722 * b, 1e-4);
  final alvo = [r / y, g / y, b / y];
  const luma = [.2126, .7152, .0722];
  return [
    for (var i = 0; i < 3; i++) ...[
      for (var j = 0; j < 3; j++) (i == j ? 1 - f : 0.0) + f * alvo[i] * luma[j],
      0,
      0,
    ],
    0, 0, 0, 1, 0,
  ];
}

/// Cor pura de um matiz (0..1), saturacao e brilho maximos.
({double r, double g, double b}) corDoMatiz(double h) {
  double rampa(double n) => (((h * 6 + n) % 6 - 3).abs() - 1).clamp(0.0, 1.0);
  return (r: rampa(0), g: rampa(4), b: rampa(2));
}
