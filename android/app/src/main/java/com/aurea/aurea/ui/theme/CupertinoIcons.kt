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
    const val AddCircled = ''
    const val Arrow2Squarepath = ''
    const val ArrowDownRightSquare = ''
    const val ArrowDownToLine = ''
    const val ArrowDownCircleFill = ''
    const val ArrowLeftRight = ''
    const val ArrowLeftRightSquare = ''
    const val ArrowLeftToLine = ''
    const val ArrowRightArrowLeft = ''
    const val ArrowRightToLine = ''
    const val ArrowTurnLeftDown = ''
    const val ArrowTurnUpRight = ''
    const val ArrowUpArrowDown = ''
    const val ArrowUpDownSquare = ''
    const val ArrowUpToLine = ''
    const val ArrowUpDoc = ''
    const val ArrowUturnLeft = ''
    const val ArrowUturnRight = ''
    const val ArrowCounterclockwise = ''
    const val BackwardEnd = ''
    const val BackwardEndAlt = ''
    const val Bolt = ''
    const val Book = ''
    const val Bookmark = ''
    const val Camera = ''
    const val CameraFill = ''
    const val CameraViewfinder = ''
    const val CaptionsBubble = ''
    const val CaptionsBubbleFill = ''
    const val ChartBarAltFill = ''
    const val CheckmarkAlt = ''
    const val CheckmarkCircle = ''
    const val CheckmarkCircleFill = ''
    const val CheckmarkSeal = ''
    const val CheckmarkSquare = ''
    const val CheckmarkSquareFill = ''
    const val ChevronDown = ''
    const val ChevronLeft = ''
    const val ChevronRight = ''
    const val ChevronUp = ''
    const val Circle = ''
    const val CircleFill = ''
    const val CircleLefthalfFill = ''
    const val Clock = ''
    const val CloudDownload = ''
    const val ColorFilter = ''
    const val Crop = ''
    const val Cube = ''
    const val CubeBox = ''
    const val CubeBoxFill = ''
    const val CubeFill = ''
    const val Trash = ''
    const val DeleteLeft = ''
    const val DevicePhonePortrait = ''
    const val DocOnClipboard = ''
    const val DocOnDoc = ''
    const val DocText = ''
    const val DropFill = ''
    const val Ellipsis = ''
    const val ExclamationmarkBubble = ''
    const val ExclamationmarkTriangle = ''
    const val Eye = ''
    const val EyeFill = ''
    const val EyeSlash = ''
    const val Eyedropper = ''
    const val Film = ''
    const val Flag = ''
    const val Folder = ''
    const val FolderFill = ''
    const val ForwardEnd = ''
    const val ForwardEndAlt = ''
    const val Fullscreen = ''
    const val FullscreenExit = ''
    const val Gear = ''
    const val GearAltFill = ''
    const val Grid = ''
    const val House = ''
    const val HouseFill = ''
    const val InfoCircle = ''
    const val InfoCircleFill = ''
    const val Lightbulb = ''
    const val LineHorizontal3 = ''
    const val Link = ''
    const val LinkCircleFill = ''
    const val Lock = ''
    const val LockFill = ''
    const val LockOpen = ''
    const val Minus = ''
    const val MinusCircle = ''
    const val Move = ''
    const val MusicNote = ''
    const val MusicNote2 = ''
    const val Paintbrush = ''
    const val PauseFill = ''
    const val Pencil = ''
    const val PencilOutline = ''
    const val Person = ''
    const val PersonFill = ''
    const val Person2 = ''
    const val Person2Fill = ''
    const val PersonCropCircle = ''
    const val Photo = ''
    const val PhotoFill = ''
    const val PhotoOnRectangle = ''
    const val Play = ''
    const val PlayFill = ''
    const val PlayRectangle = ''
    const val Plus = ''
    const val PlusSquare = ''
    const val PlusSquareOnSquare = ''
    const val QuestionCircle = ''
    const val RectangleStack = ''
    const val Repeat = ''
    const val Rhombus = ''
    const val RhombusFill = ''
    const val Scissors = ''
    const val Search = ''
    const val SliderHorizontal3 = ''
    const val SmallcircleCircle = ''
    const val Sparkles = ''
    const val Speaker2 = ''
    const val SpeakerSlash = ''
    const val Speedometer = ''
    const val Square = ''
    const val SquareArrowUp = ''
    const val SquareGrid2x2 = ''
    const val SquareOnSquare = ''
    const val SquareStack3dDownRight = ''
    const val SquareStack3dDownRightFill = ''
    const val SquareStack3dUp = ''
    const val SuitDiamond = ''
    const val SuitDiamondFill = ''
    const val Textformat = ''
    const val Timer = ''
    const val Tv = ''
    const val Videocam = ''
    const val VideocamFill = ''
    const val WandStars = ''
    const val Waveform = ''
    const val Wrench = ''
    const val Xmark = ''
    const val XmarkCircle = ''
    const val XmarkCircleFill = ''

    // Home (Comunidade e Perfil da A.01)
    const val At = ''
    const val CloudFill = ''
    const val LockShield = ''
    const val PlusApp = ''
}
