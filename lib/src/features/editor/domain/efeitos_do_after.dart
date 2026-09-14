import 'dart:math' as math;

import 'coloring.dart' show Rgb, hslToRgb, rgbToHsl;
import 'fx.dart' show fxNoiseSigned, hueRotateMatrix;

/// A CAMADA DE AJUSTE DO AFTER — as contas de referencia em CPU.
///
/// "Adicione esses efeitos" (dono, 14/09/2026): um edit de referencia no
/// After Effects tinha, numa camada de ajuste, Magic Bullet Looks,
/// S_Sharpen, S_Flicker, S_MathOps, S_FilmDamage, Hue/Saturation e
/// Brightness & Contrast.
///
/// O desenho roda no shader (`shaders/effects_v2.frag`, modos 45 em
/// diante). Estas funcoes sao a MESMA conta em Dart e servem a duas
/// coisas: o teste compara o shader com elas pixel a pixel, e o caminho
/// sem shader usa a parte linear delas como matriz de cor.
///
/// Todas recebem e devolvem RGB nao pre-multiplicado em 0..1. As contas
/// dos plugins nao sao publicadas: os parametros seguem a documentacao
/// da Adobe e da Boris FX, e a matematica e a nossa, escrita aqui.

double _lum(Rgb c) => c.r * .2126 + c.g * .7152 + c.b * .0722;

// ------------------------------------------------------ MATRIZES 4x5

/// A matriz que aplica [antes] e depois [depois], no formato do
/// `ColorFilter.matrix` (quatro linhas de cinco, deslocamento em 0..255).
List<double> comporMatrizes(List<double> depois, List<double> antes) {
  final saida = List<double>.filled(20, 0);
  for (var i = 0; i < 4; i++) {
    for (var j = 0; j < 4; j++) {
      var soma = 0.0;
      for (var k = 0; k < 4; k++) {
        soma += depois[i * 5 + k] * antes[k * 5 + j];
      }
      saida[i * 5 + j] = soma;
    }
    var deslocamento = depois[i * 5 + 4];
    for (var k = 0; k < 4; k++) {
      deslocamento += depois[i * 5 + k] * antes[k * 5 + 4];
    }
    saida[i * 5 + 4] = deslocamento;
  }
  return saida;
}

/// Saturacao em volta da luma Rec.709 (1 = identidade).
List<double> _matrizDeSaturacao(double s) {
  const lr = .2126, lg = .7152, lb = .0722;
  return [
    lr * (1 - s) + s, lg * (1 - s), lb * (1 - s), 0, 0, //
    lr * (1 - s), lg * (1 - s) + s, lb * (1 - s), 0, 0,
    lr * (1 - s), lg * (1 - s), lb * (1 - s) + s, 0, 0,
    0, 0, 0, 1, 0,
  ];
}

/// Mistura com o branco (positivo) ou com o preto (negativo), -1..1.
List<double> _matrizDeClarear(double claro) {
  final k = claro >= 0 ? 1 - claro : 1 + claro;
  final d = claro >= 0 ? claro * 255 : 0.0;
  return [
    k, 0, 0, 0, d, //
    0, k, 0, 0, d,
    0, 0, k, 0, d,
    0, 0, 0, 1, 0,
  ];
}

// -------------------------------------------------- HUE/SATURATION

/// Hue/Saturation com os numeros da ficha: matiz em graus (-180..180),
/// saturacao e luminosidade em -100..100, matiz ao colorir em 0..360 e
/// saturacao ao colorir em 0..100.
///
/// A saturacao escala o CROMA em volta da luminosidade do HSL: o cinza
/// continua cinza, e o ganho positivo para quando o pixel chega a
/// saturacao cheia (e o comportamento do Photoshop). No shader: modo 45.
Rgb hueSaturation(
  Rgb c, {
  double matiz = 0,
  double saturacao = 0,
  double luminosidade = 0,
  bool colorir = false,
  double matizColorir = 0,
  double saturacaoColorir = 25,
}) {
  final claro = (luminosidade / 100).clamp(-1.0, 1.0);
  if (colorir) {
    var y = _lum(c).clamp(0.0, 1.0);
    y = claro >= 0 ? y + (1 - y) * claro : y * (1 + claro);
    return hslToRgb(
      matizColorir / 360,
      (saturacaoColorir / 100).clamp(0.0, 1.0),
      y,
    );
  }
  var x = c;
  if (matiz.abs() > 1e-4) {
    final hsl = rgbToHsl(x);
    final h = hsl.h + matiz / 360;
    x = hslToRgb(h - h.floorToDouble(), hsl.s, hsl.l);
  }
  final sat = (saturacao / 100).clamp(-1.0, 1.0);
  if (sat.abs() > 1e-5) {
    final mx = math.max(x.r, math.max(x.g, x.b));
    final mn = math.min(x.r, math.min(x.g, x.b));
    final l = (mx + mn) / 2, d = mx - mn;
    final s = d < 1e-5
        ? 0.0
        : (l > .5
              ? d / math.max(2 - mx - mn, 1e-5)
              : d / math.max(mx + mn, 1e-5));
    final k = sat < 0 ? sat : 1 / math.max(math.max(1 - sat, s), 1e-4) - 1;
    x = (
      r: x.r + (x.r - l) * k,
      g: x.g + (x.g - l) * k,
      b: x.b + (x.b - l) * k,
    );
  }
  if (claro.abs() > 1e-5) {
    x = claro >= 0
        ? (
            r: x.r + (1 - x.r) * claro,
            g: x.g + (1 - x.g) * claro,
            b: x.b + (1 - x.b) * claro,
          )
        : (r: x.r * (1 + claro), g: x.g * (1 + claro), b: x.b * (1 + claro));
  }
  return x;
}

/// Hue/Saturation como matriz, para o caminho sem shader. APROXIMADA: a
/// matiz gira pela matriz classica que preserva a luminancia, a
/// saturacao vira um ganho em volta da luma (o de um pixel de saturacao
/// media) e o colorir sai exato no preto e no meio-tom, e tingido nas
/// luzes. A luminosidade e exata.
List<double> hueSaturationMatrix({
  double matiz = 0,
  double saturacao = 0,
  double luminosidade = 0,
  bool colorir = false,
  double matizColorir = 0,
  double saturacaoColorir = 25,
}) {
  final claro = (luminosidade / 100).clamp(-1.0, 1.0);
  List<double> cor;
  if (colorir) {
    // A rampa de cada canal na matiz pedida (o HSL com S 1 e L 1/2).
    final k = hslToRgb((matizColorir % 360) / 360, 1, .5);
    final s = (saturacaoColorir / 100).clamp(0.0, 1.0);
    List<double> linha(double rampa) {
      final g = 1 + s * (2 * rampa - 1);
      return [.2126 * g, .7152 * g, .0722 * g, 0, 0];
    }

    cor = [...linha(k.r), ...linha(k.g), ...linha(k.b), 0, 0, 0, 1, 0];
  } else {
    final sat = (saturacao / 100).clamp(-1.0, 1.0);
    final ganho = sat < 0 ? 1 + sat : 1 / math.max(1 - sat, .5);
    cor = comporMatrizes(_matrizDeSaturacao(ganho), hueRotateMatrix(matiz));
  }
  return comporMatrizes(_matrizDeClarear(claro), cor);
}

// ------------------------------------------------------------ S_FLICKER

/// O GANHO RGB DO S_FLICKER num instante.
///
/// As fases chegam JA INTEGRADAS, em ciclos: [faseAleatoria] e a integral
/// da Frequencia aleatoria no tempo e [faseDaOnda], a da Frequencia da
/// onda. E o que o Shake faz: animar a frequencia acelera sem tranco, em
/// vez de a fase saltar a cada keyframe.
///
///     ganho do canal = brilho * (1 + amplitude * forca do canal * (
///         brilhoAleatorio * ruido comum
///       + corAleatoria    * ruido do canal
///       + amplitudeDaOnda * seno(2pi * (faseDaOnda + fase do canal))))
///
/// Os ruidos sao suaves (-1..1) e so dependem de (fase, semente): o mesmo
/// quadro sai igual na previa, no scrub e na exportacao, e nada acumula
/// estado entre quadros. O ganho nunca e negativo.
Rgb ganhoDoSFlicker({
  required double faseAleatoria,
  required double faseDaOnda,
  double amplitude = .2,
  double brilhoAleatorio = 1,
  double corAleatoria = 0,
  double amplitudeDaOnda = 0,
  double faseR = 0,
  double faseG = 0,
  double faseB = 0,
  double forcaR = 1,
  double forcaG = 1,
  double forcaB = 1,
  double brilho = 1,
  int semente = 0,
}) {
  final comum = brilhoAleatorio == 0
      ? 0.0
      : fxNoiseSigned(semente, 0, faseAleatoria);
  double canal(int i, double faseGraus, double forca) {
    final ruido = corAleatoria == 0
        ? 0.0
        : fxNoiseSigned(semente, 1 + i, faseAleatoria);
    final onda = amplitudeDaOnda == 0
        ? 0.0
        : math.sin(2 * math.pi * (faseDaOnda + faseGraus / 360));
    final pisca =
        brilhoAleatorio * comum + corAleatoria * ruido + amplitudeDaOnda * onda;
    final ganho = brilho * (1 + amplitude * forca * pisca);
    // Um numero invalido vindo do arquivo nao pode apagar a camada.
    return ganho.isFinite ? math.max(0.0, ganho) : 1.0;
  }

  return (
    r: canal(0, faseR, forcaR),
    g: canal(1, faseG, forcaG),
    b: canal(2, faseB, forcaB),
  );
}

// ------------------------------------------------------------ S_MATHOPS

/// Luzes escala, sombras desloca os escuros e a saturacao gira em volta da
/// luma Rec.709: c*luzes + sombras*(1-c), depois a saturacao. Luzes 1,
/// sombras 0 e saturacao 1 devolvem a cor intacta.
Rgb luzesSombrasSaturacao(
  Rgb c, {
  double luzes = 1,
  double sombras = 0,
  double saturacao = 1,
}) {
  double canal(double v) => v * luzes + sombras * (1 - v);
  final x = (r: canal(c.r), g: canal(c.g), b: canal(c.b));
  final y = _lum(x);
  // A mesma forma do mix() do shader: y*(1-s) + x*s.
  double sat(double v) => y * (1 - saturacao) + v * saturacao;
  return (r: sat(x.r), g: sat(x.g), b: sat(x.b));
}

/// As nove operacoes, na ordem da ficha: somar, subtrair, multiplicar,
/// tela, media, sobrepor, minimo, maximo e diferenca.
double operacaoMath(int operacao, double a, double b) => switch (operacao) {
  1 => a - b,
  2 => a * b,
  3 => 1 - (1 - a) * (1 - b),
  4 => (a + b) * .5,
  5 => a <= .5 ? 2 * a * b : 1 - 2 * (1 - a) * (1 - b),
  6 => math.min(a, b),
  7 => math.max(a, b),
  8 => (a - b).abs(),
  _ => a + b,
};

/// S_MathOps num pixel. [fonteB]: 0 preto, 1 a propria camada, 2 a camada
/// desfocada — cuja cor neste pixel chega em [bDesfocada]. A mascara de
/// luma le [mascaraDesfocada] (a camada desfocada pelo Desfoque da
/// mascara). Sem desfoque, as duas sao a propria [c]. Nao limita os
/// passos do meio: quem limita a 0..1 e a saida, como no shader (modo 46).
Rgb mathOps(
  Rgb c, {
  int operacao = 0,
  int fonteB = 0,
  Rgb? bDesfocada,
  double luzesA = 1,
  double sombrasA = 0,
  double saturacaoA = 1,
  double luzesB = 1,
  double sombrasB = 0,
  double saturacaoB = 1,
  double luzesDestino = 1,
  double sombrasDestino = 0,
  double saturacaoDestino = 1,
  bool mascaraDeLuma = false,
  Rgb? mascaraDesfocada,
  bool inverterMascara = false,
}) {
  final a = luzesSombrasSaturacao(
    c,
    luzes: luzesA,
    sombras: sombrasA,
    saturacao: saturacaoA,
  );
  final Rgb semB = (r: 0.0, g: 0.0, b: 0.0);
  final b = luzesSombrasSaturacao(
    switch (fonteB) {
      1 => c,
      2 => bDesfocada ?? c,
      _ => semB,
    },
    luzes: luzesB,
    sombras: sombrasB,
    saturacao: saturacaoB,
  );
  final destino = luzesSombrasSaturacao(
    (
      r: operacaoMath(operacao, a.r, b.r),
      g: operacaoMath(operacao, a.g, b.g),
      b: operacaoMath(operacao, a.b, b.b),
    ),
    luzes: luzesDestino,
    sombras: sombrasDestino,
    saturacao: saturacaoDestino,
  );
  var m = 1.0;
  if (mascaraDeLuma) {
    m = _lum(mascaraDesfocada ?? c).clamp(0.0, 1.0);
    if (inverterMascara) m = 1 - m;
  }
  return (
    r: c.r * (1 - m) + destino.r * m,
    g: c.g * (1 - m) + destino.g * m,
    b: c.b * (1 - m) + destino.b * m,
  );
}

// ------------------------------------------------------ S_FILMDAMAGE 2

/// A COR DA COPIA do Film Damage: saturacao em volta da luma Rec.709 e o
/// tom sepia classico misturado por [sepia]. As duas contas sao lineares,
/// entao a matriz de [matrizDaCopiaDeFilme] e exata. No shader: modo 30.
Rgb corDaCopiaDeFilme(Rgb c, {double saturacao = 1, double sepia = 0}) {
  final y = _lum(c);
  final s = (
    r: y * (1 - saturacao) + c.r * saturacao,
    g: y * (1 - saturacao) + c.g * saturacao,
    b: y * (1 - saturacao) + c.b * saturacao,
  );
  final t = (
    r: .393 * s.r + .769 * s.g + .189 * s.b,
    g: .349 * s.r + .686 * s.g + .168 * s.b,
    b: .272 * s.r + .534 * s.g + .131 * s.b,
  );
  return (
    r: s.r * (1 - sepia) + t.r * sepia,
    g: s.g * (1 - sepia) + t.g * sepia,
    b: s.b * (1 - sepia) + t.b * sepia,
  );
}

/// [corDaCopiaDeFilme] como matriz de cor, para o caminho sem shader.
List<double> matrizDaCopiaDeFilme({double saturacao = 1, double sepia = 0}) {
  final k = sepia.clamp(0.0, 1.0);
  final tom = [
    1 - k + .393 * k, .769 * k, .189 * k, 0.0, 0.0, //
    .349 * k, 1 - k + .686 * k, .168 * k, 0.0, 0.0,
    .272 * k, .534 * k, 1 - k + .131 * k, 0.0, 0.0,
    0.0, 0.0, 0.0, 1.0, 0.0,
  ];
  return comporMatrizes(tom, _matrizDeSaturacao(saturacao));
}

/// A matriz de cor de um ganho por canal; o alfa nao muda.
List<double> matrizDeGanho(Rgb ganho) => [
  ganho.r, 0, 0, 0, 0, //
  0, ganho.g, 0, 0, 0,
  0, 0, ganho.b, 0, 0,
  0, 0, 0, 1, 0,
];
