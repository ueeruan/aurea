import 'package:flutter/widgets.dart';

import 'aurea_colors.dart';
import 'aurea_paleta.dart';

/// OS TOKENS DO DESIGN SYSTEM (Fase 1 do redesign).
///
/// Uma paleta escura e uma clara com os MESMOS nomes: quem desenha com
/// tokens nunca escolhe um hex, escolhe um papel (fundo, superficie,
/// acao, keyframe, selecao). O palco de preview continua escuro nos dois
/// temas — video se avalia sobre fundo escuro.
///
/// OS HEXES MORAOM EM [AureaColors], E SÓ LÁ. Este arquivo dá os mesmos
/// valores por dois caminhos porque a base foi escrita em duas épocas: os
/// widgets novos leem `AureaTokens.of(context).accent` (que respeita o tema
/// em vigor) e os antigos leem `AmColors.action` (que é fixo). Os dois agora
/// apontam para a mesma tabela, e é isso que impede o editor e o resto do
/// app de se separarem de novo.
///
/// Papéis da marca azul:
///   acao      azul claro — o que a pessoa toca para fazer algo: Exportar,
///                          o "+", chips ativos, botao principal
///   keyframe  azul suave — diamantes, curvas, o cabecote em contexto
///   selecao   azul medio — fundo da camada selecionada e dos grupos
class AureaTokens {
  const AureaTokens({
    required this.brightness,
    required this.bg,
    required this.surface,
    required this.surfaceHigh,
    required this.chip,
    required this.text,
    required this.muted,
    required this.hairline,
    required this.accent,
    required this.onAccent,
    required this.accentDim,
    required this.keyframe,
    required this.keyframeDim,
    required this.selection,
    required this.danger,
  });

  final Brightness brightness;

  /// Fundo da tela.
  final Color bg;

  /// Superficies (barras, folhas, timeline).
  final Color surface;
  final Color surfaceHigh;

  /// Fundo de chip/tile.
  final Color chip;
  final Color text;
  final Color muted;
  final Color hairline;

  /// Acao (o azul claro da marca).
  final Color accent;
  final Color onAccent;
  final Color accentDim;

  /// Keyframe, curva, cabecote em contexto (azul suave).
  final Color keyframe;
  final Color keyframeDim;

  /// Selecao e grupos (azul medio).
  final Color selection;

  /// Excluir, erro.
  final Color danger;

  bool get isDark => brightness == Brightness.dark;

  static const dark = AureaTokens(
    brightness: Brightness.dark,
    bg: AureaColors.bg,
    surface: AureaColors.surface,
    surfaceHigh: AureaColors.surfaceHigh,
    chip: AureaColors.chip,
    text: AureaColors.text,
    muted: AureaColors.muted,
    hairline: AureaColors.border,
    accent: AureaColors.accent,
    onAccent: AureaColors.onAccent,
    accentDim: AureaColors.accentDim,
    keyframe: AureaColors.keyframe,
    keyframeDim: AureaColors.keyframeDim,
    selection: AureaColors.selection,
    danger: AureaColors.danger,
  );

  /// OS TOKENS DO EDITOR E DO ESTUDIO: a paleta em vigor, na versao que o
  /// editor usa ([AureaPaleta.editor] — escura mesmo sob o tema claro).
  ///
  /// Era `const motion = dark`. Virou getter para o unico `AureaTheme(` do
  /// app (no editor) acompanhar o tema sem que ninguem precise trocar a
  /// chamada.
  static AureaTokens get motion => dePaleta(AureaPaleta.ativa.editor);

  /// Os tokens de uma paleta.
  ///
  /// GUARDADOS POR TEMA, e nao montados a cada chamada: [AureaTheme] avisa
  /// os dependentes quando `tokens` muda, e uma instancia nova por `build`
  /// faria o editor inteiro se reconstruir a cada quadro por nada.
  ///
  /// `selection` e [AureaPaleta.primary] (o azul de PREENCHIMENTO), que e o
  /// que `AureaTokens.dark.selection` sempre foi; o fundo de camada
  /// selecionada do cromo antigo (`AmColors.selection`) e outro papel,
  /// [AureaPaleta.selected].
  static AureaTokens dePaleta(AureaPaleta p) {
    if (p.id == AureaTemaId.aurea) return dark;
    return _porTema[p.id] ??= AureaTokens(
      brightness: p.brightness,
      bg: p.background,
      surface: p.surface,
      surfaceHigh: p.panel,
      chip: p.chip,
      text: p.textPrimary,
      muted: p.textSecondary,
      hairline: p.divider,
      accent: p.accent,
      onAccent: p.onAccent,
      accentDim: p.accentDim,
      keyframe: p.keyframe,
      keyframeDim: p.keyframeDim,
      selection: p.primary,
      danger: p.danger,
    );
  }

  static final Map<AureaTemaId, AureaTokens> _porTema = {};

  static const light = AureaTokens(
    brightness: Brightness.light,
    bg: AureaColors.lightBg,
    surface: AureaColors.lightSurface,
    surfaceHigh: AureaColors.lightSurfaceHigh,
    chip: AureaColors.lightChip,
    text: AureaColors.lightText,
    muted: AureaColors.lightMuted,
    hairline: AureaColors.lightBorder,
    accent: AureaColors.lightAccent,
    onAccent: AureaColors.lightOnAccent,
    accentDim: AureaColors.lightAccentDim,
    keyframe: AureaColors.lightKeyframe,
    keyframeDim: AureaColors.lightKeyframeDim,
    selection: AureaColors.lightSelection,
    danger: AureaColors.lightDanger,
  );

  /// Os tokens em vigor neste ponto da arvore.
  ///
  /// SEM ANCESTRAL, OS DO EDITOR ([motion]), e nao `dark` fixo: as folhas do
  /// editor abrem pelo Navigator, FORA do `AureaTheme` da tela, e pintariam
  /// o tema Aurea por cima de um editor Midnight.
  static AureaTokens of(BuildContext context) =>
      context.dependOnInheritedWidgetOfExactType<AureaTheme>()?.tokens ??
      motion;

  // ----------------------------------------------------- medidas
  //
  // AS MEDIDAS MORAOM EM [AureaSpacing] E [AureaRadius]. Estes apelidos
  // ficam porque centenas de chamadas ja escrevem `AureaTokens.s4`, e
  // renomear chamada nao muda pixel nenhum — so o risco de errar uma.

  /// Grade de 4 pt.
  static const double s1 = AureaSpacing.x1;
  static const double s2 = AureaSpacing.x2;
  static const double s3 = AureaSpacing.x3;
  static const double s4 = AureaSpacing.x4;
  static const double s5 = AureaSpacing.x5;

  /// Alvo de toque minimo (regra 6 do prompt).
  static const double minTap = AureaSpacing.minTap;

  /// Alturas das zonas fixas.
  // As barras do editor na medida do AM 5: navbar 44, playbar 46.
  static const double topBar = AureaSpacing.topBar;
  static const double transport = AureaSpacing.transport;

  /// Regua de arrasto e tile de categoria.
  static const double ruler = AureaSpacing.ruler;
  static const double tile = AureaSpacing.tile;

  static const double radius = AureaRadius.card;
  static const double radiusChip = AureaRadius.chip;
}

/// Entrega os tokens a subarvore.
class AureaTheme extends InheritedWidget {
  const AureaTheme({super.key, required this.tokens, required super.child});

  final AureaTokens tokens;

  @override
  bool updateShouldNotify(AureaTheme old) => old.tokens != tokens;
}
