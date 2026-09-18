/// QUANTAS AMOSTRAS O BRILHO PAGA, POR TAMANHO DE TRABALHO.
///
/// O kernel do Glow e o mais caro do app, e era o unico sem freio. Cada
/// amostra e um bilinear de QUATRO leituras de textura — o
/// `ImageFilter.shader` entrega a entrada sem interpolar, entao o bilinear
/// e na mao. Com 64 amostras sao 256 leituras por pixel; com 96, 384.
///
/// Numa previa 1080x1920 a 3x sao 18,6 milhoes de pixels: 4,7 BILHOES de
/// leituras por quadro. Nao e erro — e o app parado esperando a GPU, que
/// foi o "travou" do relato.
///
/// A fracao vale contra o maximo de CADA kernel (`luz.frag`): o Brilho e o
/// Deep Glow tem 64, o S_GlowAura e o S_GlowDarks tem 96, o S_Glint tem 24
/// por braco. Todos normalizam por n, entao menos amostras NAO clareiam
/// nem escurecem nada — muda so o quao liso o halo sai.
abstract final class AmostrasDoBrilho {
  /// Tocando: o quadro tem de sair em milissegundos. O halo fica mais
  /// cru, e o aviso de "Rascunho" na tela diz que a qualidade final e
  /// outra.
  static const double rascunho = 0.1875; // 12 de 64 · 18 de 96

  /// Parado, editando: o valor que a pessoa olha enquanto ajusta. Vale
  /// para o painel E para o palco, senao o numero que ela escolhe nao
  /// corresponde ao que ela ve.
  static const double previa = 0.4375; // 28 de 64 · 42 de 96

  /// Exportando: o maximo do kernel, sem desconto.
  static const double exportacao = 1;

  /// O valor de um quadro, dado o que ele e.
  static double para({
    required bool exportando,
    required bool tocando,
  }) => exportando
      ? exportacao
      : (tocando ? rascunho : previa);
}
