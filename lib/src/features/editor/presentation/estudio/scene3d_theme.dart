import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/material.dart';

/// Design system e paleta de cores para a nova UI do Scene 3D.
abstract final class Scene3DTheme {
  static final ThemeData theme = ThemeData(
    brightness: Brightness.dark,
    colorScheme: ColorScheme.fromSeed(
      seedColor: accent,
      brightness: Brightness.dark,
    ).copyWith(primary: accent, surface: panel, onPrimary: onAccent),
    scaffoldBackgroundColor: bg,
    chipTheme: const ChipThemeData(
      selectedColor: accentDim,
      backgroundColor: panel,
    ),
  );
  // Paleta de cores principais
  static const Color bg = Color(0xFF0B0F15);
  static const Color panel = Color(0xFF161B22);
  static const Color panelElevated = Color(0xFF1C2128);
  static const Color card = Color(0xFF1F242C);
  static const Color border = Color(0xFF2D333B);
  static const Color borderLight = Color(0xFF373E47);

  // Acentos
  static const Color accent = Color(0xFF27E38C);
  static const Color accentDim = Color(0xFF153F2B);
  static const Color onAccent = Color(0xFF071B11);

  // Eixos 3D
  static const Color axisX = Color(0xFFEF4444); // Vermelho
  static const Color axisY = Color(0xFF22C55E); // Verde
  static const Color axisZ = Color(0xFF3B82F6); // Azul

  // Tipografia e tons neutros
  static const Color text = Color(0xFFFFFFFF);
  static const Color textMuted = Color(0xFF8B949E);
  static const Color textSubtle = Color(0xFF6E7681);
  static const Color icon = Color(0xFF9DA7B3);

  // Decorações padrão
  static BoxDecoration cardDecoration({
    Color? color,
    Color? borderColor,
    double borderRadius = 16,
    bool isSelected = false,
  }) => BoxDecoration(
    color: color ?? panelElevated,
    borderRadius: BorderRadius.circular(borderRadius),
    border: Border.all(
      color: isSelected ? accent : (borderColor ?? border),
      width: isSelected ? 1.5 : 1.0,
    ),
  );

  static BoxDecoration pillDecoration({
    required bool active,
    Color? activeColor,
    Color? inactiveColor,
  }) => BoxDecoration(
    color: active ? (activeColor ?? accent) : (inactiveColor ?? panelElevated),
    borderRadius: BorderRadius.circular(20),
    border: Border.all(
      color: active ? (activeColor ?? accent) : border,
      width: 1,
    ),
  );
}

/// Cabeçalho estilizado para as folhas modais do Scene 3D
class Scene3DSheetHeader extends StatelessWidget {
  const Scene3DSheetHeader({
    super.key,
    required this.title,
    this.onBack,
    this.onClose,
    this.trailing,
  });

  final String title;
  final VoidCallback? onBack;
  final VoidCallback? onClose;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
      child: Row(
        children: [
          if (onBack != null) ...[
            GestureDetector(
              behavior: HitTestBehavior.opaque,
              onTap: onBack,
              child: const SizedBox(
                width: 36,
                height: 36,
                child: Icon(
                  Icons.chevron_left_rounded,
                  color: Scene3DTheme.text,
                  size: 26,
                ),
              ),
            ),
            const SizedBox(width: 4),
          ],
          Expanded(
            child: AppText(title,
              maxLines: 2,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(
                fontSize: 18,
                fontWeight: FontWeight.w700,
                color: Scene3DTheme.text,
                letterSpacing: -0.2,
              ),
            ),
          ),
          ?trailing,
          if (onClose != null)
            GestureDetector(
              behavior: HitTestBehavior.opaque,
              onTap: onClose,
              child: Container(
                width: 32,
                height: 32,
                decoration: BoxDecoration(
                  color: Scene3DTheme.panelElevated,
                  shape: BoxShape.circle,
                  border: Border.all(color: Scene3DTheme.border),
                ),
                child: const Icon(
                  Icons.close_rounded,
                  color: Scene3DTheme.textMuted,
                  size: 18,
                ),
              ),
            ),
        ],
      ),
    );
  }
}

/// Botão de ação principal verde neon
class Scene3DActionButton extends StatelessWidget {
  const Scene3DActionButton({
    super.key,
    required this.label,
    this.icon,
    required this.onPressed,
    this.outlined = false,
  });

  final String label;
  final IconData? icon;
  final VoidCallback? onPressed;
  final bool outlined;

  @override
  Widget build(BuildContext context) {
    final enabled = onPressed != null;
    return SizedBox(
      height: 48,
      child: ElevatedButton(
        style: ElevatedButton.styleFrom(
          backgroundColor: outlined ? Colors.transparent : Scene3DTheme.accent,
          foregroundColor: outlined ? Scene3DTheme.text : Scene3DTheme.onAccent,
          elevation: 0,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(14),
            side: outlined
                ? const BorderSide(color: Scene3DTheme.border, width: 1.2)
                : BorderSide.none,
          ),
          padding: const EdgeInsets.symmetric(horizontal: 20),
        ),
        onPressed: onPressed,
        child: Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            if (icon != null) ...[
              Icon(
                icon,
                size: 19,
                color: outlined ? Scene3DTheme.text : Scene3DTheme.onAccent,
              ),
              const SizedBox(width: 8),
            ],
            AppText(
              label,
              style: TextStyle(
                fontSize: 15,
                fontWeight: FontWeight.w700,
                color: enabled
                    ? (outlined ? Scene3DTheme.text : Scene3DTheme.onAccent)
                    : Scene3DTheme.textMuted,
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// Switch estilizado verde neon para o Scene 3D
class Scene3DSwitch extends StatelessWidget {
  const Scene3DSwitch({
    super.key,
    required this.value,
    required this.onChanged,
  });

  final bool value;
  final ValueChanged<bool> onChanged;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: () => onChanged(!value),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 200),
        curve: Curves.easeInOut,
        width: 44,
        height: 24,
        padding: const EdgeInsets.all(2),
        decoration: BoxDecoration(
          borderRadius: BorderRadius.circular(14),
          color: value ? Scene3DTheme.accent : const Color(0xFF242A35),
          border: Border.all(
            color: value ? Scene3DTheme.accent : Scene3DTheme.borderLight,
            width: 1,
          ),
        ),
        child: Align(
          alignment: value ? Alignment.centerRight : Alignment.centerLeft,
          child: Container(
            width: 18,
            height: 18,
            decoration: const BoxDecoration(
              shape: BoxShape.circle,
              color: Colors.white,
            ),
          ),
        ),
      ),
    );
  }
}
