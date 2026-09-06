/// ESPACO DE COR — a conta de luz precisa do espaco LINEAR.
///
/// O valor guardado num PNG nao e "quanta luz tem": e um numero
/// corrigido para o olho, com a curva do sRGB por cima. Somar, borrar ou
/// misturar esses numeros direto e somar as coisas erradas — glow
/// acinzentado, desfoque com halo escuro, dissolve escurecendo no meio,
/// faixa em gradiente.
///
/// A mesma matematica do `shaders/gamma.frag` vive aqui em Dart: e o que
/// os testes prendem, e o que o codigo de exportacao usa quando precisa
/// da conta na CPU.
library;

import 'dart:math' as math;

/// sRGB (0..1) -> linear (0..1).
double srgbToLinear(double c) {
  final v = c.clamp(0.0, 1.0);
  return v <= 0.04045 ? v / 12.92 : math.pow((v + 0.055) / 1.055, 2.4) as double;
}

/// linear (0..1) -> sRGB (0..1).
double linearToSrgb(double c) {
  final v = c.clamp(0.0, 1.0);
  return v <= 0.0031308
      ? v * 12.92
      : 1.055 * (math.pow(v, 1 / 2.4) as double) - 0.055;
}

/// Luminancia em espaco LINEAR (Rec. 709).
///
/// Calcular luminancia em sRGB e o erro que faz o amarelo parecer mais
/// escuro do que e — os pesos so valem depois de desfazer a curva.
double linearLuminance(double r, double g, double b) =>
    0.2126 * srgbToLinear(r) +
    0.7152 * srgbToLinear(g) +
    0.0722 * srgbToLinear(b);

/// UNIDADES DOS PARAMETROS.
///
/// Metade da divergencia com o Alight Motion mora aqui e nao se descobre
/// olhando o resultado: raio "20" em pixel absoluto num projeto 4K e um
/// quarto do raio "20" num projeto 1080p. Aqui o raio e sempre uma
/// FRACAO do menor lado da composicao, e esta funcao e a unica conversao
/// para pixel.
///
/// Referencia: 1080p de altura. Um raio de 0,1 vale 108 px em 1080p, e
/// 216 px em 4K — o mesmo tamanho APARENTE, que e o que a pessoa espera
/// ao trocar a resolucao do projeto.
double radiusToPixels(double fraction, int compWidth, int compHeight) {
  final menorLado = math.min(compWidth, compHeight).toDouble();
  return fraction.clamp(0.0, 4.0) * menorLado;
}

/// UM VALOR PENSADO EM 1080p, convertido para esta composicao.
///
/// Alguns controles nasceram medidos em pixel — amplitude de tremor,
/// deslocamento de canal. Trocar a unidade quebraria todo projeto ja
/// salvo, e nao e preciso: basta dizer em que resolucao aquele numero
/// foi pensado. Em 1080p o resultado e exatamente o de antes; em 4K
/// dobra, que e o mesmo tamanho APARENTE.
double pxAt1080(double px, int compWidth, int compHeight) =>
    radiusToPixels(px / 1080.0, compWidth, compHeight);

/// O caminho de volta, para a interface mostrar o numero em pixel.
double pixelsToRadius(double pixels, int compWidth, int compHeight) {
  final menorLado = math.min(compWidth, compHeight).toDouble();
  return menorLado <= 0 ? 0 : pixels / menorLado;
}
