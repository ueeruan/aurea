import 'dart:ui';

/// A PALETA DO EDITOR.
///
/// OS NEUTROS SAO NOSSOS (16/09). Ate hoje eles vinham MEDIDOS de uma
/// gravacao do app de referencia — o comentario que ficava aqui dizia
/// isso com todas as letras. Medir estrutura, medida e comportamento e
/// aprender; herdar a cor do cromo e herdar identidade, e foi o que o
/// dono viu ao dizer que estava "muito parecido".
///
/// A escala agora e da Aurea e mais FUNDA: o cromo desceu cerca de um
/// terco em luminancia, o que aumenta a distancia entre a ferramenta
/// (quase preta) e a composicao (o unico conteudo colorido da tela).
/// Os degraus entre os tons continuam pequenos de proposito — o que
/// tem de saltar e o trabalho, nao o painel.
///
/// AS CORES DE MARCA SEGUEM AS DA AUREA: o lima da logo ocupa o lugar
/// da acao, o violeta marca selecao.
abstract final class AmColors {
  /// O FUNDO ATRAS DA COMPOSICAO. Mais escuro que o cromo, para o quadro
  /// do projeto se destacar do que e ferramenta.
  static const Color bg = Color(0xFF08080C);

  /// O CROMO: cabecalho, transporte, linha do tempo. Tudo que e
  /// ferramenta usa este tom, e por isso os tres blocos parecem uma peca
  /// so, que e o que eles sao.
  static const Color topBar = Color(0xFF0E0E13);
  static const Color panel = Color(0xFF0E0E13);
  static const Color panelHigh = Color(0xFF15151D);

  /// A CAPSULA DO TEMPO e os chips em geral.
  static const Color chip = Color(0xFF1A1A24);

  /// A PILULA DA CAMADA: o olho e a cor, flutuando sobre a trilha.
  static const Color pilula = Color(0xFF1A1A28);

  /// A CAIXA DE VALOR e os chips do painel de transformacao. Medido em
  /// #242436 (`docs/painel-de-transformacao-alight.md`, secao "Cores").
  ///
  /// SEIS PONTOS DE AZUL ACIMA DE [chip], e nao um descuido de copiar e
  /// colar: [chip] foi medido na capsula do tempo, noutra tela e noutra
  /// gravacao. Sao dois tons quase iguais porque a referencia tem dois
  /// tons quase iguais — fundi-los num so pouparia uma constante hoje e
  /// faria a proxima medida discordar do codigo sem ninguem saber qual
  /// das duas telas estava errada.
  static const Color campo = Color(0xFF1A1A28);

  /// Keyframe, curva e realce de contexto (teal).
  static const Color accent = Color(0xFF1ED6B1);
  static const Color accentDim = Color(0xFF183F3C);

  /// ACAO (o lima da logo): Exportar, o "+", chips de acao. O teal fica
  /// com keyframe e curva; a acao e outra cor para nao se confundir com
  /// "esta animado".
  static const Color action = Color(0xFFB8FF3D);
  static const Color onAction = Color(0xFF0B0E12);
  static const Color actionDim = Color(0xFF2A3A16);

  /// Selecao e grupos (violeta da logo).
  static const Color selection = Color(0xFF7C62FF);

  /// Barras de camada com contraste para texto e keyframes.
  static const Color teal = Color(0xFF43B7C6);
  static const Color tealBright = Color(0xFF81D8E0);

  /// Marca de keyframe na regua.
  static const Color pink = Color(0xFFFF6B6B);

  /// O CABECOTE E BRANCO, como na referencia. Ele cruza trilhas de todas
  /// as cores: qualquer cor propria brigaria com alguma delas, e branco
  /// puro nao e usado em mais nada grande na tela.
  static const Color cabecote = Color(0xFFFFFFFF);

  static const Color text = Color(0xFFE9EDF2);
  static const Color muted = Color(0xFF8B94A3);
  static const Color hairline = Color(0x14FFFFFF);
}
