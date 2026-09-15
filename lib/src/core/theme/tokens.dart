import 'package:flutter/widgets.dart';

/// OS TOKENS DO DESIGN SYSTEM (Fase 1 do redesign).
///
/// Uma paleta escura e uma clara com os MESMOS nomes: quem desenha com
/// tokens nunca escolhe um hex, escolhe um papel (fundo, superficie,
/// acao, keyframe, selecao). O palco de preview continua escuro nos dois
/// temas — video se avalia sobre fundo escuro.
///
/// Papeis das tres cores da marca:
///   acao      lima    — o que a pessoa toca para fazer algo: Exportar,
///                       o "+", chips ativos, botao principal
///   keyframe  teal    — diamantes, curvas, o cabecote em contexto
///   selecao   violeta — a camada selecionada e os grupos
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

  /// Acao (lima da logo).
  final Color accent;
  final Color onAccent;
  final Color accentDim;

  /// Keyframe, curva, cabecote em contexto (teal).
  final Color keyframe;
  final Color keyframeDim;

  /// Selecao e grupos (violeta da logo).
  final Color selection;

  /// Excluir, erro.
  final Color danger;

  bool get isDark => brightness == Brightness.dark;

  static const dark = AureaTokens(
    brightness: Brightness.dark,
    bg: Color(0xFF12151A),
    surface: Color(0xFF171C23),
    surfaceHigh: Color(0xFF1E242E),
    chip: Color(0xFF262C36),
    text: Color(0xFFE9EDF2),
    muted: Color(0xFF8B94A3),
    hairline: Color(0x14FFFFFF),
    accent: Color(0xFFB8FF3D),
    onAccent: Color(0xFF0B0E12),
    accentDim: Color(0xFF2A3A16),
    keyframe: Color(0xFF1ED6B1),
    keyframeDim: Color(0xFF183F3C),
    selection: Color(0xFF7C62FF),
    danger: Color(0xFFFF6B6B),
  );

  /// Editor and studio retain the Aurea brand palette.
  static const motion = dark;

  static const light = AureaTokens(
    brightness: Brightness.light,
    bg: Color(0xFFF4F5F7),
    surface: Color(0xFFFFFFFF),
    surfaceHigh: Color(0xFFEDEFF3),
    chip: Color(0xFFE4E7EC),
    text: Color(0xFF14171C),
    muted: Color(0xFF6B7280),
    hairline: Color(0x14000000),
    accent: Color(0xFF7BC300),
    onAccent: Color(0xFFFFFFFF),
    accentDim: Color(0xFFE3F5C2),
    keyframe: Color(0xFF0FA88C),
    keyframeDim: Color(0xFFCDEFE7),
    selection: Color(0xFF6A4FF0),
    danger: Color(0xFFD94B4B),
  );

  /// Os tokens em vigor neste ponto da arvore (escuro por padrao).
  static AureaTokens of(BuildContext context) =>
      context.dependOnInheritedWidgetOfExactType<AureaTheme>()?.tokens ?? dark;

  // ----------------------------------------------------- medidas

  /// Grade de 8 pt.
  static const double s1 = 4;
  static const double s2 = 8;
  static const double s3 = 12;
  static const double s4 = 16;
  static const double s5 = 24;

  /// Alvo de toque minimo (regra 6 do prompt).
  static const double minTap = 44;

  /// Alturas das zonas fixas.
  // As barras do editor na medida do AM 5: navbar 44, playbar 46.
  static const double topBar = 44;
  static const double transport = 46;

  /// Regua de arrasto e tile de categoria.
  static const double ruler = 52;
  static const double tile = 56;

  static const double radius = 12;
  static const double radiusChip = 10;
}

/// Entrega os tokens a subarvore.
class AureaTheme extends InheritedWidget {
  const AureaTheme({super.key, required this.tokens, required super.child});

  final AureaTokens tokens;

  @override
  bool updateShouldNotify(AureaTheme old) => old.tokens != tokens;
}
