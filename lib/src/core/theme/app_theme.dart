import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';

import 'aurea_colors.dart';

/// A PALETA DO APP — um apelido para [AureaColors], e nada mais.
///
/// ESTE ARQUIVO JA GUARDOU HEXADECIMAIS. Ele era uma das três listas da
/// marca (as outras eram `AmColors` e `AureaTokens`), com o verde-lima
/// escrito à mão em cada uma. Agora ele só TRADUZ nome antigo para papel
/// novo, e o hexadecimal existe num lugar só.
///
/// OS NOMES ANTIGOS FICARAM DE PROPOSITO. `lime` e `violet` descrevem a
/// cor de 2025, e há mais de cem lugares no app escritos assim. Renomeá-los
/// seria uma tarde de substituição mecânica com risco de errar uma chamada,
/// em troca de nada que a pessoa que usa o app perceba. O comentário de cada
/// um diz o papel — que é o que importa quando a identidade mudar de novo.
///
/// TEMA CLARO (Fase 6, decisao Q6): os mesmos NOMES trocam de valor com
/// [modoClaro]. Quem pintava com AppColors continua pintando com o papel
/// certo — fundo, superficie, texto — e o app inteiro (fora do editor,
/// que fica escuro por ser palco de video) acompanha o ajuste.
abstract final class AppColors {
  /// Ligado pelo tema em vigor (ver [AppTheme.tema]).
  static bool modoClaro = false;

  static Color get background => modoClaro ? AureaColors.lightBg : AureaColors.bg;
  static Color get surface =>
      modoClaro ? AureaColors.lightSurface : AureaColors.surface;
  static Color get surfaceHigh =>
      modoClaro ? AureaColors.lightSurfaceHigh : AureaColors.surfaceHigh;

  /// AÇÃO. Era o lima da logo; hoje é o azul claro dela.
  static Color get lime =>
      modoClaro ? AureaColors.lightAccent : AureaColors.accent;

  /// SELEÇÃO. Era o violeta; hoje é o azul suave.
  static Color get violet =>
      modoClaro ? AureaColors.lightSelection : AureaColors.selectionText;

  static Color get onDark =>
      modoClaro ? AureaColors.lightText : AureaColors.text;
  static Color get muted =>
      modoClaro ? AureaColors.lightMuted : AureaColors.muted;
  static Color get outline =>
      modoClaro ? AureaColors.lightBorder : AureaColors.border;

  /// A ação APAGADA, para fundo de chip aceso — o mesmo papel que
  /// `AmColors.accentDim` faz no editor.
  static Color get accentDim =>
      modoClaro ? AureaColors.lightAccentDim : AureaColors.accentDim;

  /// Linha fina estilo iOS (separadores e borda do chrome translucido).
  ///
  /// UMA LINHA FINA É MEIA TRANSPARÊNCIA, e não a cor da borda cheia: ela
  /// passa por cima de fundo, de superfície e de chip, e a #273442 sólida
  /// ficaria pesada sobre o fundo mais escuro. A 42% ela rende os três.
  static Color get hairline => modoClaro
      ? Colors.black.withValues(alpha: 0.10)
      : AureaColors.border.withValues(alpha: 0.72);
}

abstract final class AppTheme {
  static const Color timelineBackground = AureaColors.surface;

  static ThemeData get dark => tema(claro: false);
  static ThemeData get light => tema(claro: true);

  /// O tema em vigor. Liga [AppColors.modoClaro] antes de montar, para
  /// as cores fixas das telas de Projetos e Ajustes acompanharem.
  static ThemeData tema({required bool claro}) {
    AppColors.modoClaro = claro;
    final scheme = ColorScheme(
      brightness: claro ? Brightness.light : Brightness.dark,
      primary: AppColors.lime,
      onPrimary: AureaColors.onAccent,
      secondary: AppColors.violet,
      onSecondary: Colors.white,
      error: AureaColors.danger,
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
      highlightColor: (claro ? Colors.black : Colors.white).withValues(
        alpha: 0.05,
      ),

      // Navegacao com a fisica/transicao do iOS em todas as plataformas.
      pageTransitionsTheme: const PageTransitionsTheme(
        builders: {
          TargetPlatform.android: CupertinoPageTransitionsBuilder(),
          TargetPlatform.iOS: CupertinoPageTransitionsBuilder(),
        },
      ),
      cupertinoOverrideTheme: CupertinoThemeData(
        brightness: claro ? Brightness.light : Brightness.dark,
        primaryColor: AppColors.lime,
      ),

      // Tipografia estilo SF: tracking negativo cresce junto com o corpo.
      textTheme: TextTheme(
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

      appBarTheme: AppBarTheme(
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
          foregroundColor: AureaColors.onAccent,
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
          borderSide: BorderSide(color: AppColors.lime),
        ),
      ),
      dividerTheme: DividerThemeData(
        color: AppColors.hairline,
        thickness: 0.5,
        space: 0.5,
      ),
      snackBarTheme: SnackBarThemeData(
        backgroundColor: AppColors.surfaceHigh,
        contentTextStyle: TextStyle(color: AppColors.onDark),
        behavior: SnackBarBehavior.floating,
      ),
    );
  }
}
