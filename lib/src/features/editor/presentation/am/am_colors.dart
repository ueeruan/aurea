import 'dart:ui';

/// Flat, low-contrast editing surfaces and teal selection, matching the
/// reference workflow. Branding on project/home screens is independent.
abstract final class AmColors {
  static const Color bg = Color(0xFF191A1C);
  static const Color topBar = Color(0xFF202123);
  static const Color panel = Color(0xFF17181A);
  static const Color panelHigh = Color(0xFF25262B);
  static const Color chip = Color(0xFF292B33);

  /// Seleção e ações do editor.
  static const Color accent = Color(0xFF1ED6B1);
  static const Color accentDim = Color(0xFF183F3C);

  /// Barras de camada com contraste para texto e keyframes.
  static const Color teal = Color(0xFF43B7C6);
  static const Color tealBright = Color(0xFF81D8E0);

  /// Playhead em contexto de keyframe/efeito.
  static const Color pink = Color(0xFFFF6B6B);

  static const Color text = Color(0xFFE9EDF2);
  static const Color muted = Color(0xFF8B94A3);
  static const Color hairline = Color(0x14FFFFFF);
}
