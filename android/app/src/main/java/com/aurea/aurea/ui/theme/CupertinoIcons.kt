package com.aurea.aurea.ui.theme

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.graphics.vector.path
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.PlatformTextStyle
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.LineHeightStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.ui.i18n.mirrorInRtl

/**
 * Ícones da UI aprovada: a fonte `CupertinoIcons.ttf` (MIT, pacote
 * cupertino_icons 1.0.9 — licença em docs/licenses). O app antigo desenhava
 * cada ícone como um glifo dessa fonte numa caixa N×N com `fontSize = N`;
 * aqui é igual, então tamanho e peso visual batem com os prints.
 */
val CupertinoIconsFont = FontFamily(Font(R.font.cupertino_icons))

/**
 * 0.3.4: o estilo dos ícones do app, num ponto só (par de `AureaIconStyle` do
 * Theme.swift, mesmos valores). Minimalista e limpo: ícone de CONTORNO, traço fino
 * e uniforme, desenhado UMA vez (sem contorno extra, sem cópias deslocadas).
 */
object AureaIconStyle {
    /**
     * Traço dos ícones desenhados à mão, na caixa de 24 (multiplique pela unidade
     * `tamanho / 24`). É o peso dos glifos da fonte CupertinoIcons, que já são de
     * traço fino; cantos e pontas redondos.
     */
    const val Line = 1.5f

    /**
     * Glifo cheio → o de contorno da mesma fonte. Vale para todo ícone que passa
     * pelo [CupertinoIcon] e pelos glifos da timeline. Ficam cheios só os que são
     * estado ou marca: play/pausa, losango de keyframe, marcador de batida, estrela
     * de favorito, check de selecionado/feito, caixa marcada, solo, ponto de cor
     * (`CircleFill`), as metades de círculo (opacidade/mistura) e o triangulinho
     * de 7–8 dp que marca "tem menu" no canto de um botão (os de abrir/fechar
     * seção já usam chevron).
     */
    private val Outline: Map<Char, Char> = mapOf(
        CupertinoGlyph.VideocamFill to CupertinoGlyph.Videocam,
        CupertinoGlyph.PhotoFill to CupertinoGlyph.Photo,
        CupertinoGlyph.FolderFill to CupertinoGlyph.Folder,
        CupertinoGlyph.CameraFill to CupertinoGlyph.Camera,
        CupertinoGlyph.CubeFill to CupertinoGlyph.Cube,
        CupertinoGlyph.CubeBoxFill to CupertinoGlyph.CubeBox,
        CupertinoGlyph.HouseFill to CupertinoGlyph.House,
        CupertinoGlyph.MicFill to CupertinoGlyph.Mic,
        CupertinoGlyph.PaintbrushFill to CupertinoGlyph.Paintbrush,
        CupertinoGlyph.DropFill to CupertinoGlyph.Drop,
        CupertinoGlyph.GearAltFill to CupertinoGlyph.Gear,
        CupertinoGlyph.InfoCircleFill to CupertinoGlyph.InfoCircle,
        CupertinoGlyph.ArrowDownCircleFill to CupertinoGlyph.ArrowDownCircle,
        CupertinoGlyph.XmarkCircleFill to CupertinoGlyph.XmarkCircle,
        CupertinoGlyph.SquareStack3dDownRightFill to CupertinoGlyph.SquareStack3dDownRight,
        CupertinoGlyph.CaptionsBubbleFill to CupertinoGlyph.CaptionsBubble,
        CupertinoGlyph.LockFill to CupertinoGlyph.Lock,
        CupertinoGlyph.LockOpenFill to CupertinoGlyph.LockOpen,
        CupertinoGlyph.LinkCircleFill to CupertinoGlyph.LinkCircle,
        CupertinoGlyph.EyeFill to CupertinoGlyph.Eye,
    )

    /** O glifo no estilo do app (ver [Outline]). */
    fun glyph(c: Char): Char = Outline[c] ?: c
}

/**
 * 0.3.4: ícones de linha próprios no lugar dos vetores do Material (cheios ou de
 * traço 2): traço [AureaIconStyle.Line], pontas e cantos redondos, na caixa de 24.
 * Use `Icon(AureaLineIcons.X, ...)`; a cor vem do `tint`. A mesma geometria no
 * `AureaLineIcon` do Theme.swift.
 */
object AureaLineIcons {
    /** ‹ de voltar dos painéis. */
    val ChevronLeft: ImageVector by lazy { line("ChevronLeft", false, floatArrayOf(15f, 6f, 9f, 12f, 15f, 18f)) }
    /** Alinhar os inícios (linhas encostadas à esquerda; espelha em RTL). */
    val AlignStarts: ImageVector by lazy {
        line("AlignStarts", true, floatArrayOf(4f, 6f, 20f, 6f), floatArrayOf(4f, 10f, 14f, 10f),
            floatArrayOf(4f, 14f, 20f, 14f), floatArrayOf(4f, 18f, 14f, 18f))
    }
    /** Alinhar os fins (linhas encostadas à direita; espelha em RTL). */
    val AlignEnds: ImageVector by lazy {
        line("AlignEnds", true, floatArrayOf(4f, 6f, 20f, 6f), floatArrayOf(10f, 10f, 20f, 10f),
            floatArrayOf(4f, 14f, 20f, 14f), floatArrayOf(10f, 18f, 20f, 18f))
    }
    /** Escada (uma camada começa quando a de cima termina / distribuir no tempo). */
    val Stairs: ImageVector by lazy {
        line("Stairs", false, floatArrayOf(4.5f, 19.5f, 9.5f, 19.5f, 9.5f, 14.5f, 14.5f, 14.5f, 14.5f, 9.5f, 19.5f, 9.5f, 19.5f, 4.5f))
    }

    /** Cada [FloatArray] é uma polilinha x0,y0,x1,y1… na caixa de 24. */
    private fun line(name: String, autoMirror: Boolean, vararg strokes: FloatArray): ImageVector =
        ImageVector.Builder(
            name = "AureaLine.$name", defaultWidth = 24.dp, defaultHeight = 24.dp,
            viewportWidth = 24f, viewportHeight = 24f, autoMirror = autoMirror,
        ).apply {
            strokes.forEach { s ->
                path(
                    stroke = SolidColor(Color.Black), strokeLineWidth = AureaIconStyle.Line,
                    strokeLineCap = StrokeCap.Round, strokeLineJoin = StrokeJoin.Round,
                ) {
                    moveTo(s[0], s[1])
                    var i = 2
                    while (i + 1 < s.size) { lineTo(s[i], s[i + 1]); i += 2 }
                }
            }
        }.build()
}

@Composable
fun CupertinoIcon(
    glyph: Char,
    size: Dp,
    tint: Color = AureaColors.Text,
    modifier: Modifier = Modifier,
    /** Glifos de sentido de leitura (‹ › ↶ ↷ ▸) espelham em árabe; `false` desliga. */
    autoMirror: Boolean = true,
) {
    if (glyph == CupertinoGlyph.MotionBlur) {
        // Own vector: a moving object and fading trails, shared with the Swift drawing.
        Canvas(modifier.size(size)) {
            val unit = this.size.minDimension / 24f
            // 0.3.4: o traço fino do estilo dos ícones (AureaIconStyle.Line).
            val line = AureaIconStyle.Line * unit
            drawCircle(tint, radius = 5.4f * unit, center = Offset(17f * unit, 12f * unit),
                style = Stroke(width = line))
            val trails = listOf(Triple(2f, 7f, .55f), Triple(1f, 12f, .85f), Triple(3f, 17f, .55f))
            trails.forEach { (x, y, alpha) ->
                drawLine(tint.copy(alpha = tint.alpha * alpha), Offset(x * unit, y * unit),
                    Offset(9f * unit, y * unit), strokeWidth = line, cap = StrokeCap.Round)
            }
        }
        return
    }
    // 0.3.4: o glifo no estilo do app (cheio → contorno, AureaIconStyle), desenhado uma vez.
    val shown = AureaIconStyle.glyph(glyph)
    val fontSize = with(LocalDensity.current) { size.toSp() }
    val mirror = if (autoMirror && shown in CupertinoGlyph.Directional) Modifier.mirrorInRtl() else Modifier
    Box(modifier.size(size).then(mirror), contentAlignment = Alignment.Center) {
        Text(
            text = shown.toString(),
            style = TextStyle(
                fontFamily = CupertinoIconsFont,
                fontSize = fontSize,
                lineHeight = fontSize,
                color = tint,
                textAlign = TextAlign.Center,
                platformStyle = PlatformTextStyle(includeFontPadding = false),
                lineHeightStyle = LineHeightStyle(LineHeightStyle.Alignment.Center, LineHeightStyle.Trim.Both),
            ),
        )
    }
}

/** Codepoints usados pela UI (nomes do pacote cupertino_icons). */
object CupertinoGlyph {
    /** Reserved for Aurea's own vector; not a font codepoint. */
    const val MotionBlur = '\uE000'
    const val AddCircled = '\uF48A'
    const val Arrow2Squarepath = '\uF4E6'
    const val ArrowDownRightSquare = '\uF4F7'
    const val ArrowDownToLine = '\uF4FB'
    const val ArrowDownCircleFill = '\uF4EC'
    const val ArrowDownCircle = '\uF4EB'
    const val ArrowLeftRight = '\uF500'
    const val ArrowLeftRightSquare = '\uF503'
    const val ArrowLeftToLine = '\uF507'
    const val ArrowRightArrowLeft = '\uF50B'
    const val ArrowRightToLine = '\uF514'
    const val ArrowTurnLeftDown = '\uF519'
    const val ArrowTurnUpRight = '\uF51E'
    const val ArrowUpArrowDown = '\uF51F'
    const val ArrowUpDownSquare = '\uF52D'
    const val ArrowUpToLine = '\uF53D'
    const val ArrowUpDoc = '\uF528'
    const val ArrowUturnLeft = '\uF544'
    const val ArrowUturnRight = '\uF549'
    const val ArrowCounterclockwise = '\uF21C'
    const val BackwardEnd = '\uF578'
    const val BackwardEndAlt = '\uF579'
    const val Bolt = '\uF593'
    const val Book = '\uF3E7'
    const val Bookmark = '\uF3E9'
    const val Camera = '\uF3F5'
    const val CameraFill = '\uF3F6'
    const val CameraViewfinder = '\uF5B9'
    const val CaptionsBubble = '\uF5BE'
    const val CaptionsBubbleFill = '\uF5BF'
    const val ChartBarAltFill = '\uF8B7'
    const val CheckmarkAlt = '\uF8C1'
    const val CheckmarkCircle = '\uF3FE'
    const val CheckmarkCircleFill = '\uF3FF'
    const val CheckmarkSeal = '\uF5CB'
    const val CheckmarkSquare = '\uF5CF'
    const val CheckmarkSquareFill = '\uF5D0'
    const val ChevronDown = '\uF5D5'
    const val ChevronLeft = '\uF3D2'
    const val ChevronRight = '\uF3D3'
    const val ChevronUp = '\uF5E5'
    const val Circle = '\uF401'
    const val CircleFill = '\uF400'
    const val CircleLefthalfFill = '\uF5F0'
    const val Clock = '\uF4BE'
    const val CloudDownload = '\uF8C4'
    const val ColorFilter = '\uF8C8'
    const val Crop = '\uF618'
    const val Cube = '\uF61A'
    const val CubeBox = '\uF61B'
    const val CubeBoxFill = '\uF61C'
    const val CubeFill = '\uF61D'
    const val Trash = '\uF4C4'
    const val DeleteLeft = '\uF621'
    const val DevicePhonePortrait = '\uF8CF'
    const val DocOnClipboard = '\uF632'
    const val DocOnDoc = '\uF634'
    const val DocText = '\uF638'
    const val DropFill = '\uF8D9'
    const val Drop = '\uF8D8'
    const val Ellipsis = '\uF46A'
    const val ExclamationmarkBubble = '\uF656'
    const val ExclamationmarkTriangle = '\uF660'
    const val Eye = '\uF424'
    const val EyeFill = '\uF425'
    const val EyeSlash = '\uF662'
    const val Eyedropper = '\uF664'
    const val Film = '\uF66B'
    const val Flag = '\uF42C'
    const val Folder = '\uF434'
    const val FolderFill = '\uF435'
    const val ForwardEnd = '\uF67F'
    const val ForwardEndAlt = '\uF680'
    const val Fullscreen = '\uF386'
    const val FullscreenExit = '\uF37D'
    const val Gear = '\uF43C'
    const val GearAltFill = '\uF43D'
    const val Grid = '\uF6A5'
    const val House = '\uF447'
    const val HouseFill = '\uF6CA'
    const val InfoCircle = '\uF44C'
    const val InfoCircleFill = '\uF6CF'
    const val Lightbulb = '\uF6DD'
    const val LineHorizontal3 = '\uF6E1'
    const val Link = '\uF6E5'
    const val LinkCircleFill = '\uF6E7'
    const val LinkCircle = '\uF6E6'
    const val Lock = '\uF4C8'
    const val LockFill = '\uF4C9'
    const val LockOpen = '\uF6FA'
    const val LockOpenFill = '\uF6FB'
    const val Minus = '\uF70F'
    const val MinusCircle = '\uF463'
    // Microfone (Gravar voz): contorno e cheio.
    const val Mic = '\uF460'
    const val MicFill = '\uF461'
    const val Move = '\uF8F8'
    const val MusicNote = '\uF46B'
    const val MusicNote2 = '\uF46C'
    const val Paintbrush = '\uF72E'
    const val PaintbrushFill = '\uF72F'
    const val PauseFill = '\uF478'
    const val Pencil = '\uF37E'
    const val PencilOutline = '\uF73D'
    const val PersonCropCircle = '\uF419'
    const val Photo = '\uF767'
    const val PhotoFill = '\uF768'
    const val PhotoOnRectangle = '\uF76A'
    const val Play = '\uF487'
    const val PlayFill = '\uF488'
    const val PlayRectangle = '\uF771'
    const val Plus = '\uF489'
    const val PlusSquare = '\uF77E'
    const val PlusSquareOnSquare = '\uF781'
    const val QuestionCircle = '\uF78F'
    const val RectangleStack = '\uF3C9'
    const val Repeat = '\uF7BF'
    const val Rhombus = '\uF7C2'
    const val RhombusFill = '\uF7C3'
    const val Scissors = '\uF7C9'
    const val Search = '\uF4A5'
    const val SliderHorizontal3 = '\uF7DC'
    const val SmallcircleCircle = '\uF7DF'
    const val Sparkles = '\uF7E8'
    const val Speaker2 = '\uF7EB'
    const val SpeakerSlash = '\uF7EE'
    const val Speedometer = '\uF7F5'
    const val Square = '\uF7F8'
    const val SquareArrowUp = '\uF4CA'
    const val SquareGrid2x2 = '\uF804'
    const val SquareOnSquare = '\uF80D'
    const val SquareStack3dDownRight = '\uF817'
    const val SquareStack3dDownRightFill = '\uF818'
    const val SquareStack3dUp = '\uF819'
    const val SuitDiamond = '\uF831'
    const val SuitDiamondFill = '\uF832'
    const val Textformat = '\uF85C'
    const val Timer = '\uF868'
    const val Tv = '\uF881'
    const val Videocam = '\uF4CC'
    const val VideocamFill = '\uF4CD'
    const val WandStars = '\uF892'
    const val Waveform = '\uF894'
    const val Wrench = '\uF8A0'
    const val Xmark = '\uF404'
    const val XmarkCircle = '\uF405'
    const val XmarkCircleFill = '\uF36E'

    // Painéis e controles de propriedade
    const val ArrowtriangleDownFill = '\uF55D'
    const val ArrowtriangleRightFill = '\uF569'
    const val ArrowUp = '\uF366'
    const val ArrowDown = '\uF35D'
    const val ChevronBack = '\uF3CF'
    const val Star = '\uF81F'
    const val StarFill = '\uF822'
    const val Scribble = '\uF7CB'
    const val Tortoise = '\uF86A'
    const val Hare = '\uF6B9'

    /**
     * Os que apontam o sentido da LEITURA (voltar, avançar, abrir, desfazer e
     * refazer): em RTL o [CupertinoIcon] os espelha. Fora daqui ficam os do
     * TEMPO e do palco (⇤ ⇥, play, alinhar à esquerda/direita da tela): o tempo
     * e o canvas não espelham. Dentro de `KeepLtr` nada espelha.
     */
    val Directional: Set<Char> = setOf(
        ChevronLeft, ChevronRight, ChevronBack, ArrowtriangleRightFill, ArrowUturnLeft, ArrowUturnRight,
    )
}
