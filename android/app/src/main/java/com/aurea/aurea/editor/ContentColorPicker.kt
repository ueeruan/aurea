package com.aurea.aurea.editor

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.tocavel

/**
 * Omega conteúdo (área 06): o SELETOR DE COR COMPACTO dos editores de conteúdo
 * (popup de cor do modo de texto e painel de desenho), 281 × 128 dp no máximo:
 * quadro saturação × brilho, faixa de matiz vertical e cinco amostras rápidas.
 * A cor sai viva em [onChange]; [onStart]/[onEnd] cercam cada arrasto (um passo
 * de desfazer por gesto, quem chama decide). Par do `ContentColorPicker` do Swift.
 */
@Composable
internal fun ContentColorPicker(
    color: Color,
    onChange: (Color) -> Unit,
    modifier: Modifier = Modifier,
    tag: String = "content.color",
    label: String = "",
    onStart: () -> Unit = {},
    onEnd: () -> Unit = {},
) {
    val hsv = remember(color) { FloatArray(3).also { android.graphics.Color.colorToHSV(color.copy(alpha = 1f).toArgb(), it) } }
    var h by remember(color) { mutableFloatStateOf(hsv[0]) }
    var s by remember(color) { mutableFloatStateOf(hsv[1]) }
    var v by remember(color) { mutableFloatStateOf(hsv[2]) }
    val emit by rememberUpdatedState(onChange)
    val start by rememberUpdatedState(onStart)
    val end by rememberUpdatedState(onEnd)
    fun current() = Color(android.graphics.Color.HSVToColor(floatArrayOf(h, s, v))).copy(alpha = color.alpha)

    Row(modifier.height(128.dp).testTag(tag).semantics { contentDescription = label }, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        // Quadro S × V.
        val hue = Color(android.graphics.Color.HSVToColor(floatArrayOf(h, 1f, 1f)))
        Box(
            Modifier
                .weight(1f)
                .fillMaxHeight()
                .clip(RoundedCornerShape(6.dp))
                .pointerInput(Unit) {
                    awaitEachGesture {
                        val down = awaitFirstDown()
                        start()
                        fun pick(p: Offset) {
                            s = (p.x / size.width).coerceIn(0f, 1f)
                            v = 1f - (p.y / size.height).coerceIn(0f, 1f)
                            emit(current())
                        }
                        pick(down.position)
                        down.consume()
                        while (true) {
                            val e = awaitPointerEvent()
                            val c = e.changes.firstOrNull { it.id == down.id } ?: break
                            c.consume()
                            if (!c.pressed) break
                            pick(c.position)
                        }
                        end()
                    }
                },
        ) {
            Canvas(Modifier.fillMaxSize()) {
                drawRect(Brush.horizontalGradient(listOf(Color.White, hue)))
                drawRect(Brush.verticalGradient(listOf(Color.Transparent, Color.Black)))
                val c = Offset(s * size.width, (1f - v) * size.height)
                drawCircle(Color.Black.copy(alpha = 0.5f), 7.dp.toPx(), c, style = Stroke(3.dp.toPx()))
                drawCircle(Color.White, 7.dp.toPx(), c, style = Stroke(1.5.dp.toPx()))
            }
        }
        // Matiz.
        Box(
            Modifier
                .width(22.dp)
                .fillMaxHeight()
                .clip(RoundedCornerShape(6.dp))
                .pointerInput(Unit) {
                    awaitEachGesture {
                        val down = awaitFirstDown()
                        start()
                        fun pick(p: Offset) {
                            h = ((p.y / size.height).coerceIn(0f, 1f) * 360f).coerceIn(0f, 359.9f)
                            emit(current())
                        }
                        pick(down.position)
                        down.consume()
                        while (true) {
                            val e = awaitPointerEvent()
                            val c = e.changes.firstOrNull { it.id == down.id } ?: break
                            c.consume()
                            if (!c.pressed) break
                            pick(c.position)
                        }
                        end()
                    }
                },
        ) {
            Canvas(Modifier.fillMaxSize()) {
                drawRect(Brush.verticalGradient((0..6).map { Color(android.graphics.Color.HSVToColor(floatArrayOf(it * 60f % 360f, 1f, 1f))) }))
                val y = h / 360f * size.height
                drawRect(Color.White, Offset(0f, y - 1.5.dp.toPx()), androidx.compose.ui.geometry.Size(size.width, 3.dp.toPx()))
            }
        }
        // Amostras rápidas (cores do conteúdo, não do tema).
        Column(Modifier.fillMaxHeight(), verticalArrangement = Arrangement.SpaceBetween) {
            QuickSwatches.forEach { q ->
                Box(
                    Modifier
                        .size(22.dp)
                        .clip(CircleShape)
                        .border(1.dp, AureaColors.Border, CircleShape)
                        .tocavel(shrink = 1f) {
                            start()
                            val o = FloatArray(3).also { android.graphics.Color.colorToHSV(q.toArgb(), it) }
                            h = o[0]; s = o[1]; v = o[2]
                            emit(current())
                            end()
                        },
                ) { Canvas(Modifier.fillMaxSize()) { drawRect(q) } }
            }
        }
    }
}

/** As amostras do seletor compacto (dados de cor do usuário, não papéis do tema). */
private val QuickSwatches = listOf(
    Color(0xFFFFFFFF), Color(0xFF000000), Color(0xFFFF4D4D), Color(0xFFFFC233), Color(0xFF3DA5FF),
)

/** A amostra redonda de 24,5 dp (botão de cor da barra de estilo e do desenho). */
@Composable
internal fun ColorDot(color: Color, size: androidx.compose.ui.unit.Dp = 24.5.dp, modifier: Modifier = Modifier) {
    Box(modifier.size(size).clip(CircleShape).border(1.dp, AureaColors.Text.copy(alpha = 0.6f), CircleShape)) {
        Canvas(Modifier.fillMaxSize()) { drawRect(color) }
    }
}
