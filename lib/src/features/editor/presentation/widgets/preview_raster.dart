import 'dart:math' as math;

/// RESOLUCAO DAS FOTOS DO PREVIEW.
///
/// Todo efeito que precisa de imagem (FxSnapshot, mescla customizada,
/// dithering) fotografa a composicao em pixels = tamanho logico x razao
/// de pixels. A composicao tem o tamanho de SAIDA (1080 x 1920) e o
/// aparelho tem DPR 3: a foto saia com 3240 x 5760 — 75 MB — por efeito,
/// por quadro, para ser desenhada num preview de 390 pontos de largura.
/// No iPhone isso e o app fechando "do nada" na primeira animacao: o
/// iOS mata quem aloca centenas de MB por segundo.
///
/// A razao certa e a da TELA: o preview mostra a composicao escalada por
/// [stageScale], entao a foto so precisa ter stageScale x dpr pixels por
/// pixel logico. Mesma nitidez na tela, 9x menos memoria no DPR 3.
/// E um teto absoluto por seguranca: nenhuma foto passa de
/// [maxSidePx] no lado maior, aconteca o que acontecer com a tela.
double previewRasterRatio({
  required double compWidth,
  required double compHeight,
  required double stageScale,
  required double devicePixelRatio,
  double maxSidePx = 2160,
}) {
  final natural = stageScale * devicePixelRatio;
  final maior = math.max(compWidth, compHeight);
  final teto = maior <= 0 ? natural : maxSidePx / maior;
  final razao = math.min(natural, teto);
  // Nunca abaixo de um quarto: uma foto de 4 px nao e efeito, e ruido.
  return razao.isFinite && razao > 0.25 ? razao : 0.25;
}
