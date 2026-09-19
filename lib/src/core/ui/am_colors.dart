import 'dart:ui';

import '../theme/aurea_colors.dart';

/// A PALETA DO EDITOR — o cromo do editor e do estudio.
///
/// ==========================================================================
/// HAVIA DOIS [AmColors] NO PROJETO, E ELES DISCORDAVAM.
/// ==========================================================================
///
/// Este arquivo e o irmao em `features/editor/presentation/am/am_colors.dart`
/// declaravam a MESMA classe com valores diferentes para `bg`, `topBar`,
/// `panel`, `panelHigh` e `chip` — o fundo do editor era #08080C num caminho
/// de import e #12151A no outro. Quem importasse um ou outro pintava um tom
/// diferente para o mesmo papel, e nenhum dos dois sabia do terceiro.
///
/// Agora existe um dono so: ESTE. O outro virou `export` deste, entao os
/// quarenta arquivos que escrevem `import 'am_colors.dart'` continuam certos
/// sem tocar em uma linha.
///
/// ==========================================================================
/// A MARCA MUDOU DE VERDE PARA AZUL (18/09/2026)
/// ==========================================================================
///
/// Os neutros ja eram nossos (16/09): antes vinham MEDIDOS de uma gravacao
/// do app de referencia, e medir estrutura e aprender enquanto herdar a cor e
/// herdar identidade. A escala continua funda — o cromo fica bem abaixo da
/// composicao em luminancia, e o que tem de saltar e o trabalho, nao o
/// painel.
///
/// O que mudou foi a FAMILIA: de grafite quase neutro para o azul da marca,
/// no mesmo degrau. Os papeis de marca (acao, keyframe, selecao) agora saem
/// de [AureaColors], e nao ha hexadecimal escrito aqui.
///
/// OS CAMPOS SAO `static const` E NAO GETTERS, de proposito: `CromoEditor`
/// escreve `static const Color acao = AmColors.action;`, e um getter
/// quebraria a compilacao de todo o cromo.
abstract final class AmColors {
  /// O FUNDO ATRAS DA COMPOSICAO. Mais escuro que o cromo, para o quadro
  /// do projeto se destacar do que e ferramenta.
  static const Color bg = AureaColors.stage;

  /// O CROMO: cabecalho, transporte, linha do tempo. Tudo que e ferramenta
  /// usa este tom, e por isso os tres blocos parecem uma peca so, que e o
  /// que eles sao.
  static const Color topBar = AureaColors.chrome;
  static const Color panel = AureaColors.chrome;
  static const Color panelHigh = AureaColors.chromeHigh;

  /// A CAPSULA DO TEMPO e os chips em geral.
  static const Color chip = AureaColors.chip;

  /// A PILULA DA CAMADA: o olho e a cor, flutuando sobre a trilha.
  static const Color pilula = AureaColors.pill;

  /// A CAIXA DE VALOR e os chips do painel de transformacao.
  static const Color campo = AureaColors.field;

  /// Keyframe, curva e realce de contexto: o que esta LIGADO na tela.
  static const Color accent = AureaColors.accent;
  static const Color accentDim = AureaColors.accentDim;

  /// ACAO: Exportar, o "+", chips de acao.
  ///
  /// E O AZUL FUNDO, e nao o claro, porque este e um PREENCHIMENTO cheio com
  /// texto claro por cima (#F7F9FB sobre #245D8C da 6,8:1). O realce do que
  /// esta ligado usa o claro, que e um TRACO fino sobre o fundo escuro e
  /// precisa de 7,8:1 contra ele. Um so azul nao serve aos dois: o que se ve
  /// sobre o preto e claro demais para levar texto em cima.
  static const Color action = AureaColors.brand;
  static const Color onAction = AureaColors.text;
  static const Color actionDim = Color(0xFF16304A);

  /// Selecao e grupos: o fundo da camada escolhida e o chip de grupo.
  static const Color selection = AureaColors.brandDeep;

  /// Selecao em TEXTO e em TRACO, onde [selection] seria escuro demais.
  static const Color selectionText = AureaColors.selectionText;

  /// Barras de camada com contraste para texto e keyframes.
  static const Color teal = AureaColors.brandLight;
  static const Color tealBright = AureaColors.brandSoft;

  /// Excluir, erro, e a marca de alerta na regua.
  static const Color pink = AureaColors.danger;

  /// O CABECOTE E BRANCO, como na referencia. Ele cruza trilhas de todas
  /// as cores: qualquer cor propria brigaria com alguma delas, e branco
  /// puro nao e usado em mais nada grande na tela.
  static const Color cabecote = AureaColors.playhead;

  static const Color text = AureaColors.text;
  static const Color muted = AureaColors.muted;
  static const Color hairline = AureaColors.border;
}
