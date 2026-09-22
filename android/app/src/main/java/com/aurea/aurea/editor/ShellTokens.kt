package com.aurea.aurea.editor

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.aurea.aurea.ui.theme.AureaColors

/**
 * Tokens que SÓ a casca do editor usa (Beta A.01, `cromo_editor.dart`,
 * `layer_menu.dart`, `add_layer_sheet.dart` e `editor_screen.dart` @aba36bb).
 *
 * Moram aqui, e não em `ui/theme`, porque são peças de uma tela só (a doca,
 * o "+", o chip de resolução): no tema global viravam ruído e conflito entre
 * as áreas portadas em paralelo. A regra continua a mesma: nenhuma cor solta
 * nos composables da casca.
 */
internal object ShellColors {
    /** `CromoEditor.apagado`: o tempo da barra do projeto (branco 40 %). */
    val White40 = Color(0x66FFFFFF)
    val DockRow = Color(0xFF1E222D)
    val DockTile = Color(0xFF222634)
    val DockTileContent = Color(0xFFD4D8E2)
    val BadgeNew = Color(0xFFFFD600)
    val Fab = Color(0xFF1E2130)
    val FabShadow = Color(0x73000000)          // Colors.black45
    val FloatingDark = Color(0xCC12151A)       // "Voltar ao editor" e o HUD
    val ResolutionChip = Color(0xCC171D25)
    val OutlineUnder = Color(0x8C0A0E13)       // #0A0E13 a 55 %
    val SnapLine = Color(0xCCFF6B6B)
    val BusyVeil = Color(0xDD17191D)
    val ObjectCard = Color(0xFF0D0E12)
    val ShapeTile = Color(0xFF000000)
    val ShapeFill = Color(0xFF9E9E9E)
    val PageDotOff = Color(0xFF484E5C)
    val Camera3D = Color(0xFF8BD5FF)
    val Text3D = Color(0xFFFFD36B)
    val Phone3D = Color(0xFFC9CDD4)
    val Track22 = Color(0x38FFFFFF)            // trilho da barra de tempo (branco 22 %)
    val MenuHandle = Color(0x66AAB6C3)         // puxador da folha de menu (muted 40 %)
    val SheetHandle = Color(0x80AAB6C3)        // puxador da folha de ajustes (muted 50 %)
    val MenuScrim = Color(0x8A000000)          // véu padrão da folha Material (54 %)
    val SettingsScrim = Color(0x61000000)      // Colors.black38 da folha de ajustes
    val PopupScrim = Color(0x400A0E13)         // véu do menu flutuante (#0A0E13 25 %)
    val DisabledMuted = Color(0x99AAB6C3)      // item de menu apagado (muted 60 %)
    val Swatch = Color(0x3DFFFFFF)             // borda da amostra de cor (white24)
    val NavigationBar = Color(0xFF000000)      // faixa da barra de navegação (print)
    val AccentHalf = Color(0x806FAED9)         // borda da faixa do cadeado (destaque 50 %)

    /** Etiquetas de camada (`LayerLabel.palette` @aba36bb). */
    val LabelPalette = listOf(
        Color(0xFFE85B81), Color(0xFFFFB020), AureaColors.Accent, Color(0xFF2BE3A0),
        Color(0xFF35C4E7), AureaColors.Keyframe, Color(0xFF3D7BFF), Color(0xFFFF7A3D),
        Color(0xFFFF4D5E), Color(0xFFFFE14D), Color(0xFFB0B8C4), Color(0xFF4A5160),
    )
}

/** Medidas da casca A.01 (`EditorLayoutMetrics`, `CromoEditor`, `ContextSheet`). */
internal object ShellDims {
    val TopBar = 44.dp
    val Transport = 46.dp
    val Strip = 8.dp
    val SheetHandle = 12.dp          // faixa vazia do ContextSheet sem título
    val FullscreenTimeBar = 44.dp
    val StageInset = 8.dp            // compositionRect: min(8, lado/4)
    val Fab = 52.dp
    val FabMargin = 18.dp
    val TouchSlop = 18.dp            // kTouchSlop do Flutter: tocar nunca move
    val HandleSlop = 4.dp
    val SnapTolerance = 10.dp
    val HitSlack = 12.dp
    val ScaleHandleTarget = 26.dp
    val RotateHandleTarget = 22.dp
}

/**
 * Glifos Cupertino que só a casca usa (codepoints do `icons.dart` do Flutter,
 * mesma fonte `CupertinoIcons.ttf` de [com.aurea.aurea.ui.theme.CupertinoGlyph]).
 */
internal object ShellGlyph {
    const val SquareOnCircle = '\uF80C'
    const val CircleGridHex = '\uF5EE'
    const val Scribble = '\uF7CB'
    const val SliderHorizontalBelowRectangle = '\uF7DD'
    const val CircleGrid3x3 = '\uF5EC'
    const val Viewfinder = '\uF88D'
    const val SquareSplit2x2 = '\uF813'
    const val PaintbrushFill = '\uF72F'
    const val RectangleArrowUpRightArrowDownLeft = '\uF79E'
    const val ArrowUpLeftArrowDownRight = '\uF386'
    const val Nosign = '\uF727'
    const val FolderBadgePlus = '\uF678'
    const val SquareGrid3x2 = '\uF806'
    const val LockOpenFill = '\uF6FB'
    const val Snow = '\uF7E7'
    const val BookmarkSolid = '\uF3EA'
    const val Metronome = '\uF70B'
    const val ScissorsAlt = '\uF905'
    const val WandRaysInverse = '\uF891'
    const val PauseCircle = '\uF736'
    const val PlayCircle = '\uF76F'
    const val SquareFill = '\uF7FF'
    const val SquareStack3dDownDottedline = '\uF816'
    const val RectangleDock = '\uF7A3'
    const val WaveformPathEcg = '\uF89A'
    const val Table = '\uF844'
    const val TextformatAlt = '\uF860'
}
