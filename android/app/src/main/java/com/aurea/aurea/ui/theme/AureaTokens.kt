package com.aurea.aurea.ui.theme

import androidx.compose.animation.core.CubicBezierEasing
import androidx.compose.animation.core.Easing
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.PlatformTextStyle
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.LineHeightStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp

/**
 * Tokens da UI aprovada (Beta A.01 do Aurea antigo).
 *
 * Os valores vêm do código Flutter da release A.01 (`aurea_colors.dart`,
 * `am_colors.dart`, `app_theme.dart`) e foram conferidos pixel a pixel nos
 * prints aprovados (`docs/migration/ui_reference/`). O aparelho de referência
 * tem densidade 2,625: 1 dp = 2,625 px.
 *
 * REGRA: nenhuma tela escreve cor/medida solta. Se falta um token, ele entra
 * aqui — é o que impede a UI de derivar do visual aprovado aos poucos.
 */
object AureaColors {
    // --- Marca ---------------------------------------------------------------
    val BrandDeep = Color(0xFF123A63)
    val Brand = Color(0xFF245D8C)          // preenchimento de ação (Exportar, "+")
    val Accent = Color(0xFF6FAED9)         // ação/estado fora do editor, destaque
    val Keyframe = Color(0xFFA9D3EC)       // keyframe, curva, valor, seleção em texto
    val Background = Color(0xFF0F141A)
    val Surface = Color(0xFF151C24)
    val SurfaceHigh = Color(0xFF1B2530)
    val Chip = Color(0xFF212D3A)
    val ChipHigh = Color(0xFF323D49)       // campoAlto = lerp(chip, texto, 0,08)
    val Border = Color(0xFF273442)
    val Text = Color(0xFFF7F9FB)
    val Muted = Color(0xFFAAB6C3)
    val OnAccent = Color(0xFF0B1117)
    val AccentDim = Color(0xFF1D3A55)
    val KeyframeDim = Color(0xFF22405A)
    val Danger = Color(0xFFFF6B6B)
    val Warning = Color(0xFFFFC978)
    val Success = Color(0xFF4CD08A)
    val Stage = Color(0xFF0A0E13)          // fundo atrás da composição / timeline
    val Playhead = Color(0xFFFFFFFF)
    val Hairline = Color(0xB8273442)       // #273442 @ 0,72

    // --- Cromo do editor (AmColors) -----------------------------------------
    val EditorTopBar = Color(0xFF0F141A)
    val EditorPanel = Color(0xFF0F141A)
    val EditorPanelHigh = Color(0xFF151C24)
    val Pill = Color(0xFF1B2530)
    val Action = Brand
    val OnAction = Text
    val ActionDim = Color(0xFF16304A)
    val Selection = BrandDeep
    val SelectionText = Keyframe
    val Disabled = Color(0x40FFFFFF)       // branco 25 %
    val StatusBarVeil = Color(0xFF070A0E)

    // --- Régua (AmTickRuler) --------------------------------------------------
    val TickWeak = Color(0xFF43516A)
    val TickStrong = Color(0xFF7485A3)

    // --- Diálogos Cupertino ----------------------------------------------------
    val DestructiveCupertino = Color(0xFFFF453A)

    val BrandGradient = listOf(BrandDeep, Brand, Accent)

    // --- Painéis da A.01 (valores escritos à mão no Flutter, aqui viram token) ---
    val RailModeFill = Color(0xFF1E222D)       // modo aceso do trilho, cartão de preset da curva
    val RailDisabled = Color(0xFF434956)       // losango/curva do trilho sem alvo
    val ControlButton = Color(0xFF434A60)      // corrente Largura/Altura, botão "Centro"
    val DialTrack = Color(0xFF2E3548)          // anel do dial de rotação
    val DialValueBox = Color(0xFF242436)       // caixa do ângulo no centro do dial
    val CurveGrid = Color(0xFF34405A)          // grade pontilhada do editor de curva
    val CurvePresetBorder = Color(0xFF333B4F)  // borda do preset de curva apagado
    val EffectPreviewTop = Color(0xFF232B3A)   // cartela genérica do catálogo (degradê)
    val EffectPreviewBottom = Color(0xFF606F98)
    val EffectPreviewDisc = Color(0xFFFF4D2D)
    val BlendThumbBottom = Color(0xFFFF8A3D)   // miniatura da mescla: disco de baixo
    val BlendThumbTop = Color(0xFF3D9BFF)      // miniatura da mescla: disco de cima
    val SheetScrim = Color(0x590A0E13)         // véu das folhas de ajuste: palco a 35 %
}

/** Cor (fundo de barra) e glifo por tipo de camada — `aurea_tipo_da_camada.dart`. */
enum class LayerType(val kind: Int, val color: Color, val glyph: Char, val label: String) {
    Video(1, Color(0xFF6A52E0), CupertinoGlyph.VideocamFill, "Vídeo"),
    Image(2, Color(0xFF3D6FD9), CupertinoGlyph.PhotoFill, "Imagem"),
    Audio(3, Color(0xFF1F8C93), CupertinoGlyph.MusicNote, "Áudio"),
    Text(4, Color(0xFFB07A16), CupertinoGlyph.Textformat, "Texto"),
    Shape(5, Color(0xFF2E9459), CupertinoGlyph.CircleFill, "Forma"),
    Null(6, Color(0xFF444C5C), CupertinoGlyph.SmallcircleCircle, "Nulo"),
    Adjustment(7, Color(0xFF5A4A7A), CupertinoGlyph.SliderHorizontal3, "Ajuste"),
    Camera(8, Color(0xFF2A7B9B), CupertinoGlyph.CameraFill, "Câmera"),
    Light(9, Color(0xFFC06A24), CupertinoGlyph.Lightbulb, "Luz"),
    Model3D(10, Color(0xFFC06A24), CupertinoGlyph.CubeFill, "Objeto 3D"),
    Particles(11, Color(0xFFB0417A), CupertinoGlyph.Sparkles, "Partículas"),
    Group(12, Color(0xFF4C5566), CupertinoGlyph.FolderFill, "Grupo");

    companion object {
        fun of(kind: Int): LayerType = entries.firstOrNull { it.kind == kind } ?: Null
    }
}

/**
 * Tipografia. O Flutter do app antigo faz TODO texto herdar 15 sp, −0,1 de
 * espaçamento e altura de linha 1,35 (bodyMedium do M3). Sem essa base as
 * alturas dos componentes não batem com os prints.
 */
object AureaType {
    val Base = TextStyle(
        fontFamily = FontFamily.Default,
        fontSize = 15.sp,
        fontWeight = FontWeight.Normal,
        letterSpacing = (-0.1).sp,
        lineHeight = 1.35.em,
        color = AureaColors.Text,
        platformStyle = PlatformTextStyle(includeFontPadding = false),
        lineHeightStyle = LineHeightStyle(LineHeightStyle.Alignment.Center, LineHeightStyle.Trim.None),
    )
    val HeadlineLarge = Base.merge(TextStyle(fontSize = 34.sp, fontWeight = FontWeight.W700, letterSpacing = (-0.8).sp, lineHeight = 1.1.em))
    val TitleLarge = Base.merge(TextStyle(fontSize = 22.sp, fontWeight = FontWeight.W700, letterSpacing = (-0.5).sp, lineHeight = 1.27.em))
    val TitleMedium = Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600, letterSpacing = (-0.3).sp, lineHeight = 1.5.em))
    val BodyLarge = Base.merge(TextStyle(fontSize = 17.sp, letterSpacing = (-0.2).sp, lineHeight = 1.5.em))
    val BodySmall = Base.merge(TextStyle(fontSize = 13.sp, letterSpacing = 0.sp, lineHeight = 1.33.em, color = AureaColors.Muted))
    val LabelLarge = Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600, letterSpacing = (-0.2).sp, lineHeight = 1.43.em))

    // Editor (AureaEstilos)
    val EditorTitle = Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600))
    val Property = Base.merge(TextStyle(fontSize = 12.5.sp, color = AureaColors.Muted))
    val Value = Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W700, color = AureaColors.Keyframe, fontFeatureSettings = "tnum"))
    val Label = Base.merge(TextStyle(fontSize = 10.sp, color = AureaColors.Muted))
    val Section = Base.merge(TextStyle(fontSize = 11.sp, fontWeight = FontWeight.W600, letterSpacing = 0.3.sp, color = AureaColors.Muted))
    val Body = Base.merge(TextStyle(fontSize = 13.sp))
    val Tabular = TextStyle(fontFeatureSettings = "tnum")
}

/** Espaços, raios e alturas. */
object AureaDims {
    val S1 = 4.dp
    val S2 = 8.dp
    val S3 = 12.dp
    val S4 = 16.dp
    val S5 = 24.dp
    val S6 = 32.dp
    val MinTap = 44.dp

    val RadiusChip = 10.dp
    val RadiusCard = 12.dp
    val RadiusSheet = 18.dp
    val RadiusClip = 8.dp

    val IconSm = 16.dp
    val IconMd = 20.dp
    val IconLg = 24.dp
    val IconXl = 32.dp

    // Editor A.01
    val EditorTopBar = 44.dp
    val EditorTransport = 46.dp
    val PreviewResizeStrip = 8.dp
    val PanelHeader = 44.dp

    val Hairline = 0.5.dp
    val SelectionStroke = 2.dp
    val MultiSelectionStroke = 1.5.dp
}

/** Movimento: 100/200/300 ms; entrada desacelera, saída acelera (t²). Sem molas. */
object AureaMotion {
    const val FAST = 100
    const val NORMAL = 200
    const val SLOW = 300
    val Enter: Easing = CubicBezierEasing(0.0f, 0.0f, 0.58f, 1.0f)      // ≈ Curves.decelerate
    val Exit: Easing = CubicBezierEasing(1f / 3f, 0f, 2f / 3f, 1f / 3f)  // t²

    // Tocavel: escala 0,965 e opacidade 0,82 enquanto pressionado.
    const val PRESS_SCALE = 0.965f
    const val PRESS_ALPHA = 0.82f
    const val PRESS_DOWN_MS = 90
    const val PRESS_UP_MS = 220
    const val PRESS_ALPHA_DOWN_MS = 60
    const val PRESS_ALPHA_UP_MS = 180
}
