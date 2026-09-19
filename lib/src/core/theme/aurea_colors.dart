import 'package:flutter/widgets.dart';

/// ==========================================================================
/// A IDENTIDADE VISUAL DO AUREA — UM LUGAR SO.
/// ==========================================================================
///
/// Até 18/09/2026 a marca era VERDE-LIMA no fundo grafite, e as cores
/// moravam em três lugares que se repetiam: [AppColors] (o app fora do
/// editor), [AmColors] (o cromo do editor) e [AureaTokens] (o design system
/// com dois brilhos). Três listas com os mesmos hexes é uma promessa de
/// divergirem — e divergiram: o lima do app era #B8FF3D e o do editor
/// também, mas por coincidência, não por contrato.
///
/// Agora existe UMA tabela: esta. As outras três passaram a LER daqui, e
/// nenhuma delas guarda hexadecimal próprio. Trocar a identidade de novo é
/// mexer neste arquivo e em mais nenhum.
///
/// A LINGUAGEM NOVA É AZUL. O verde saiu de toda a interface. A hierarquia
/// que antes vinha de três MATIZES (lima = ação, teal = keyframe, violeta =
/// seleção) agora vem de três VALORES do mesmo azul — que é o que uma
/// identidade de uma cor só exige, e o motivo de os quatro azuis abaixo
/// existirem em vez de um:
///
///   brandLight #6FAED9  clareia sobre o fundo escuro   -> AÇÃO e ESTADO
///   brandSoft  #A9D3EC  o mais claro da família        -> KEYFRAME, SELEÇÃO
///   brand      #245D8C  fundo de botão com texto claro -> PREENCHIMENTO
///   brandDeep  #123A63  o mais fundo                   -> FUNDOS DE GRUPO
///
/// CONTRASTE CONFERIDO, não estimado (fórmula WCAG 2.1, luminância
/// relativa):
///
///   accent  #6FAED9 sobre bg #0F141A ....... 7,8:1  (texto e ícone: AA)
///   soft    #A9D3EC sobre bg #0F141A ....... 11,9:1 (texto e ícone: AAA)
///   text    #F7F9FB sobre brand #245D8C .... 6,8:1  (rótulo de botão: AA)
///   muted   #AAB6C3 sobre surface #151C24 .. 7,4:1  (texto secundário: AA)
///   border  #273442 sobre surface #151C24 .. 1,4:1  (separador, não texto)
///
/// O TEMA CLARO NÃO É O ESCURO INVERTIDO. No claro o papel de AÇÃO pede o
/// azul FUNDO ([brand]) porque é ele que carrega texto branco; no escuro
/// pede o CLARO ([brandLight]) porque é ele que se vê sobre o preto. Os
/// dois papéis existem como par ([accent]/[onAccent]) exatamente para que
/// quem desenha não precise saber disso.
abstract final class AureaColors {
  // ------------------------------------------------------------ os azuis

  /// Primary Dark Blue. O mais fundo da família: fundo de grupo, de faixa
  /// apagada e da tarja que precisa recuar sem sumir.
  static const Color brandDeep = Color(0xFF123A63);

  /// Primary Blue. Preenchimento: botão principal, chip aceso, seleção.
  static const Color brand = Color(0xFF245D8C);

  /// Light Blue. A cor de AÇÃO: o que a pessoa toca, o que está ligado.
  static const Color brandLight = Color(0xFF6FAED9);

  /// Soft Blue. A mais clara. Keyframe, curva e seleção em texto.
  static const Color brandSoft = Color(0xFFA9D3EC);

  // ------------------------------------------------------- neutros escuros

  /// Background Dark. O fundo da tela.
  static const Color bg = Color(0xFF0F141A);

  /// Surface. Barras, folhas, painéis, linha do tempo.
  static const Color surface = Color(0xFF151C24);

  /// Surface Elevated. O que flutua sobre a superfície (cápsulas, cartões).
  static const Color surfaceHigh = Color(0xFF1B2530);

  /// Fundo de chip, tile e campo numérico.
  static const Color chip = Color(0xFF212D3A);

  /// Borders. Separadores e contornos.
  static const Color border = Color(0xFF273442);

  /// Text Primary.
  static const Color text = Color(0xFFF7F9FB);

  /// Text Secondary.
  static const Color muted = Color(0xFFAAB6C3);

  // ------------------------------------------------------------- os papeis

  /// AÇÃO: botão principal, o "+", chip ligado, ícone ativo.
  static const Color accent = brandLight;

  /// O que vai ESCRITO sobre [accent] quando ele é preenchimento.
  static const Color onAccent = Color(0xFF0B1117);

  /// [accent] apagado, para fundo de chip aceso.
  static const Color accentDim = Color(0xFF1D3A55);

  /// KEYFRAME: diamantes, curvas, o cabeçote em contexto.
  static const Color keyframe = brandSoft;

  /// [keyframe] apagado, para fundo de faixa animada.
  static const Color keyframeDim = Color(0xFF22405A);

  /// SELEÇÃO: fundo da camada selecionada e do chip de grupo.
  static const Color selection = brand;

  /// SELEÇÃO em texto e em traço — onde [selection] seria escuro demais.
  static const Color selectionText = brandSoft;

  /// Excluir, erro, alerta.
  static const Color danger = Color(0xFFFF6B6B);

  /// Aviso (orçamento de quadro estourado, por exemplo).
  static const Color warning = Color(0xFFFFC978);

  // ------------------------------------------------------- cromo do editor

  /// O FUNDO ATRÁS DA COMPOSIÇÃO. Mais fundo que o cromo, para o quadro do
  /// projeto se destacar do que é ferramenta.
  static const Color stage = Color(0xFF0A0E13);

  /// Cabeçalho, transporte e linha do tempo: tudo que é ferramenta.
  static const Color chrome = bg;
  static const Color chromeHigh = surface;

  /// A pílula da camada e a caixa de valor do painel de transformação.
  static const Color pill = surfaceHigh;
  static const Color field = chip;

  /// O CABEÇOTE É BRANCO. Ele cruza trilhas de todas as cores: qualquer cor
  /// própria brigaria com alguma delas, e branco puro não é usado em mais
  /// nada grande na tela.
  static const Color playhead = Color(0xFFFFFFFF);

  // ------------------------------------------------------------ tema claro

  static const Color lightBg = Color(0xFFF4F6F9);
  static const Color lightSurface = Color(0xFFFFFFFF);
  static const Color lightSurfaceHigh = Color(0xFFE9EFF6);
  static const Color lightChip = Color(0xFFDCE6F0);
  static const Color lightBorder = Color(0xFFCDD9E6);
  static const Color lightText = Color(0xFF0F141A);
  static const Color lightMuted = Color(0xFF55697D);
  static const Color lightAccent = brand;
  static const Color lightOnAccent = Color(0xFFFFFFFF);
  static const Color lightAccentDim = Color(0xFFDCEAF7);
  static const Color lightKeyframe = Color(0xFF2E7FB0);
  static const Color lightKeyframeDim = Color(0xFFCFE6F5);
  static const Color lightSelection = brandDeep;
  static const Color lightDanger = Color(0xFFD94B4B);

  /// A COR DE MARCA EM FUNDO ESCURO. Um degradê de três paradas na diagonal,
  /// do azul fundo ao azul claro: é o que dá volume de tubo à logo nova sem
  /// precisar de bitmap nenhum.
  static const List<Color> brandGradient = [brandDeep, brand, brandLight];

  /// A FAMÍLIA DE AZUIS, para quem precisa gerar N tons (a rampa de uma
  /// onda, o degradê de um preset, a escala de um medidor).
  static const List<Color> scale = [brandDeep, brand, brandLight, brandSoft];
}

/// ==========================================================================
/// ESPAÇAMENTO — grade de 4.
/// ==========================================================================
///
/// A grade é de 4 e não de 8 porque o editor tem muito alvo pequeno (chip de
/// 24, ícone de 18) e uma grade de 8 obrigaria a usar metade dela o tempo
/// todo, que é o mesmo que não ter grade.
abstract final class AureaSpacing {
  static const double x1 = 4;
  static const double x2 = 8;
  static const double x3 = 12;
  static const double x4 = 16;
  static const double x5 = 24;
  static const double x6 = 32;

  /// Alvo de toque mínimo (regra de acessibilidade).
  static const double minTap = 44;

  /// Alturas das zonas fixas do editor.
  static const double topBar = 44;
  static const double transport = 46;

  /// Régua de arrasto e tile de categoria.
  static const double ruler = 52;
  static const double tile = 56;
}

/// ==========================================================================
/// RAIOS
/// ==========================================================================
abstract final class AureaRadius {
  static const double chip = 10;
  static const double card = 12;
  static const double sheet = 18;
  static const double pill = 999;

  static const BorderRadius chipAll = BorderRadius.all(Radius.circular(chip));
  static const BorderRadius cardAll = BorderRadius.all(Radius.circular(card));
  static const BorderRadius sheetTop = BorderRadius.vertical(
    top: Radius.circular(sheet),
  );
  static const BorderRadius pillAll = BorderRadius.all(Radius.circular(pill));
}

/// ==========================================================================
/// SOMBRAS
/// ==========================================================================
///
/// No escuro, sombra quase não se vê: o que separa um painel do fundo é o
/// TOM da superfície e a linha da borda. As sombras existem para o que
/// FLUTUA (folha, popover, diálogo), onde há conteúdo por baixo para
/// escurecer.
abstract final class AureaShadows {
  static const List<BoxShadow> sheet = [
    BoxShadow(color: Color(0x66000000), blurRadius: 28, offset: Offset(0, -6)),
  ];
  static const List<BoxShadow> popover = [
    BoxShadow(color: Color(0x73000000), blurRadius: 20, offset: Offset(0, 8)),
  ];
  static const List<BoxShadow> none = [];
}

/// ==========================================================================
/// TIPOGRAFIA — o estilo SF do iOS.
/// ==========================================================================
///
/// O tracking é negativo e CRESCE com o corpo, que é o que faz texto grande
/// parecer desenhado e não esticado. Os tamanhos são os mesmos que a Fase 6
/// do redesign fixou; o que muda aqui é haver UM lugar que os diz.
abstract final class AureaTypography {
  static const TextStyle display = TextStyle(
    fontSize: 34,
    fontWeight: FontWeight.w700,
    letterSpacing: -0.8,
    height: 1.1,
  );
  static const TextStyle title = TextStyle(
    fontSize: 22,
    fontWeight: FontWeight.w700,
    letterSpacing: -0.5,
  );
  static const TextStyle headline = TextStyle(
    fontSize: 17,
    fontWeight: FontWeight.w600,
    letterSpacing: -0.3,
  );
  static const TextStyle body = TextStyle(fontSize: 15, letterSpacing: -0.2);
  static const TextStyle label = TextStyle(
    fontSize: 12,
    fontWeight: FontWeight.w600,
    letterSpacing: 0.2,
  );
  static const TextStyle caption = TextStyle(fontSize: 11, letterSpacing: 0.1);

  /// O NÚMERO DE UM CAMPO. Tabular para os dígitos não dançarem enquanto o
  /// valor muda — em campo de tempo isso é a diferença entre ler e adivinhar.
  static const TextStyle numeric = TextStyle(
    fontSize: 13,
    fontWeight: FontWeight.w600,
    fontFeatures: [FontFeature.tabularFigures()],
    letterSpacing: 0,
  );
}
