package com.aurea.aurea.ui.theme

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
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

/**
 * Ícones da UI aprovada: a fonte `CupertinoIcons.ttf` (MIT, pacote
 * cupertino_icons 1.0.9 — licença em docs/licenses). O app antigo desenhava
 * cada ícone como um glifo dessa fonte numa caixa N×N com `fontSize = N`;
 * aqui é igual, então tamanho e peso visual batem com os prints.
 */
val CupertinoIconsFont = FontFamily(Font(R.font.cupertino_icons))

@Composable
fun CupertinoIcon(
    glyph: Char,
    size: Dp,
    tint: Color = AureaColors.Text,
    modifier: Modifier = Modifier,
) {
    val fontSize = with(LocalDensity.current) { size.toSp() }
    Box(modifier.size(size), contentAlignment = Alignment.Center) {
        Text(
            text = glyph.toString(),
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
    const val AddCircled = '\uF48A'
    const val Arrow2Squarepath = '\uF4E6'
    const val ArrowDownRightSquare = '\uF4F7'
    const val ArrowDownToLine = '\uF4FB'
    const val ArrowDownCircleFill = '\uF4EC'
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
    const val Lock = '\uF4C8'
    const val LockFill = '\uF4C9'
    const val LockOpen = '\uF6FA'
    const val Minus = '\uF70F'
    const val MinusCircle = '\uF463'
    const val Move = '\uF8F8'
    const val MusicNote = '\uF46B'
    const val MusicNote2 = '\uF46C'
    const val Paintbrush = '\uF72E'
    const val PauseFill = '\uF478'
    const val Pencil = '\uF37E'
    const val PencilOutline = '\uF73D'
    const val Person = '\uF47D'
    const val PersonFill = '\uF47E'
    const val Person2 = '\uF740'
    const val Person2Fill = '\uF741'
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

    // Home (Comunidade e Perfil da A.01)
    const val At = '\uF574'
    const val CloudFill = '\uF5FB'
    const val LockShield = '\uF6FE'
    const val PlusApp = '\uF775'

    // Painéis e controles de propriedade
    const val ArrowtriangleDownFill = '\uF55D'
    const val ArrowtriangleRightFill = '\uF569'
    const val ArrowUp = '\uF366'
    const val ArrowDown = '\uF35D'
    const val ChevronBack = '\uF3CF'
    const val Star = '\uF81F'
    const val StarFill = '\uF822'
    const val Scribble = '\uF7CB'
    const val Sportscourt = '\uF7F6'
    const val WaveformPath = '\uF897'
    const val Tortoise = '\uF86A'
    const val Hare = '\uF6B9'
}
