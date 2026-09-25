package com.aurea.aurea.ui.theme

import androidx.compose.animation.core.CubicBezierEasing
import androidx.compose.animation.core.Easing
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
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
    /**
     * Tema escolhido nos Ajustes. É estado do Compose: trocar redesenha o app
     * inteiro na hora, sem reiniciar. Só os tons de fundo, superfície e a cor
     * de destaque mudam; estados (erro, aviso, sucesso) e o texto ficam.
     */
    var palette by mutableStateOf(AureaPalette.Aurea)
    // --- Marca ---------------------------------------------------------------
    val BrandDeep: Color get() = palette.brandDeep
    val Brand: Color get() = palette.brand          // preenchimento de ação (Exportar, "+")
    val Accent: Color get() = palette.accent         // ação/estado fora do editor, destaque
    val Keyframe: Color get() = palette.keyframe       // keyframe, curva, valor, seleção em texto
    val Background: Color get() = palette.background
    val Surface: Color get() = palette.surface
    val SurfaceHigh: Color get() = palette.surfaceHigh
    val Chip: Color get() = palette.chip
    val ChipHigh: Color get() = palette.chipHigh       // campoAlto = lerp(chip, texto, 0,08)
    val Border: Color get() = palette.border
    val Text = Color(0xFFF7F9FB)
    val Muted: Color get() = palette.muted
    val Subtle: Color get() = palette.subtle         // texto terciário: nota, rodapé, ficha
    val OnAccent: Color get() = palette.onAccent
    val AccentDim: Color get() = palette.accentDim
    val KeyframeDim: Color get() = palette.keyframeDim
    val Danger = Color(0xFFFF6B6B)
    val Warning = Color(0xFFFFC978)
    val Success = Color(0xFF4CD08A)
    val Stage: Color get() = palette.stage          // fundo atrás da composição / timeline
    val Playhead = Color(0xFFFFFFFF)
    val Hairline: Color get() = palette.hairline       // #273442 @ 0,72

    // --- Cromo do editor (AmColors) -----------------------------------------
    val EditorTopBar: Color get() = palette.editorTopBar
    val EditorPanel: Color get() = palette.editorPanel
    val EditorPanelHigh: Color get() = palette.editorPanelHigh
    val Pill: Color get() = palette.pill
    val Action: Color get() = Brand
    val OnAction = Text
    val ActionDim: Color get() = palette.actionDim
    val Selection: Color get() = BrandDeep
    val SelectionText: Color get() = Keyframe
    val Disabled = Color(0x40FFFFFF)       // branco 25 %
    val StatusBarVeil: Color get() = palette.statusBarVeil

    // --- Barra de sistema (a Home é edge-to-edge) ----------------------------
    /** Faixa sob a status bar: o fundo #0F141A sob o véu preto 25 % do sistema. */
    val SystemBarVeil: Color get() = palette.systemBarVeil
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
    val SegmentTrack: Color get() = Background
    val SegmentThumb: Color get() = SurfaceHigh
    /** Trilho desligado do interruptor (secondarySystemFill escuro). */
    val SwitchOffTrack = Color(0x52787880)

    // --- Campos ---------------------------------------------------------------
    /** Campo de busca na barra da lista: CupertinoTextField padrão no escuro. */
    val Field = Color(0xFF000000)
    val FieldBorder = Color(0x33FFFFFF)
    val FieldPlaceholder = Color(0x4DEBEBF5)
    /** Campo preenchido (busca do navegador de efeitos, formulários). */
    val FieldFilled: Color get() = palette.fieldFilled
    /** Campo do diálogo de nome (o mesmo de `AureaNamePrompt`). */
    val FieldDialog = Color(0xFF1C1C1E)

    // --- Régua (AmTickRuler) --------------------------------------------------
    val TickWeak: Color get() = palette.tickWeak
    val TickStrong: Color get() = palette.tickStrong

    // --- Diálogos Cupertino ----------------------------------------------------
    val DestructiveCupertino = Color(0xFFFF453A)

    val BrandGradient: List<Color> get() = listOf(BrandDeep, Brand, Accent)

    // --- Painéis da A.01 (valores escritos à mão no Flutter, aqui viram token) ---
    val RailModeFill: Color get() = palette.railModeFill       // modo aceso do trilho, cartão de preset da curva
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

/**
 * Paleta de um tema: só o que muda entre temas (fundos, superfícies,
 * marca e destaque). [id] é o que fica salvo nos Ajustes; nunca renomeie.
 */
data class AureaPalette(
    val id: String,
    val brandDeep: Color,
    val brand: Color,
    val accent: Color,
    val keyframe: Color,
    val background: Color,
    val surface: Color,
    val surfaceHigh: Color,
    val chip: Color,
    val chipHigh: Color,
    val border: Color,
    val muted: Color,
    val subtle: Color,
    val onAccent: Color,
    val accentDim: Color,
    val keyframeDim: Color,
    val stage: Color,
    val hairline: Color,
    val editorTopBar: Color,
    val editorPanel: Color,
    val editorPanelHigh: Color,
    val pill: Color,
    val actionDim: Color,
    val statusBarVeil: Color,
    val systemBarVeil: Color,
    val tickWeak: Color,
    val tickStrong: Color,
    val railModeFill: Color,
    val fieldFilled: Color,
) {
    companion object {
        val Aurea = AureaPalette(
            id = "aurea",
            brandDeep = Color(0xFF123A63),
            brand = Color(0xFF245D8C),
            accent = Color(0xFF6FAED9),
            keyframe = Color(0xFFA9D3EC),
            background = Color(0xFF0F141A),
            surface = Color(0xFF151C24),
            surfaceHigh = Color(0xFF1B2530),
            chip = Color(0xFF212D3A),
            chipHigh = Color(0xFF323D49),
            border = Color(0xFF273442),
            muted = Color(0xFFAAB6C3),
            subtle = Color(0xFF7C8A99),
            onAccent = Color(0xFF0B1117),
            accentDim = Color(0xFF1D3A55),
            keyframeDim = Color(0xFF22405A),
            stage = Color(0xFF0A0E13),
            hairline = Color(0xB8273442),
            editorTopBar = Color(0xFF0F141A),
            editorPanel = Color(0xFF0F141A),
            editorPanelHigh = Color(0xFF151C24),
            pill = Color(0xFF1B2530),
            actionDim = Color(0xFF16304A),
            statusBarVeil = Color(0xFF070A0E),
            systemBarVeil = Color(0xFF0B0F13),
            tickWeak = Color(0xFF43516A),
            tickStrong = Color(0xFF7485A3),
            railModeFill = Color(0xFF1E222D),
            fieldFilled = Color(0xFF272B33),
        )
        val Midnight = AureaPalette(
            id = "midnight",
            brandDeep = Color(0xFF15325A),
            brand = Color(0xFF2C66A0),
            accent = Color(0xFF7DB8E6),
            keyframe = Color(0xFFB3D9F0),
            background = Color(0xFF000000),
            surface = Color(0xFF0B0D10),
            surfaceHigh = Color(0xFF14171C),
            chip = Color(0xFF1A1E24),
            chipHigh = Color(0xFF2A2F37),
            border = Color(0xFF1E232A),
            muted = Color(0xFFA7B0BA),
            subtle = Color(0xFF77818C),
            onAccent = Color(0xFF05080B),
            accentDim = Color(0xFF15283C),
            keyframeDim = Color(0xFF1A3247),
            stage = Color(0xFF000000),
            hairline = Color(0xB81E232A),
            editorTopBar = Color(0xFF000000),
            editorPanel = Color(0xFF000000),
            editorPanelHigh = Color(0xFF0B0D10),
            pill = Color(0xFF14171C),
            actionDim = Color(0xFF102438),
            statusBarVeil = Color(0xFF000000),
            systemBarVeil = Color(0xFF000000),
            tickWeak = Color(0xFF3A4250),
            tickStrong = Color(0xFF6C788C),
            railModeFill = Color(0xFF15181E),
            fieldFilled = Color(0xFF1A1D22),
        )
        val Graphite = AureaPalette(
            id = "graphite",
            brandDeep = Color(0xFF3A4250),
            brand = Color(0xFF566273),
            accent = Color(0xFFB7C4D3),
            keyframe = Color(0xFFD6DEE8),
            background = Color(0xFF16181C),
            surface = Color(0xFF1D2025),
            surfaceHigh = Color(0xFF25292F),
            chip = Color(0xFF2B3037),
            chipHigh = Color(0xFF3A4048),
            border = Color(0xFF33383F),
            muted = Color(0xFFB0B6BE),
            subtle = Color(0xFF838A94),
            onAccent = Color(0xFF101215),
            accentDim = Color(0xFF2E343C),
            keyframeDim = Color(0xFF343B45),
            stage = Color(0xFF111316),
            hairline = Color(0xB833383F),
            editorTopBar = Color(0xFF16181C),
            editorPanel = Color(0xFF16181C),
            editorPanelHigh = Color(0xFF1D2025),
            pill = Color(0xFF25292F),
            actionDim = Color(0xFF2A3038),
            statusBarVeil = Color(0xFF0D0E10),
            systemBarVeil = Color(0xFF121417),
            tickWeak = Color(0xFF4A515C),
            tickStrong = Color(0xFF7F8896),
            railModeFill = Color(0xFF23272D),
            fieldFilled = Color(0xFF2A2E34),
        )
        val Emerald = AureaPalette(
            id = "emerald",
            brandDeep = Color(0xFF0F4A3A),
            brand = Color(0xFF1C7A5E),
            accent = Color(0xFF5BD6A8),
            keyframe = Color(0xFFA6ECD2),
            background = Color(0xFF0B1411),
            surface = Color(0xFF111D19),
            surfaceHigh = Color(0xFF172722),
            chip = Color(0xFF1D302A),
            chipHigh = Color(0xFF2C403A),
            border = Color(0xFF223A33),
            muted = Color(0xFFA6BDB5),
            subtle = Color(0xFF789088),
            onAccent = Color(0xFF06110D),
            accentDim = Color(0xFF163A30),
            keyframeDim = Color(0xFF1D4539),
            stage = Color(0xFF080F0C),
            hairline = Color(0xB8223A33),
            editorTopBar = Color(0xFF0B1411),
            editorPanel = Color(0xFF0B1411),
            editorPanelHigh = Color(0xFF111D19),
            pill = Color(0xFF172722),
            actionDim = Color(0xFF123326),
            statusBarVeil = Color(0xFF060B09),
            systemBarVeil = Color(0xFF09100D),
            tickWeak = Color(0xFF3B5249),
            tickStrong = Color(0xFF6C8C80),
            railModeFill = Color(0xFF18251F),
            fieldFilled = Color(0xFF1F2B27),
        )
        val Amethyst = AureaPalette(
            id = "amethyst",
            brandDeep = Color(0xFF3A2470),
            brand = Color(0xFF6246B8),
            accent = Color(0xFFB9A2FF),
            keyframe = Color(0xFFDCCFFF),
            background = Color(0xFF110E19),
            surface = Color(0xFF181423),
            surfaceHigh = Color(0xFF201B2E),
            chip = Color(0xFF282238),
            chipHigh = Color(0xFF383049),
            border = Color(0xFF2F2842),
            muted = Color(0xFFB5ADC6),
            subtle = Color(0xFF877E99),
            onAccent = Color(0xFF0D0A14),
            accentDim = Color(0xFF2C2248),
            keyframeDim = Color(0xFF362B55),
            stage = Color(0xFF0C0A12),
            hairline = Color(0xB82F2842),
            editorTopBar = Color(0xFF110E19),
            editorPanel = Color(0xFF110E19),
            editorPanelHigh = Color(0xFF181423),
            pill = Color(0xFF201B2E),
            actionDim = Color(0xFF261E42),
            statusBarVeil = Color(0xFF08070C),
            systemBarVeil = Color(0xFF0D0B13),
            tickWeak = Color(0xFF4A4260),
            tickStrong = Color(0xFF7D7399),
            railModeFill = Color(0xFF211C2D),
            fieldFilled = Color(0xFF272233),
        )
        val Sunset = AureaPalette(
            id = "sunset",
            brandDeep = Color(0xFF6A3212),
            brand = Color(0xFFB45A1E),
            accent = Color(0xFFFFB060),
            keyframe = Color(0xFFFFD6A8),
            background = Color(0xFF15100C),
            surface = Color(0xFF1E1712),
            surfaceHigh = Color(0xFF281F18),
            chip = Color(0xFF30251D),
            chipHigh = Color(0xFF40342B),
            border = Color(0xFF3A2C22),
            muted = Color(0xFFC4B4A6),
            subtle = Color(0xFF96867A),
            onAccent = Color(0xFF140C06),
            accentDim = Color(0xFF45280F),
            keyframeDim = Color(0xFF4E3218),
            stage = Color(0xFF100C09),
            hairline = Color(0xB83A2C22),
            editorTopBar = Color(0xFF15100C),
            editorPanel = Color(0xFF15100C),
            editorPanelHigh = Color(0xFF1E1712),
            pill = Color(0xFF281F18),
            actionDim = Color(0xFF3E230D),
            statusBarVeil = Color(0xFF0B0806),
            systemBarVeil = Color(0xFF110D0A),
            tickWeak = Color(0xFF5A4838),
            tickStrong = Color(0xFF907A66),
            railModeFill = Color(0xFF271E17),
            fieldFilled = Color(0xFF2E251E),
        )
        val all = listOf(Aurea, Midnight, Graphite, Emerald, Amethyst, Sunset)
        fun of(id: String?): AureaPalette = all.firstOrNull { it.id == id } ?: Aurea
    }
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
    /** Cabeçalho de tela grande (28 w800): o título das abas da Home. */
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
