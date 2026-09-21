import 'dart:ui';

import '../theme/aurea_colors.dart';
import '../theme/aurea_paleta.dart';

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
/// ==========================================================================
/// OS CAMPOS VIRARAM GETTERS (20/09/2026) — E O QUE ISSO CUSTOU
/// ==========================================================================
///
/// Eram `static const`. Cor `const` se resolve na compilacao, entao o editor
/// nascia preso a UMA paleta: com seis temas em Ajustes, o app inteiro
/// trocava de cor e o editor ficava no tom antigo.
///
/// Agora cada papel le [AureaPaleta.ativa] — mais precisamente a sub-paleta
/// [AureaPaleta.editor], que no tema claro continua escura (palco de video se
/// avalia sobre fundo escuro, e as alcas e rotulos do palco sao brancos
/// fixos).
///
/// O CUSTO: toda expressao `const` que continha um destes campos deixou de
/// ser constante e perdeu a palavra `const` (cerca de 170 sitios, apontados
/// um a um pelo analisador). Por isso [text] e [muted] CONTINUAM `const`:
/// sozinhos eles respondem por ~530 sitios, e os seis temas so variam o
/// texto em nuances de branco — nao paga o preco. Quando o tema claro for
/// ao editor, ai sim eles terao de virar getter tambem.
abstract final class AmColors {
  static AureaPaleta get _p => AureaPaleta.ativa.editor;

  /// O FUNDO ATRAS DA COMPOSICAO. Mais escuro que o cromo, para o quadro
  /// do projeto se destacar do que e ferramenta.
  static Color get bg => _p.stage;

  /// O CROMO: cabecalho, transporte, linha do tempo. Tudo que e ferramenta
  /// usa este tom, e por isso os tres blocos parecem uma peca so, que e o
  /// que eles sao.
  static Color get topBar => _p.background;
  static Color get panel => _p.background;
  static Color get panelHigh => _p.surface;

  /// A CAPSULA DO TEMPO e os chips em geral.
  static Color get chip => _p.chip;

  /// A PILULA DA CAMADA: o olho e a cor, flutuando sobre a trilha.
  static Color get pilula => _p.panel;

  /// A CAIXA DE VALOR e os chips do painel de transformacao.
  static Color get campo => _p.chip;

  /// Keyframe, curva e realce de contexto: o que esta LIGADO na tela.
  static Color get accent => _p.accent;
  static Color get accentDim => _p.accentDim;

  /// ACAO: Exportar, o "+", chips de acao.
  ///
  /// E O AZUL FUNDO, e nao o claro, porque este e um PREENCHIMENTO cheio com
  /// texto claro por cima (#F7F9FB sobre #245D8C da 6,8:1). O realce do que
  /// esta ligado usa o claro, que e um TRACO fino sobre o fundo escuro e
  /// precisa de 7,8:1 contra ele. Um so azul nao serve aos dois: o que se ve
  /// sobre o preto e claro demais para levar texto em cima.
  static Color get action => _p.primary;
  static Color get onAction => _p.onPrimary;
  static Color get actionDim => _p.primaryDim;

  /// Selecao e grupos: o fundo da camada escolhida e o chip de grupo.
  static Color get selection => _p.selected;

  /// Selecao em TEXTO e em TRACO, onde [selection] seria escuro demais.
  static Color get selectionText => _p.selectedText;

  /// Barras de camada com contraste para texto e keyframes.
  static Color get teal => _p.accent;
  static Color get tealBright => _p.keyframe;

  /// Excluir, erro, e a marca de alerta na regua.
  static Color get pink => _p.danger;

  /// O CABECOTE E BRANCO, como na referencia. Ele cruza trilhas de todas
  /// as cores: qualquer cor propria brigaria com alguma delas, e branco
  /// puro nao e usado em mais nada grande na tela.
  static Color get cabecote => _p.playhead;

  /// TEXTO — ainda `const`, ver o cabecalho da classe.
  static const Color text = AureaColors.text;
  static const Color muted = AureaColors.muted;

  static Color get hairline => _p.divider;

  // ------------------------------------------------ papeis da UI nova (ds)
  //
  // O design system (`core/ds/`) separa as superficies SO POR TOM, sem
  // borda — e para isso precisa de um degrau acima do [chip] e do texto
  // secundario da paleta em vigor. Os dois saem da paleta (nenhum hex
  // novo) e nao mudam nenhum papel que ja existia.

  /// O DEGRAU MAIS ALTO: campo apertado, item de menu sob o dedo. E o
  /// [chip] puxado 8% para o texto — claro o bastante para se separar do
  /// campo sem virar um cinza que brigue com o conteudo.
  static Color get campoAlto =>
      Color.lerp(_p.chip, _p.textPrimary, .08) ?? _p.chip;

  /// Texto secundario DO TEMA (rotulo de propriedade, dica). [muted]
  /// continua `const` pelos ~530 sitios antigos; a UI nova le este.
  static Color get textoSecundario => _p.textSecondary;

  /// Texto principal do tema, pelo mesmo motivo de [textoSecundario].
  static Color get textoPrincipal => _p.textPrimary;

  /// O losango de keyframe: a cor propria da paleta.
  static Color get keyframe => _p.keyframe;
}
