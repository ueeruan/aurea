import 'package:flutter/animation.dart';
import 'package:flutter/painting.dart';

import '../ui/am_colors.dart';

/// OS NUMEROS DA UI NOVA DO EDITOR, num lugar so.
///
/// Sao as proporcoes medidas nos recursos do app de referencia (dp; texto
/// em sp) e reescritas com nomes nossos. Nenhum componente do design
/// system escreve um numero de layout solto: ou ele esta aqui, ou e
/// derivado daqui. Cada linha diz de onde o numero veio, para que ninguem
/// "arredonde" uma medida sem saber o que ela segurava.
///
/// A origem de todas: `scratchpad/am_ref/RECURSOS.md`, tabela "TOKENS
/// PROPOSTOS", e o §2 de `PLANO_UI_NOVA.md`.
abstract final class AureaDims {
  // ------------------------------------------------------------- editor

  /// Barra de cima do editor (`activity_edit_navbar` h=42).
  static const double barraDoTopo = 42;

  /// Barra contextual da camada/ajuste/multisselecao (mesma altura).
  static const double barraContextual = 42;

  /// Largura de um botao de barra (ImageButton w=40 nas barras).
  static const double botaoDeBarra = 40;

  /// Barra de transporte (`#playBar` h=46).
  static const double transporte = 46;

  /// Medidor de nivel na barra de transporte (`LevelMeterView` h=2).
  static const double medidorDeNivel = 2;

  /// Coluna lateral da previa (`#viewmodeHolder` w=50).
  static const double colunaDaPrevia = 50;

  /// Area da linha do tempo (`edit_timeline_height`).
  static const double timeline = 280;

  /// Botao "+" (`add_button_size`).
  static const double botaoAdicionar = 73;

  /// Distancia do "+" ate a borda (`add_button_inset`).
  static const double margemDoAdicionar = 6;

  /// Folha de adicionar (`add_popup_height`).
  static const double folhaDeAdicionar = 251;

  /// Barra de abas e trilho lateral da folha de adicionar (58 x 58).
  static const double abasDaFolhaDeAdicionar = 58;

  /// Painel da camada, que SOBE por cima da timeline (`edit_panel_height`).
  static const double painel = 200;

  /// Meio painel: o deslocamento da folha (`edit_panel_half_height`).
  static const double meioPainel = 100;

  /// Painel grande (`#elementLargeFragmentHolder` = transporte + timeline).
  static const double painelGrande = 326;

  // ----------------------------------------------------------- timeline

  /// Regua da timeline (`timeline_top_space`).
  static const double regua = 42;

  /// Linha de camada (`timeline_row_height`).
  static const double linhaDeCamada = 28;

  /// Recuo vertical do clipe na linha (mT/mB 2,5 -> clipe visivel 23).
  static const double recuoDoClipe = 2.5;

  /// Altura visivel do clipe (28 - 2 x 2,5).
  static const double clipe = linhaDeCamada - 2 * recuoDoClipe;

  /// Raio do clipe (`timeline_item_corner_rad`).
  static const double raioDoClipe = 1.5;

  /// Recuo do rotulo dentro do clipe (LinearLayout mL/mR 14).
  static const double recuoDoRotuloDoClipe = 14;

  /// Corpo do rotulo do clipe (`#elementLabel` 11).
  static const double rotuloDoClipe = 11;

  /// Cabecalho da camada, aberto (`timeline_header_width_expanded`).
  static const double cabecalhoDaCamada = 70;

  /// Miniatura no cabecalho (`#trackThumb` w=32).
  static const double miniaturaDoCabecalho = 32;

  /// Faixa de cor no cabecalho (`#trackTag` w=10).
  static const double faixaDeCor = 10;

  /// Olho/visivel no cabecalho: alvo de toque de 44.
  static const double olhoDoCabecalho = 44;

  /// Alca de reordenar (`timeline_grip_width`).
  static const double alcaDeReordenar = 35;

  /// Alca de trim: area de TOQUE fora do clipe
  /// (`timeline_element_trim_grip_size`).
  static const double alcaDeTrim = 30;

  /// Desenho da alca de trim (`ac_trimgrip_left` 17 x 15).
  static const Size desenhoDaAlcaDeTrim = Size(17, 15);

  /// Linha do cabecote, FIXA no centro (`playhead_width`).
  static const double cabecote = 1.5;

  /// Zona de toque do cabecote (`#playheadTouchZone` 100 x 38).
  static const Size toqueDoCabecote = Size(100, 38);

  /// Zoom base: dp por segundo (`timeline_base_dp_per_sec`).
  static const double dpPorSegundo = 100;

  /// Borda que dispara a auto-rolagem (`timeline_scroll_margin`).
  static const double bordaDeAutoRolagem = 38;

  /// Velocidade da auto-rolagem, dp/s (`timeline_scroll_per_second`).
  static const double velocidadeDeAutoRolagem = 120;

  /// Riscos da regua: segundo / meio segundo / quadro
  /// (`timeline_tick_sec/_halfsec/_frame`).
  static const double riscoDeSegundo = 18;
  static const double riscoDeMeioSegundo = 12;
  static const double riscoDeQuadro = 5;

  /// Espessura e vao minimo dos riscos (`timeline_tick_width`,
  /// `timeline_tick_min_space`).
  static const double larguraDoRisco = 1;
  static const double vaoMinimoDoRisco = 4;

  /// Corpo do tempo na regua (`timeline_cts_text_size`).
  static const double rotuloDaRegua = 14;

  /// Marcador de tempo: area de toque (`timeline_bookmark_touch_size`).
  static const double marcadorDeTempo = 42;

  /// Losango de keyframe na linha: RAIO (`keyframeMarkerSize`, provavel).
  static const double raioDoKeyframe = 3;

  /// Traco do losango (`keyframeStrokeWidth`).
  static const double tracoDoKeyframe = 1;

  /// Folga de toque do losango (`keyframe_select_margin`).
  static const double folgaDoKeyframe = 20;

  /// Botao "mais" da timeline (`#timelineOverflowButton` 40 x 40).
  static const double botaoMaisDaTimeline = 40;

  // ------------------------------------------------------------ paineis

  /// Margem lateral do painel (`fragment_element_edit` mS/mE).
  static const double margemDoPainel = 22;

  /// Vao entre pecas do painel (idem, gutter).
  static const double vaoDoPainel = 6;

  /// Respiro no topo do corpo do painel (`fragment_element_edit` mT).
  static const double topoDoPainel = 15;

  /// Botao de painel (`effect_setting_height`).
  static const double botaoDePainel = 38;

  /// Bloco de painel, icone grande + rotulo (`effect_setting_height_large`).
  static const double blocoDePainel = 57;

  /// Separacao entre segmentos (`#buttonSplit` 1).
  static const double vaoDeSegmento = 1;

  /// Item de lista do painel (`list_panel_item_height`).
  static const double itemDeLista = 37;

  /// Respiro da lista (`list_panel_padding`).
  static const double respiroDaLista = 6.5;

  /// Linha de propriedade (`effect_setting_slider` h=51).
  static const double linhaDePropriedade = 51;

  /// Coluna do rotulo da linha de propriedade.
  static const double rotuloDaPropriedade = 75;

  /// Caixa de valor da linha de propriedade.
  static const double caixaDeValor = 56;

  /// Altura da caixa de valor: cabe na linha de 51 com folga de toque.
  static const double alturaDaCaixaDeValor = 30;

  /// Largura do botao de keyframe com as setas (‹ ◆ ›): 18 + 28 + 18.
  /// FIXA mesmo sem setas, para a linha nao pular quando nasce a 1a marca.
  static const double botaoDeKeyframe = 64;

  /// Deslizante: alca, trilho e passo de encaixe
  /// (`style/alightSliderNormal` 25 / 2,5 / 8).
  static const double alcaDoDeslizante = 25;
  static const double trilhoDoDeslizante = 2.5;
  static const double encaixeDoDeslizante = 8;

  /// Altura de toque do deslizante (`slider_height`).
  static const double toqueDoDeslizante = 44;

  /// Barra de ferramentas contextual: icone 24 + rotulo 10 na altura de
  /// um bloco de painel (57). Mora DENTRO dos 280 da timeline.
  static const double barraDeFerramentas = blocoDePainel;

  /// Largura de um botao da barra contextual.
  static const double botaoDeFerramenta = 64;

  /// Cabecalho do cartao de efeito: um item de lista (37).
  static const double cabecalhoDoCartao = itemDeLista;

  /// Cabecalho do painel: titulo e acoes na altura de um botao de barra.
  static const double cabecalhoDoPainel = botaoDePainel;

  /// Barra de sub-abas do painel: a altura de um botao de painel.
  static const double abas = botaoDePainel;

  // -------------------------------------------------------------- menus

  /// Item de menu (`alight_popup_menu_list_item`).
  static const double itemDeMenu = 40;

  /// Largura do menu (`popupMenuWidth`).
  static const double larguraDoMenu = 250;

  /// Altura maxima do menu; cresce em telas altas (`popupMaxHeight`).
  static const double alturaMaximaDoMenu = 450;

  /// Barra de marca do item escolhido (`#selectionBar`).
  static const double barraDeMarcaDoMenu = 4;

  // ------------------------------------------------ tracos e divisores

  /// Divisor fino / divisor de painel (`panel_divider_width`).
  static const double divisorFino = .5;
  static const double divisorDePainel = .7;

  /// Contorno de selecao, simples e multipla (`singleSelectionStroke`).
  static const double tracoDeSelecao = 2;
  static const double tracoDeMultisselecao = 1.5;

  /// Alcas do palco (`shapeHandleRadius`, selecionada, `rotateGripSize`).
  static const double raioDaAlcaDoPalco = 5;
  static const double raioDaAlcaDoPalcoEscolhida = 6;
  static const double alcaDeGiro = 35;

  /// Editor de curva: raio do ponto de controle e traco da curva.
  static const double raioDoControleDaCurva = 12;
  static const double tracoDaCurva = 3;

  // --------------------------------------------------------------- raios

  /// Raios (clipe, `radius_3dp`, botao de painel, lista, aviso da previa).
  static const double raioXs = 1.5;
  static const double raioSm = 3;
  static const double raioMd = 4;
  static const double raioLg = 5;
  static const double raioXl = 8;

  /// Folha que sobe da borda (`add_popup_small_radius`).
  static const double raioDaFolha = 13.5;

  /// Fim de pilula (`ac_track_header_bg_short`).
  static const double raioPilula = 100;

  // --------------------------------------------------------------- icones

  /// Icones 16/20/24/32 (24 padrao; 32 nos blocos).
  static const double iconeSm = 16;
  static const double iconeMd = 20;
  static const double iconeLg = 24;
  static const double iconeXl = 32;

  // -------------------------------------------------------------- espacos

  /// Espacos 2/4/6/8/10/15/20 (`margin_*`, `padding_*`, `default_margin`).
  static const double e2 = 2;
  static const double e4 = 4;
  static const double e6 = 6;
  static const double e8 = 8;
  static const double e10 = 10;
  static const double e15 = 15;
  static const double e20 = 20;

  /// Toque minimo: 40 no app de referencia; no Aurea, 44 onde couber.
  static const double toqueMinimo = 40;
  static const double toqueConfortavel = 44;

  // --------------------------------------------------------------- texto

  /// Corpo de barra e de barra pequena (`actionBarTextSize`, `...Small`).
  static const double textoDeBarra = 14;
  static const double textoDeBarraPequeno = 11;

  /// Rotulo de botao de painel (`style/PanelButton` 10).
  static const double textoDeRotulo = 10;

  /// Informacao (infoBar 12) e rotulo de aba da folha de adicionar (9).
  static const double textoDeInfo = 12;
  static const double textoDeAba = 9;

  /// Rotulo da linha de propriedade e do cartao.
  static const double textoDePropriedade = 12;

  /// Titulo do painel.
  static const double textoDeTitulo = 14;

  /// Margem do aviso breve ate a base (`#presetToast` mB).
  static const double margemDoAviso = 34;
}

/// O MOVIMENTO DA UI NOVA.
///
/// Tres duracoes e duas curvas, e nada de mola: a referencia anima tudo
/// com desacelerar na ENTRADA e acelerar na SAIDA. Quem precisa de outra
/// duracao esta inventando movimento.
abstract final class AureaMotion {
  /// Painel, submenu, barra (`option_sheet_*`, `element_submenu_*`).
  static const Duration rapido = Duration(milliseconds: 100);

  /// Folha grande, fundo (`create_project_*`, `fade_*`).
  static const Duration normal = Duration(milliseconds: 200);

  /// Folha que sobe da borda (`slide_in_bottom`, `slide_out_bottom`).
  static const Duration lento = Duration(milliseconds: 300);

  /// Entrada: desacelera.
  static const Curve entrada = Curves.decelerate;

  /// Saida: acelera — y = t², o espelho exato de [entrada] (que e
  /// 1 - (1 - t)²). Os pontos de controle 1/3 e 2/3 fazem x = t, e ai a
  /// cubica vira a quadratica pura.
  static const Curve saida = Cubic(1 / 3, 0, 2 / 3, 1 / 3);
}

/// A HIERARQUIA DE COR DA UI NOVA — so papeis, nenhum hex.
///
/// Superficies separadas por TOM, nunca por borda: seis niveis, do palco
/// (o mais fundo) ao campo apertado (o mais alto). Texto secundario vem do
/// tema e o destaque e UM so, para estado ativo.
///
/// TUDO GETTER: a paleta muda em tempo de execucao ([AureaPaleta.ativa]) e
/// um `const` aqui prenderia o editor ao tema de quando o app compilou.
abstract final class AureaCores {
  /// Nivel 0 — atras da composicao.
  static Color get palco => AmColors.bg;

  /// Nivel 1 — cromo: barra de cima, transporte, timeline.
  static Color get cromo => AmColors.topBar;

  /// Nivel 2 — o painel que sobe e as folhas.
  static Color get painel => AmColors.panelHigh;

  /// Nivel 3 — o que flutua sobre o painel: cartao, cabecalho de efeito.
  static Color get elevado => AmColors.pilula;

  /// Nivel 4 — caixa de valor, chip, campo.
  static Color get campo => AmColors.chip;

  /// Nivel 5 — campo apertado, item sob o dedo.
  static Color get campoAlto => AmColors.campoAlto;

  static Color get texto => AmColors.textoPrincipal;
  static Color get textoSecundario => AmColors.textoSecundario;

  /// O UNICO destaque: o que esta LIGADO (aba ativa, trilho cheio, alvo).
  static Color get destaque => AmColors.accent;
  static Color get destaqueApagado => AmColors.accentDim;

  /// Preenchimento de acao (Exportar, o "+"), com [sobreAcao] por cima.
  static Color get acao => AmColors.action;
  static Color get sobreAcao => AmColors.onAction;

  static Color get keyframe => AmColors.keyframe;
  static Color get perigo => AmColors.pink;
  static Color get selecao => AmColors.selection;
  static Color get cabecote => AmColors.cabecote;
}

/// OS ESTILOS DE TEXTO DA UI NOVA. Getters, pelo mesmo motivo de
/// [AureaCores]: a cor vem do tema em vigor.
abstract final class AureaEstilos {
  /// Titulo do painel e da folha.
  static TextStyle get titulo => TextStyle(
    fontSize: AureaDims.textoDeTitulo,
    fontWeight: FontWeight.w600,
    color: AureaCores.texto,
  );

  /// Rotulo da linha de propriedade.
  static TextStyle get propriedade => TextStyle(
    fontSize: AureaDims.textoDePropriedade,
    color: AureaCores.textoSecundario,
  );

  /// Numero da caixa de valor: algarismos de largura fixa, para o numero
  /// nao danar enquanto o dedo arrasta.
  static TextStyle get valor => TextStyle(
    fontSize: 13,
    fontWeight: FontWeight.w600,
    color: AureaCores.texto,
    fontFeatures: const [FontFeature.tabularFigures()],
  );

  /// Rotulo de botao de barra e de bloco (10 sp).
  static TextStyle get rotulo => TextStyle(
    fontSize: AureaDims.textoDeRotulo,
    color: AureaCores.textoSecundario,
  );

  /// Titulo de secao: pequeno e apagado, para nao competir com o valor.
  static TextStyle get secao => TextStyle(
    fontSize: 11,
    fontWeight: FontWeight.w600,
    letterSpacing: .3,
    color: AureaCores.textoSecundario,
  );

  /// Texto corrido (item de menu, nome da camada).
  static TextStyle get corpo => TextStyle(fontSize: 13, color: AureaCores.texto);
}
