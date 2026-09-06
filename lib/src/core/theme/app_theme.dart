import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

/// Paleta extraida do logo: fundo grafite, verde-lima e violeta.
abstract final class AppColors {
  static const Color background = Color(0xFF12151A);
  static const Color surface = Color(0xFF171C23);
  static const Color surfaceHigh = Color(0xFF1E242E);
  static const Color lime = Color(0xFFB8FF3D);
  static const Color violet = Color(0xFF7C62FF);
  static const Color onDark = Color(0xFFE9EDF2);
  static const Color muted = Color(0xFF8B94A3);
  static const Color outline = Color(0xFF2A313C);

  /// O verde da marca APAGADO, para fundo de chip aceso — o mesmo papel
  /// que `AmColors.accentDim` faz no editor.
  static const Color accentDim = Color(0xFF2A3A16);

  /// Linha fina estilo iOS (separadores e borda do chrome translucido).
  static final Color hairline = Colors.white.withValues(alpha: 0.08);
}

abstract final class AppTheme {
  static const Color timelineBackground = Color(0xFF171C23);

  static ThemeData get dark {
    const scheme = ColorScheme(
      brightness: Brightness.dark,
      primary: AppColors.lime,
      onPrimary: Color(0xFF0B0E12),
      secondary: AppColors.violet,
      onSecondary: Colors.white,
      error: Color(0xFFFF6B6B),
      onError: Colors.white,
      surface: AppColors.background,
      onSurface: AppColors.onDark,
      surfaceContainerHighest: AppColors.surfaceHigh,
      surfaceContainerHigh: AppColors.surfaceHigh,
      surfaceContainer: AppColors.surface,
      surfaceContainerLow: AppColors.surface,
      onSurfaceVariant: AppColors.muted,
      outline: AppColors.outline,
      outlineVariant: AppColors.outline,
    );

    return ThemeData(
      useMaterial3: true,
      colorScheme: scheme,
      scaffoldBackgroundColor: AppColors.background,

      // Feedback estilo iOS: sem ripple do Material, realce sutil no toque.
      splashFactory: NoSplash.splashFactory,
      splashColor: Colors.transparent,
      hoverColor: Colors.transparent,
      highlightColor: Colors.white.withValues(alpha: 0.05),

      // Navegacao com a fisica/transicao do iOS em todas as plataformas.
      pageTransitionsTheme: const PageTransitionsTheme(builders: {
        TargetPlatform.android: CupertinoPageTransitionsBuilder(),
        TargetPlatform.iOS: CupertinoPageTransitionsBuilder(),
      }),
      cupertinoOverrideTheme: const CupertinoThemeData(
        brightness: Brightness.dark,
        primaryColor: AppColors.lime,
      ),

      // Tipografia estilo SF: tracking negativo cresce junto com o corpo.
      textTheme: const TextTheme(
        headlineLarge: TextStyle(
          fontSize: 34,
          fontWeight: FontWeight.w700,
          letterSpacing: -0.8,
          height: 1.1,
          color: AppColors.onDark,
        ),
        titleLarge: TextStyle(
          fontSize: 22,
          fontWeight: FontWeight.w700,
          letterSpacing: -0.5,
          color: AppColors.onDark,
        ),
        titleMedium: TextStyle(
          fontSize: 17,
          fontWeight: FontWeight.w600,
          letterSpacing: -0.3,
          color: AppColors.onDark,
        ),
        bodyLarge: TextStyle(
          fontSize: 17,
          letterSpacing: -0.2,
          color: AppColors.onDark,
        ),
        bodyMedium: TextStyle(
          fontSize: 15,
          letterSpacing: -0.1,
          height: 1.35,
          color: AppColors.onDark,
        ),
        bodySmall: TextStyle(
          fontSize: 13,
          letterSpacing: 0,
          color: AppColors.muted,
        ),
        labelLarge: TextStyle(
          fontSize: 17,
          fontWeight: FontWeight.w600,
          letterSpacing: -0.2,
        ),
      ),

      appBarTheme: const AppBarTheme(
        backgroundColor: AppColors.background,
        foregroundColor: AppColors.onDark,
        elevation: 0,
        centerTitle: true,
        titleTextStyle: TextStyle(
          fontSize: 17,
          fontWeight: FontWeight.w600,
          letterSpacing: -0.3,
          color: AppColors.onDark,
        ),
      ),
      filledButtonTheme: FilledButtonThemeData(
        style: FilledButton.styleFrom(
          backgroundColor: AppColors.lime,
          foregroundColor: const Color(0xFF0B0E12),
          textStyle: const TextStyle(
            fontSize: 17,
            fontWeight: FontWeight.w600,
            letterSpacing: -0.2,
          ),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(14),
          ),
          padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 14),
        ),
      ),
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: AppColors.surfaceHigh,
        border: OutlineInputBorder(
          borderRadius: BorderRadius.circular(12),
          borderSide: BorderSide.none,
        ),
        focusedBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(12),
          borderSide: const BorderSide(color: AppColors.lime),
        ),
      ),
      dividerTheme: DividerThemeData(
        color: AppColors.hairline,
        thickness: 0.5,
        space: 0.5,
      ),
      snackBarTheme: const SnackBarThemeData(
        backgroundColor: AppColors.surfaceHigh,
        contentTextStyle: TextStyle(color: AppColors.onDark),
        behavior: SnackBarBehavior.floating,
      ),
    );
  }
}
