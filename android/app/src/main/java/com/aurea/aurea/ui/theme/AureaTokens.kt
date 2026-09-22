package com.aurea.aurea.ui.theme

import androidx.compose.animation.core.CubicBezierEasing
import androidx.compose.animation.core.Easing
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.PlatformTextStyle
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.LineHeightStyle
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp

// =============================================================================
//  O DESIGN SYSTEM DO AUREA (Fase 7.3 §8).
//
//  Um arquivo, uma fonte. Cor, tipografia, espaço, raio, ícone, sombra, vidro,
//  movimento e estados vivem AQUI — nenhuma tela escreve número ou cor solta.
//
//  A base numérica continua sendo a UI aprovada (Beta A.01 do Aurea antigo,
//  `aurea_colors.dart` / `app_theme.dart`, conferida pixel a pixel nos prints em
//  `docs/migration/ui_reference/`). O aparelho de referência tem densidade 2,625:
//  1 dp = 2,625 px. O que a A.01 tinha espalhado fora dos tokens entrou aqui com
//  nome semântico.
//
//  REGRA: se falta um token, ele entra aqui. Nunca no componente.
// =============================================================================

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
    val Subtle = Color(0xFF7C8A99)         // texto terciário: nota, rodapé, ficha
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

    // --- Barra de sistema (a Home é edge-to-edge) ----------------------------
    /** Faixa sob a status bar: o fundo #0F141A sob o véu preto 25 % do sistema. */
    val SystemBarVeil = Color(0xFF0B0F13)
    /** Barra de gestos preta opaca (o app antigo não era edge-to-edge embaixo). */
    val NavigationBar = Color(0xFF000000)
    val Scrim = Color(0x8A000000)          // véu das folhas modais
    val SheetScrim = Color(0x590A0E13)     // véu das folhas de ajuste: palco a 35 %

    // --- Sobre imagem (cards com miniatura) ----------------------------------
    val OnImage = Color(0xFFFFFFFF)
    val OnImage70 = Color(0xB3FFFFFF)
    /** Fim do scrim do hero (preto 70 %). */
    val ImageScrim = Color(0xB3000000)

    // --- Aviso "beta" (Sobre) -------------------------------------------------
    val Beta = Color(0xFFFFB020)
    val BetaFill = Color(0x22FFB020)
    val BetaBorder = Color(0x55FFB020)

    // --- Realce de pressionar -------------------------------------------------
    /** Realce do botão cheio: não encolhe, só acende branco 5 %. */
    val PressHighlight = Color(0x0DFFFFFF)
    /** Realce neutro de uma linha de lista. */
    val RowHighlight = Color(0x0FFFFFFF)
    val OverlayPressed = Color(0x1AFFFFFF)

    // --- Controles nativos (Cupertino) ---------------------------------------
    val SegmentSeparator = Color(0x4D8E8E93)
    val SegmentThumbShadow = Color(0x1F000000)
    val SegmentTrack = Background
    val SegmentThumb = SurfaceHigh
    /** Trilho desligado do interruptor (secondarySystemFill escuro). */
    val SwitchOffTrack = Color(0x52787880)

    // --- Campos ---------------------------------------------------------------
    /** Campo de busca na barra da lista: CupertinoTextField padrão no escuro. */
    val Field = Color(0xFF000000)
    val FieldBorder = Color(0x33FFFFFF)
    val FieldPlaceholder = Color(0x4DEBEBF5)
    /** Campo preenchido (busca do navegador de efeitos, formulários). */
    val FieldFilled = Color(0xFF272B33)
    /** Campo do diálogo de nome (o mesmo de `AureaNamePrompt`). */
    val FieldDialog = Color(0xFF1C1C1E)

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
 *
 * Estáticos: nenhum TextStyle é alocado por composição.
 */
object AureaType {
    /** A base de tudo (15 sp, −0,1, altura 1,35). */
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

    /** Deriva um estilo da base (o `merge` que o Flutter herdava). */
    fun of(
        size: Float,
        weight: FontWeight = FontWeight.Normal,
        spacing: Float = -0.1f,
        color: Color = AureaColors.Text,
        lineHeight: Float? = null,
    ): TextStyle = Base.merge(
        TextStyle(
            fontSize = size.sp,
            fontWeight = weight,
            letterSpacing = spacing.sp,
            color = color,
            lineHeight = lineHeight?.em ?: 1.35.em,
        ),
    )

    // --- Escala --------------------------------------------------------------
    val Display = of(30f, FontWeight.W800, -0.8f, lineHeight = 1.05f)
    val HeadlineLarge = Base.merge(TextStyle(fontSize = 34.sp, fontWeight = FontWeight.W700, letterSpacing = (-0.8).sp, lineHeight = 1.1.em))
    /** Título de tela dentro de uma aba (21–22 w700). */
    val ScreenTitle = of(21f, FontWeight.W700, -0.4f)
    /** Cabeçalho de tela grande (28 w800), como o "Comunidade" da A.01. */
    val TitleLarge = Base.merge(TextStyle(fontSize = 22.sp, fontWeight = FontWeight.W700, letterSpacing = (-0.5).sp, lineHeight = 1.27.em))
    val TitleMedium = Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600, letterSpacing = (-0.3).sp, lineHeight = 1.5.em))
    val TitleSmall = of(17f, FontWeight.W700, -0.4f)
    val BodyLarge = Base.merge(TextStyle(fontSize = 17.sp, letterSpacing = (-0.2).sp, lineHeight = 1.5.em))
    val BodySmall = Base.merge(TextStyle(fontSize = 13.sp, letterSpacing = 0.sp, lineHeight = 1.33.em, color = AureaColors.Muted))
    val LabelLarge = Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600, letterSpacing = (-0.2).sp, lineHeight = 1.43.em))

    // --- Home / telas fora do editor -----------------------------------------
    val Greeting = of(13.5f, color = AureaColors.Muted)
    val TabLabel = of(10.5f, FontWeight.W500, 0.1f)
    val Button = of(17f, FontWeight.W600, -0.2f, AureaColors.OnAccent)
    val ShortcutLabel = of(12f, FontWeight.W600)
    val HeroKicker = of(11f, FontWeight.W700, 0.4f, AureaColors.Accent)
    val HeroTitle = of(18f, FontWeight.W700, -0.2f, AureaColors.OnImage)
    val HeroSpec = of(11f, color = AureaColors.OnImage70)
    val HeroPill = of(12.5f, FontWeight.W700, color = AureaColors.OnAccent)
    val ListCount = of(15f, FontWeight.W700)
    val CardTitle = of(14f, FontWeight.W600, -0.1f)
    val CardSpec = of(11f, color = AureaColors.Muted)
    val LinkRow = of(14.5f)
    val FeatureTitle = of(15f, FontWeight.W600)
    val FeatureSubtitle = of(12.5f, color = AureaColors.Muted)
    val Empty = of(13.5f, color = AureaColors.Muted)
    val BatchCount = of(13f, color = AureaColors.Muted)
    val BatchAction = of(13f)
    val BatchDanger = of(13f, color = AureaColors.Danger)
    val SearchText = of(17f)
    val SearchPlaceholder = of(17f, color = AureaColors.FieldPlaceholder)
    val Note = of(12.5f, color = AureaColors.Muted)
    val Footer = of(11f, color = AureaColors.Subtle)
    val Pill = of(12f, FontWeight.W600, color = AureaColors.Accent)
    val ChipLabel = of(12f, FontWeight.W600)
    val Segment = of(13f, FontWeight.W600, -0.1f)
    val DialogField = of(15f)
    val VersionPill = of(12f, FontWeight.W600, color = AureaColors.Accent)
    val BetaTitle = of(13f, FontWeight.W700, color = AureaColors.Beta)
    val BetaBody = of(11f, color = AureaColors.Muted)

    // --- Folha "Novo projeto" -------------------------------------------------
    val SheetSpec = of(12.5f, color = AureaColors.Muted)
    val FrameLabel = of(22f, FontWeight.W700, -0.3f)
    val FrameHint = of(12f, color = AureaColors.Muted)
    val FormatLabel = of(12.5f, FontWeight.W600, -0.1f)
    val FormatHint = of(10f, color = AureaColors.Muted)
    /** Rótulo em caixa-alta de um campo ou seção. */
    val Caps = of(12f, FontWeight.W500, 0.6f, AureaColors.Muted)
    val NameField = of(17f, spacing = -0.2f)
    val NamePlaceholder = of(17f, spacing = -0.2f, color = AureaColors.Muted)
    val DimLabel = of(11f, color = AureaColors.Muted)
    val DimField = of(16f)
    val Times = of(16f)

    // --- Editor (AureaEstilos) ------------------------------------------------
    val EditorTitle = Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600))
    val Property = Base.merge(TextStyle(fontSize = 12.5.sp, color = AureaColors.Muted))
    val Value = Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W700, color = AureaColors.Keyframe, fontFeatureSettings = "tnum"))
    val Label = Base.merge(TextStyle(fontSize = 10.sp, color = AureaColors.Muted))
    val Section = Base.merge(TextStyle(fontSize = 11.sp, fontWeight = FontWeight.W600, letterSpacing = 0.3.sp, color = AureaColors.Muted))
    val Body = Base.merge(TextStyle(fontSize = 13.sp))
    val Tabular = TextStyle(fontFeatureSettings = "tnum")
}

/** Espaços, raios, alturas e ícones. */
object AureaDims {
    // --- Espaço ---------------------------------------------------------------
    val S1 = 4.dp
    val S2 = 8.dp
    val S3 = 12.dp
    val S4 = 16.dp
    val S5 = 24.dp
    val S6 = 32.dp
    val MinTap = 44.dp
    /** Recuo lateral das telas fora do editor. */
    val Gutter = 20.dp
    /** Alvo redondo padrão (44) e o círculo interno (36). */
    val RoundTarget = 44.dp
    val RoundCircle = 36.dp
    /** Folga no fim das listas para passar da barra de abas translúcida. */
    val ListEndSpace = 120.dp

    // --- Raios ----------------------------------------------------------------
    val RadiusXs = 5.dp
    val RadiusSm = 8.dp
    val RadiusChip = 10.dp
    val RadiusCard = 12.dp
    val RadiusMd = 14.dp
    val RadiusLg = 16.dp
    val RadiusSheet = 18.dp
    val RadiusXl = 20.dp
    val RadiusClip = 8.dp
    val RadiusPill = 999.dp

    // --- Ícones ---------------------------------------------------------------
    val IconXs = 13.dp
    val IconSm = 16.dp
    val IconMd = 20.dp
    val IconLg = 24.dp
    val IconXl = 32.dp

    // --- Controles ------------------------------------------------------------
    val ButtonHeight = 54.dp
    val ControlRow = 54.dp
    val SearchField = 40.dp
    val ChipHeight = 34.dp
    val TabBarHeight = 54.dp
    val CompactBarHeight = 52.dp
    /** Scroll a partir do qual a barra compacta aparece. */
    val CompactBarThreshold = 64.dp
    val SwitchWidth = 59.dp
    val SwitchHeight = 39.dp

    // --- Editor A.01 ----------------------------------------------------------
    val EditorTopBar = 44.dp
    val EditorTransport = 46.dp
    val PreviewResizeStrip = 8.dp
    val PanelHeader = 44.dp

    val Hairline = 0.5.dp
    val SelectionStroke = 2.dp
    val MultiSelectionStroke = 1.5.dp
}

/** Raios prontos. Uma instância por valor — nada de `RoundedCornerShape` por composição. */
object AureaShape {
    val Xs = RoundedCornerShape(AureaDims.RadiusXs)
    val Sm = RoundedCornerShape(AureaDims.RadiusSm)
    val Chip = RoundedCornerShape(AureaDims.RadiusChip)
    val Card = RoundedCornerShape(AureaDims.RadiusCard)
    val Md = RoundedCornerShape(AureaDims.RadiusMd)
    val Lg = RoundedCornerShape(AureaDims.RadiusLg)
    val Sheet = RoundedCornerShape(AureaDims.RadiusSheet)
    val Xl = RoundedCornerShape(AureaDims.RadiusXl)
    val SheetTop = RoundedCornerShape(topStart = AureaDims.RadiusXl, topEnd = AureaDims.RadiusXl)
    val Pill = RoundedCornerShape(AureaDims.RadiusPill)
    val Circle = CircleShape
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

/** Elevação: as sombras do Material e os vidros das barras. */
object AureaElevation {
    val Card = 1.dp
    val Sheet = 8.dp
    val Raised = 16.dp

    /** Sigma do blur (no vocabulário do Flutter) de cada vidro. */
    val TabBarBlur = 24.dp
    val CompactBarBlur = 18.dp
    val BatchBarBlur = 20.dp

    /** Tinta e cor sólida de recuo de cada vidro. */
    fun tabBarTint() = AureaColors.Background.copy(alpha = 0.72f)
    fun tabBarFallback() = AureaColors.Background.copy(alpha = 0.97f)
    fun compactBarTint() = AureaColors.Background.copy(alpha = 0.62f)
    fun compactBarFallback() = AureaColors.Background.copy(alpha = 0.97f)
    fun batchBarTint() = AureaColors.Surface.copy(alpha = 0.88f)
    fun batchBarFallback() = AureaColors.Surface
}

/**
 * Timeline da A.01 (`am_timeline.dart@aba36bb`), conferida no print t2
 * (`docs/migration/ui_spec/03_timeline.md` §1.B). Só a timeline usa.
 */
object AureaTimeline {
    // --- Cores ---------------------------------------------------------------
    val TickMajor = Color(0xFF8A97AD)      // risco de segundo (e rótulo)
    val TickMinor = Color(0xFF5A6880)      // risco de décimo / quadro
    val HeaderPill = Color(0xFF1E222D)     // pílula do olho + quadradinho
    val Swatch = Color(0xFFFFE899)         // quadradinho da camada sem etiqueta
    val SwatchGlyph = Color(0xFF0F141A)    // cadeado / visto dentro do quadradinho
    val KeyframeOn = Color(0xFFFFC107)     // losango escolhido (âmbar)
    val TrimHandle = Color(0xFFF2F5F9)     // alça de trim (dentro das pontas)

    // --- Medidas (dp) --------------------------------------------------------
    val RulerTicks = 20.dp                 // faixa dos riscos
    val RulerGap = 18.dp                   // respiro até a 1ª linha (o relógio mora aqui)
    val Row = 36.dp                        // mais baixa que a A.01 (46): cabem mais camadas
    val Bar = 30.dp                        // barra colada no topo da linha
    val BarRadius = 8.dp
    val BarMinWidth = 40.dp
    val KeyframeTrack = 11.dp              // faixa de baixo da barra, dos losangos
    val HeaderColumn = 66.dp               // coluna das pílulas (gradiente por cima das barras)
    val PillWidth = 58.dp
    val PillHeight = 24.dp
    val Playhead = 1.6.dp
    val PlayheadKnob = 8.dp
}
