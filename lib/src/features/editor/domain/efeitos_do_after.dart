import 'dart:math' as math;

import 'coloring.dart' show Rgb, hslToRgb, rgbToHsl;
import 'fx.dart' show hueRotateMatrix;

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
