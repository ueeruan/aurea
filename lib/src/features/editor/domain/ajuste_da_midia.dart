import 'dart:ui' show Size;

/// COMO A FOTO OU O VIDEO OCUPA A COMPOSICAO NA ESCALA 100%.
///
/// A midia sempre nasceu "pela largura": largura da composicao e altura
/// pela proporcao do arquivo. Numa composicao vertical, um video deitado
/// ficava com faixas pretas em cima e embaixo, e o testador ampliava na
/// mao ate 198,6% sem nunca chegar a cobrir (em 9:16, cobrir um 16:9 pede
/// 316%). O pedido foi: "o app tem que importar qualquer coisa e no
/// preview ficar a tela cheia de acordo com a resolucao".
enum AjusteDaMidia {
  /// O de antes: largura da composicao, altura pela proporcao. Projeto
  /// antigo abre assim e nada muda de lugar.
  largura,

  /// Cobre a composicao inteira, sem faixa: o que passa da borda fica
  /// fora do quadro (e volta a aparecer se a camada for reduzida).
  cobrir,

  /// Cabe inteira dentro da composicao: sobra faixa onde a proporcao do
  /// arquivo nao bate com a da composicao.
  conter,
}

/// A proporcao (largura / altura) ou nulo quando nao serve para conta.
double? proporcaoValida(double? proporcao) =>
    proporcao != null && proporcao.isFinite && proporcao > 0 ? proporcao : null;

/// A CAIXA DA MIDIA NA ESCALA 100%, em px da composicao.
///
/// [proporcao] e a do quadro como ele e EXIBIDO (ja girado pelo metadado
/// de rotacao), nunca a largura/altura crua do arquivo. Sem proporcao
/// conhecida: pela largura com 16:9 (o palpite de sempre) e, para cobrir
/// ou conter, a propria composicao.
Size caixaDaMidia(
  Size composicao,
  double? proporcao,
  AjusteDaMidia ajuste,
) {
  final w = composicao.width;
  final h = composicao.height;
  final a = proporcaoValida(proporcao);
  if (a == null) {
    return ajuste == AjusteDaMidia.largura || h <= 0
        ? Size(w, w * 9 / 16)
        : Size(w, h);
  }
  if (ajuste == AjusteDaMidia.largura || h <= 0) return Size(w, w / a);
  final maisLarga = a > w / h;
  return switch (ajuste) {
    AjusteDaMidia.cobrir =>
      maisLarga ? Size(h * a, h) : Size(w, w / a),
    AjusteDaMidia.conter =>
      maisLarga ? Size(w, w / a) : Size(h * a, h),
    AjusteDaMidia.largura => Size(w, w / a),
  };
}

/// A PROPORCAO DO VIDEO COMO O TOCADOR MOSTRA.
///
/// O tocador entrega o tamanho do quadro e, quando o sistema nao girou
/// o video sozinho, uma correcao de rotacao que ele aplica com um
/// RotatedBox. Com um quarto de volta impar, o quadro exibido e o
/// tamanho TROCADO: um video em pe gravado deitado. Sem proporcao
/// valida, nulo (quem chama usa a guardada ou o palpite).
double? proporcaoExibidaDoVideo(double proporcao, int correcaoDeRotacao) {
  final a = proporcaoValida(proporcao);
  if (a == null) return null;
  return (correcaoDeRotacao ~/ 90).isOdd ? 1 / a : a;
}

/// AS PROPORCOES DE PROJETO que a midia "quase" tem viram a exata: um
/// video 1920x1088 e 16:9, e a composicao sai com medidas redondas.
const _proporcoesComuns = <double>[16 / 9, 9 / 16, 1, 4 / 5, 5 / 4, 4 / 3, 3 / 4];

/// A PROPORCAO DO PROJETO NOVO criado a partir de uma midia: a da propria
/// midia, para a previa ficar cheia (antes o atalho Midia criava sempre
/// 9:16, e um video deitado abria com faixa preta). Encosta nas comuns a
/// ate 3% e fica entre 9:21 e 21:9. Sem medida, 9:16 como sempre foi.
double proporcaoDoProjeto(double? proporcaoDaMidia) {
  final a = proporcaoValida(proporcaoDaMidia);
  if (a == null) return 9 / 16;
  for (final c in _proporcoesComuns) {
    if ((a / c - 1).abs() <= 0.03) return c;
  }
  return a.clamp(9 / 21, 21 / 9);
}
