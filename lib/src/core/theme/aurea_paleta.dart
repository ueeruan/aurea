import 'package:flutter/widgets.dart';

import 'aurea_colors.dart';

/// Os temas que a pessoa escolhe em Ajustes > Aparencia.
///
/// O NOME do valor e o que vai para o disco (`settings.tema`), entao renomear
/// um deles e apagar a escolha de quem ja gravou. `aurea` e `light` tambem
/// atendem pelos nomes antigos 'escuro' e 'claro' (ver [AureaPaleta.resolver]).
enum AureaTemaId { aurea, aureaDark, midnight, oled, light, graphite }

/// ==========================================================================
/// A PALETA EM VIGOR — os PAPEIS de cor do app, com um valor por tema.
/// ==========================================================================
///
/// [AureaColors] continua sendo a tabela crua da marca (e continua `const`:
/// ela e padrao de CONTEUDO em `domain/` — cor de forma, de rotulo, de
/// neblina — e conteudo do projeto nao pode mudar com o tema do app).
/// Esta classe e a camada de cima: quem pinta CROMO le um papel daqui, por
/// [AppColors], [AmColors] ou [AureaTokens], e o papel muda com o tema.
///
/// POR QUE UM ESTATICO MUTAVEL ([ativa]) E NAO UM InheritedWidget: o app tem
/// mais de mil leituras de cor em lugares sem contexto (pintores, funcoes
/// soltas, estilos montados fora do `build`) e uma arvore cheia de widgets
/// `const`, que o Flutter nao reconstroi. A troca funciona porque a raiz
/// ([AureaApp]) escreve [ativa] ANTES de montar os filhos e troca a chave do
/// `MaterialApp`: a arvore inteira nasce de novo lendo a paleta nova. E o
/// mesmo mecanismo que o tema claro/escuro ja usava, so que com seis valores
/// em vez de um `bool`.
///
/// O TEMA "AUREA" E ESCRITO SO COM [AureaColors] — e a garantia de que o
/// padrao continua pixel a pixel o que era antes de existir tema.
@immutable
class AureaPaleta {
  const AureaPaleta({
    required this.id,
    required this.nome,
    required this.brightness,
    required this.background,
    required this.surface,
    required this.panel,
    required this.primary,
    required this.accent,
    required this.textPrimary,
    required this.textSecondary,
    required this.divider,
    required this.timeline,
    required this.clip,
    required this.selected,
    required this.playhead,
    required this.keyframe,
    required this.danger,
    required this.success,
    required this.stage,
    required this.chip,
    required this.onPrimary,
    required this.primaryDim,
    required this.onAccent,
    required this.accentDim,
    required this.keyframeDim,
    required this.selectedText,
    required this.warning,
  });

  final AureaTemaId id;

  /// O nome que aparece na amostra. Nome proprio: nao se traduz.
  final String nome;

  final Brightness brightness;

  // ------------------------------------------------------ os quinze papeis

  /// Fundo da tela e do cromo do editor (cabecalho, transporte).
  final Color background;

  /// Barras, folhas e grupos de ajuste.
  final Color surface;

  /// O que flutua sobre a superficie: capsula, cartao, pilula da camada.
  final Color panel;

  /// PREENCHIMENTO de acao: Exportar, o "+". Leva [onPrimary] por cima.
  final Color primary;

  /// TRACO de acao e estado ligado. Leva [onAccent] quando vira fundo.
  final Color accent;

  final Color textPrimary;
  final Color textSecondary;

  /// Separadores e contornos.
  final Color divider;

  /// Fundo da linha do tempo.
  final Color timeline;

  /// A base da barra de camada, antes de misturar a cor do tipo. Os rotulos
  /// da barra sao brancos, entao este papel e ESCURO em todo tema.
  final Color clip;

  /// Fundo da camada selecionada e do chip de grupo.
  final Color selected;

  final Color playhead;
  final Color keyframe;
  final Color danger;
  final Color success;

  // ------------------------------------------- os que o codigo ja exigia

  /// O fundo ATRAS da composicao. Escuro em todo tema: video se avalia
  /// sobre fundo escuro.
  final Color stage;

  /// Fundo de chip, tile e campo numerico.
  final Color chip;

  final Color onPrimary;

  /// [primary] apagado, para fundo de chip de acao.
  final Color primaryDim;

  final Color onAccent;

  /// [accent] apagado, para fundo de chip aceso.
  final Color accentDim;

  /// [keyframe] apagado, para fundo de faixa animada.
  final Color keyframeDim;

  /// Selecao em TEXTO e em TRACO, onde [selected] seria escuro demais.
  final Color selectedText;

  final Color warning;

  bool get claro => brightness == Brightness.light;

  /// A PALETA QUE O EDITOR USA SOB ESTE TEMA.
  ///
  /// Nos temas escuros e a propria. No Light e a do tema Aurea: a barra de
  /// camada, os rotulos da linha do tempo e as alcas do palco sao brancos
  /// fixos (desenhados para fundo escuro), e o texto do cromo do editor
  /// ainda e `const`. Um editor claro com esses brancos ficaria ilegivel —
  /// entao, ate o texto do editor virar papel, o editor continua escuro
  /// sob o tema claro, como sempre foi.
  AureaPaleta get editor => claro ? aurea : this;

  // ------------------------------------------------------------- o estado

  /// O UNICO estado mutavel do sistema de temas. Quem escreve e
  /// [AppTheme.tema], chamado no `build` da raiz antes de os filhos nascerem.
  static AureaPaleta ativa = aurea;

  static AureaPaleta de(AureaTemaId id) => switch (id) {
    AureaTemaId.aurea => aurea,
    AureaTemaId.aureaDark => aureaDark,
    AureaTemaId.midnight => midnight,
    AureaTemaId.oled => oled,
    AureaTemaId.light => light,
    AureaTemaId.graphite => graphite,
  };

  /// Na ordem em que aparecem em Ajustes.
  static const List<AureaPaleta> todas = [
    aurea,
    aureaDark,
    midnight,
    oled,
    light,
    graphite,
  ];

  /// O valor gravado de 'seguir o sistema'. Nao e um tema: e Aurea ou Light
  /// conforme o brilho do aparelho.
  static const String modoSistema = 'sistema';

  /// O que vai para o disco quando a pessoa escolhe [id].
  ///
  /// `aurea` e `light` gravam 'escuro' e 'claro' — os nomes que o app ja
  /// gravava quando so existiam dois temas. Assim quem volta para uma versao
  /// antiga do app nao perde a escolha, e nao ha migracao nenhuma.
  static String modoDe(AureaTemaId id) => switch (id) {
    AureaTemaId.aurea => 'escuro',
    AureaTemaId.light => 'claro',
    _ => id.name,
  };

  /// Do valor gravado para o tema. Valor desconhecido (uma versao futura
  /// gravou um tema que esta nao tem) cai no padrao em vez de quebrar.
  static AureaTemaId resolver(String? modo, Brightness sistema) {
    switch (modo) {
      case 'escuro':
        return AureaTemaId.aurea;
      case 'claro':
        return AureaTemaId.light;
      case modoSistema:
        return sistema == Brightness.light
            ? AureaTemaId.light
            : AureaTemaId.aurea;
    }
    for (final id in AureaTemaId.values) {
      if (id.name == modo) return id;
    }
    return AureaTemaId.aurea;
  }

  // ------------------------------------------------------------- os temas
  //
  // Os hexadecimais dos temas NOVOS moram aqui, e so aqui. Contraste
  // calculado (WCAG 2.1) para texto/fundo, apagado/superficie, acento/fundo
  // e rotulo/preenchimento: todos acima de 4,5:1. Nenhum foi visto em
  // aparelho ainda — sao o ponto de partida, nao a palavra final.

  /// AUREA — o padrao. Identico ao app de antes dos temas.
  static const aurea = AureaPaleta(
    id: AureaTemaId.aurea,
    nome: 'Aurea',
    brightness: Brightness.dark,
    background: AureaColors.bg,
    surface: AureaColors.surface,
    panel: AureaColors.surfaceHigh,
    primary: AureaColors.brand,
    accent: AureaColors.accent,
    textPrimary: AureaColors.text,
    textSecondary: AureaColors.muted,
    divider: AureaColors.border,
    timeline: AureaColors.chrome,
    clip: AureaColors.chromeHigh,
    selected: AureaColors.brandDeep,
    playhead: AureaColors.playhead,
    keyframe: AureaColors.keyframe,
    danger: AureaColors.danger,
    success: Color(0xFF4CD08A),
    stage: AureaColors.stage,
    chip: AureaColors.chip,
    onPrimary: AureaColors.text,
    primaryDim: Color(0xFF16304A),
    onAccent: AureaColors.onAccent,
    accentDim: AureaColors.accentDim,
    keyframeDim: AureaColors.keyframeDim,
    selectedText: AureaColors.selectionText,
    warning: AureaColors.warning,
  );

  /// AUREA DARK — a mesma marca, um degrau mais funda: azul-preto no fundo,
  /// cinza-azulado nas superficies, o azul claro da marca como acao.
  static const aureaDark = AureaPaleta(
    id: AureaTemaId.aureaDark,
    nome: 'Aurea Dark',
    brightness: Brightness.dark,
    background: Color(0xFF0A0E13),
    surface: Color(0xFF0F141A),
    panel: Color(0xFF151C24),
    primary: AureaColors.brand,
    accent: AureaColors.accent,
    textPrimary: AureaColors.text,
    textSecondary: Color(0xFFA3AFBC),
    divider: Color(0xFF212C38),
    timeline: Color(0xFF0A0E13),
    clip: Color(0xFF0F141A),
    selected: AureaColors.brandDeep,
    playhead: AureaColors.playhead,
    keyframe: AureaColors.keyframe,
    danger: AureaColors.danger,
    success: Color(0xFF4CD08A),
    stage: Color(0xFF06090C),
    chip: Color(0xFF1B2530),
    onPrimary: AureaColors.text,
    primaryDim: Color(0xFF132A41),
    onAccent: AureaColors.onAccent,
    accentDim: Color(0xFF19344D),
    keyframeDim: Color(0xFF1D384F),
    selectedText: AureaColors.selectionText,
    warning: AureaColors.warning,
  );

  /// MIDNIGHT — preto azulado, superficies em azul muito escuro e um azul
  /// eletrico como acao.
  static const midnight = AureaPaleta(
    id: AureaTemaId.midnight,
    nome: 'Midnight',
    brightness: Brightness.dark,
    background: Color(0xFF0B1020),
    surface: Color(0xFF111831),
    panel: Color(0xFF18213F),
    primary: Color(0xFF3D4FB8),
    accent: Color(0xFF8EA2FF),
    textPrimary: Color(0xFFF2F4FF),
    textSecondary: Color(0xFFA5AECF),
    divider: Color(0xFF2A3660),
    timeline: Color(0xFF0B1020),
    clip: Color(0xFF111831),
    selected: Color(0xFF232F6B),
    playhead: Color(0xFFFFFFFF),
    keyframe: Color(0xFFC3CCFF),
    danger: Color(0xFFFF6B7F),
    success: Color(0xFF4CD08A),
    stage: Color(0xFF070A16),
    chip: Color(0xFF202B4D),
    onPrimary: Color(0xFFF2F4FF),
    primaryDim: Color(0xFF1C2659),
    onAccent: Color(0xFF0A0E1F),
    accentDim: Color(0xFF252F66),
    keyframeDim: Color(0xFF2A3570),
    selectedText: Color(0xFFC3CCFF),
    warning: AureaColors.warning,
  );

  /// OLED — preto absoluto no fundo (pixel apagado em tela OLED), cinza nas
  /// superficies e o azul da marca. O palco tambem e #000: quem separa o
  /// quadro da composicao do cromo aqui e a linha [divider], nao o tom.
  static const oled = AureaPaleta(
    id: AureaTemaId.oled,
    nome: 'OLED',
    brightness: Brightness.dark,
    background: Color(0xFF000000),
    surface: Color(0xFF0A0B0D),
    panel: Color(0xFF121316),
    primary: AureaColors.brand,
    accent: AureaColors.accent,
    textPrimary: Color(0xFFF5F7FA),
    textSecondary: Color(0xFF9AA3AE),
    // MAIS CLARO QUE NOS OUTROS TEMAS de proposito: aqui o palco e o cromo
    // sao os dois #000, e quem separa o quadro da composicao da ferramenta
    // e esta linha. Em #24272D ela dava 1,40:1 sobre o preto e sumia.
    divider: Color(0xFF30343A),
    timeline: Color(0xFF000000),
    clip: Color(0xFF0A0B0D),
    selected: AureaColors.brandDeep,
    playhead: AureaColors.playhead,
    keyframe: AureaColors.keyframe,
    danger: AureaColors.danger,
    success: Color(0xFF4CD08A),
    stage: Color(0xFF000000),
    chip: Color(0xFF1A1C20),
    onPrimary: AureaColors.text,
    primaryDim: Color(0xFF11273D),
    onAccent: AureaColors.onAccent,
    accentDim: Color(0xFF172F46),
    keyframeDim: Color(0xFF1B3449),
    selectedText: AureaColors.selectionText,
    warning: AureaColors.warning,
  );

  /// LIGHT — branco e cinza claro, com o azul FUNDO da marca como acao
  /// (e ele que carrega texto branco; o claro sumiria sobre o branco).
  ///
  /// [stage] e [clip] continuam escuros de proposito, e o editor usa a
  /// paleta de [editor] — ver o comentario la.
  static const light = AureaPaleta(
    id: AureaTemaId.light,
    nome: 'Light',
    brightness: Brightness.light,
    background: AureaColors.lightBg,
    surface: AureaColors.lightSurface,
    panel: AureaColors.lightSurfaceHigh,
    primary: AureaColors.brand,
    accent: AureaColors.lightAccent,
    textPrimary: AureaColors.lightText,
    textSecondary: AureaColors.lightMuted,
    divider: AureaColors.lightBorder,
    timeline: AureaColors.lightBg,
    clip: AureaColors.chromeHigh,
    selected: Color(0xFFCFE0F2),
    // Cabecote branco sobre cromo claro nao existe: aqui ele e o texto.
    playhead: AureaColors.lightText,
    // #2E7FB0 (o keyframe claro antigo) da 4,06:1 sobre o fundo claro e
    // reprova AA para traco fino; este da 4,99:1.
    keyframe: Color(0xFF24709E),
    // Idem: #D94B4B da 3,83:1; este da 4,75:1.
    danger: Color(0xFFC43D3D),
    success: Color(0xFF1A7A42),
    stage: AureaColors.stage,
    chip: AureaColors.lightChip,
    onPrimary: AureaColors.lightOnAccent,
    primaryDim: AureaColors.lightAccentDim,
    onAccent: AureaColors.lightOnAccent,
    accentDim: AureaColors.lightAccentDim,
    keyframeDim: AureaColors.lightKeyframeDim,
    selectedText: AureaColors.lightSelection,
    warning: Color(0xFF9A5B00),
  );

  /// GRAPHITE — grafite neutro, cinza nas superficies e um azul frio,
  /// dessaturado, como acao.
  static const graphite = AureaPaleta(
    id: AureaTemaId.graphite,
    nome: 'Graphite',
    brightness: Brightness.dark,
    background: Color(0xFF131416),
    surface: Color(0xFF1A1B1E),
    panel: Color(0xFF222327),
    primary: Color(0xFF3A6A94),
    accent: Color(0xFF8DB9DA),
    textPrimary: Color(0xFFF5F5F6),
    textSecondary: Color(0xFFA9ACB3),
    divider: Color(0xFF33363C),
    timeline: Color(0xFF131416),
    clip: Color(0xFF1A1B1E),
    selected: Color(0xFF2B3A4A),
    playhead: Color(0xFFFFFFFF),
    keyframe: Color(0xFFC5DCEC),
    danger: AureaColors.danger,
    success: Color(0xFF4CD08A),
    stage: Color(0xFF0C0D0E),
    chip: Color(0xFF2A2C31),
    onPrimary: AureaColors.text,
    primaryDim: Color(0xFF1F3345),
    onAccent: Color(0xFF0E1114),
    accentDim: Color(0xFF263A4A),
    keyframeDim: Color(0xFF2C4253),
    selectedText: Color(0xFFC5DCEC),
    warning: AureaColors.warning,
  );
}
