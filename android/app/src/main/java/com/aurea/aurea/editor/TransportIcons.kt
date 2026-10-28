package com.aurea.aurea.editor

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Fill
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.scale
import androidx.compose.ui.graphics.vector.PathParser
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.tocavel

/**
 * 0.4.1 (mockup "interface 03"): os ícones da barra de reprodução, os mesmos traços
 * do mockup (caixa 24 × 24; contorno de 1,8 ou preenchidos). Par do iOS.
 */
internal enum class TransportIcon(val filled: Boolean, vararg val paths: String) {
    Undo(false, "M3 10h10a5 5 0 0 1 5 5v0a5 5 0 0 1 -5 5H3", "M3 10l4-4M3 10l4 4"),
    Redo(false, "M21 10H11a5 5 0 0 0 -5 5v0a5 5 0 0 0 5 5h10", "M21 10l-4-4M21 10l-4 4"),
    Previous(true, "M19 20L9 12L19 4Z", "M6 4h0.5a1 1 0 0 1 1 1v14a1 1 0 0 1 -1 1H6a1 1 0 0 1 -1-1V5a1 1 0 0 1 1-1z"),
    Play(true, "M6 4L20 12L6 20Z"),
    Pause(true, "M6 4h4v16H6zM14 4h4v16h-4z"),
    Next(true, "M5 4L15 12L5 20Z", "M17.5 4h0.5a1 1 0 0 1 1 1v14a1 1 0 0 1 -1 1h-0.5a1 1 0 0 1 -1-1V5a1 1 0 0 1 1-1z"),
    Bookmark(false, "M19 21l-7-5-7 5V5a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2z"),
    BookmarkFill(true, "M19 21l-7-5-7 5V5a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2z"),
    Copy(false, "M11 9h9a2 2 0 0 1 2 2v9a2 2 0 0 1 -2 2h-9a2 2 0 0 1 -2-2v-9a2 2 0 0 1 2-2z", "M5 15H4a2 2 0 0 1 -2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"),
    Sidebar(false, "M6 4h12a3 3 0 0 1 3 3v10a3 3 0 0 1 -3 3H6a3 3 0 0 1 -3-3V7a3 3 0 0 1 3-3z", "M7 4v16"),
}

@Composable
internal fun TransportGlyph(icon: TransportIcon, size: Dp, tint: Color, modifier: Modifier = Modifier) {
    val paths = remember(icon) { icon.paths.map { PathParser().parsePathString(it).toPath() } }
    Canvas(modifier.size(size)) {
        val k = this.size.minDimension / 24f
        scale(k, k, pivot = androidx.compose.ui.geometry.Offset.Zero) {
            val style = if (icon.filled) Fill else Stroke(1.8f, cap = StrokeCap.Round, join = StrokeJoin.Round)
            paths.forEach { p: Path -> drawPath(p, tint, style = style) }
        }
    }
}

/** Um botão da barra: 40 de largura, o ícone de 17 no meio (o mockup desenha 28 × 28). */
@Composable
internal fun TransportIconButton(
    icon: TransportIcon,
    description: String,
    onClick: (() -> Unit)?,
    modifier: Modifier = Modifier,
    tint: Color? = null,
    onLongClick: (() -> Unit)? = null,
    height: Dp = ShellDims.Transport,
) {
    Box(
        modifier
            .size(40.dp, height)
            .semantics { contentDescription = description }
            .tocavel(enabled = onClick != null, haptic = true, onLongClick = onLongClick) { onClick?.invoke() },
        contentAlignment = Alignment.Center,
    ) {
        TransportGlyph(icon, 17.dp, tint ?: if (onClick == null) AureaColors.EditorIconDisabled else AureaColors.Text)
    }
}

/** A cor do ▶ tocando: o arco-íris girando (uma volta a cada 2 s). */
@Composable
internal fun rgbCycleColor(running: Boolean, idle: Color): Color {
    if (!running) return idle
    val t = rememberInfiniteTransition(label = "play.rgb")
    val hue by t.animateFloat(0f, 360f, infiniteRepeatable(tween(2000, easing = LinearEasing), RepeatMode.Restart), label = "play.hue")
    return Color.hsv(hue, 0.75f, 1f)
}
