package com.aurea.aurea.editor.panels

import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.ui.theme.AureaColors
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.ln
import kotlin.math.pow
import kotlin.math.roundToInt

/** Faixa vertical do gráfico de velocidade, em log2: 0,1× embaixo, 10× em cima (1× no meio). */
private const val SPEED_LOG_MIN = -3.3219f
private const val SPEED_LOG_MAX = 3.3219f

/**
 * GRÁFICO DE VELOCIDADE (0.1.8, pedido dos testers: "velocidade como no AE").
 * Horizontal = tempo da camada (início..fim), vertical = velocidade em escala
 * logarítmica (meio risco para 0,5× vale o mesmo que para 2×). A curva vem do
 * motor (`query_speed_curve`): é exatamente a que a prévia e o export usam.
 *
 * Editável (aba Curva): tocar perto de um ponto o escolhe e arrastar move
 * (para cima acelera, para os lados muda o momento); tocar/arrastar no resto
 * move o cabeçote. Só leitura (aba Remapear): mostra a velocidade que a curva
 * de tempo dá e move o cabeçote.
 */
@Composable
internal fun SpeedGraph(
    data: FloatArray,
    playhead: Int,
    editable: Boolean,
    selected: Int,
    description: String,
    onSelect: (Int) -> Unit,
    onMoveStart: () -> Unit,
    onMove: (index: Int, local: Int, speed: Float) -> Unit,
    onMoveEnd: () -> Unit,
    onScrub: (local: Int) -> Unit,
) {
    val samples = data[0].toInt().coerceAtLeast(0)
    val keyCount = data[1].toInt().coerceAtLeast(0)
    val lo = data[2].roundToInt()
    val hi = maxOf(lo + 1, data[3].roundToInt())
    val keys = (0 until keyCount).mapNotNull { i ->
        val at = 8 + samples + i * 3
        if (at + 1 < data.size) Pair(data[at].roundToInt(), data[at + 1]) else null
    }
    val readKeys by rememberUpdatedState(keys)
    val readSelect by rememberUpdatedState(onSelect)
    val readMoveStart by rememberUpdatedState(onMoveStart)
    val readMove by rememberUpdatedState(onMove)
    val readMoveEnd by rememberUpdatedState(onMoveEnd)
    val readScrub by rememberUpdatedState(onScrub)
    val measurer = rememberTextMeasurer()
    val accent = AureaColors.Accent
    val text = AureaColors.Text
    val muted = AureaColors.Muted
    val grid = AureaColors.Chip
    val head = AureaColors.Playhead
    val labelStyle = TextStyle(fontSize = 9.sp, color = muted)
    Box(
        Modifier.fillMaxWidth().height(176.dp).clip(RoundedCornerShape(10.dp)).semantics { contentDescription = description }
            .pointerInput(lo, hi, editable) {
                val left = 30.dp.toPx()
                val pad = 10.dp.toPx()
                fun w() = (size.width - left - pad).coerceAtLeast(1f)
                fun h() = (size.height - pad * 2).coerceAtLeast(1f)
                fun xOf(t: Int) = left + w() * ((t - lo).toFloat() / (hi - lo))
                fun yOf(v: Float) = pad + h() * (1f - (log2(v) - SPEED_LOG_MIN) / (SPEED_LOG_MAX - SPEED_LOG_MIN))
                fun timeAt(x: Float) = lo + (((x - left) / w()).coerceIn(0f, 1f) * (hi - lo)).roundToInt()
                fun speedAt(y: Float): Float {
                    val u = (1f - (y - pad) / h()).coerceIn(0f, 1f)
                    return snapGraphSpeed(2f.pow(SPEED_LOG_MIN + u * (SPEED_LOG_MAX - SPEED_LOG_MIN)))
                }
                awaitEachGesture {
                    val down = awaitFirstDown()
                    down.consume()
                    val hit = if (!editable) -1 else readKeys.indices.minByOrNull { i ->
                        hypot(xOf(readKeys[i].first) - down.position.x, yOf(readKeys[i].second) - down.position.y)
                    }?.takeIf { i -> hypot(xOf(readKeys[i].first) - down.position.x, yOf(readKeys[i].second) - down.position.y) <= 22.dp.toPx() } ?: -1
                    var moving = false
                    if (hit >= 0) readSelect(hit) else readScrub(timeAt(down.position.x))
                    // Ponto pego: guarda a distância do dedo ao ponto (o ponto não pula para o dedo).
                    val grabDx = if (hit >= 0) xOf(readKeys[hit].first) - down.position.x else 0f
                    val grabDy = if (hit >= 0) yOf(readKeys[hit].second) - down.position.y else 0f
                    while (true) {
                        val change = awaitPointerEvent().changes.firstOrNull { it.id == down.id } ?: break
                        if (!change.pressed) break
                        change.consume()
                        if (hit >= 0) {
                            if (!moving && (change.position - down.position).getDistance() < 4.dp.toPx()) continue
                            if (!moving) { moving = true; readMoveStart() }
                            readMove(hit, timeAt(change.position.x + grabDx), speedAt(change.position.y + grabDy))
                        } else {
                            readScrub(timeAt(change.position.x))
                        }
                    }
                    if (moving) readMoveEnd()
                }
            }
            .drawBehind {
                val left = 30.dp.toPx()
                val pad = 10.dp.toPx()
                val w = (size.width - left - pad).coerceAtLeast(1f)
                val h = (size.height - pad * 2).coerceAtLeast(1f)
                fun xOf(t: Float) = left + w * ((t - lo) / (hi - lo))
                fun yOf(v: Float) = pad + h * (1f - (log2(v) - SPEED_LOG_MIN) / (SPEED_LOG_MAX - SPEED_LOG_MIN))
                drawRect(AureaColors.Surface.copy(alpha = 0.6f))
                // Linhas de referência: 0,25× · 0,5× · 1× · 2× · 4× · 8×.
                for (v in floatArrayOf(0.25f, 0.5f, 1f, 2f, 4f, 8f)) {
                    val y = yOf(v)
                    drawLine(if (v == 1f) muted.copy(alpha = 0.6f) else grid, Offset(left, y), Offset(size.width - pad, y), strokeWidth = if (v == 1f) 1.5f else 1f)
                    val label = measurer.measure(graphSpeedLabel(v), labelStyle)
                    drawText(label, topLeft = Offset(left - label.size.width - 4.dp.toPx(), y - label.size.height / 2f))
                }
                if (samples >= 2) {
                    val line = Path()
                    val fill = Path()
                    for (i in 0 until samples) {
                        val t = lo + (hi - lo) * (i.toFloat() / (samples - 1))
                        val p = Offset(xOf(t), yOf(data[8 + i]).coerceIn(pad, pad + h))
                        if (i == 0) { line.moveTo(p.x, p.y); fill.moveTo(p.x, pad + h); fill.lineTo(p.x, p.y) } else { line.lineTo(p.x, p.y); fill.lineTo(p.x, p.y) }
                    }
                    fill.lineTo(xOf(hi.toFloat()), pad + h)
                    fill.close()
                    drawPath(fill, accent.copy(alpha = 0.14f))
                    drawPath(line, accent, style = Stroke(width = 2.dp.toPx()))
                }
                keys.forEachIndexed { i, (t, v) ->
                    val c = Offset(xOf(t.toFloat()), yOf(v).coerceIn(pad, pad + h))
                    val on = i == selected
                    drawCircle(if (on) accent else text, radius = if (on) 7.dp.toPx() else 5.5.dp.toPx(), center = c)
                    drawCircle(AureaColors.Surface, radius = if (on) 3.dp.toPx() else 2.5.dp.toPx(), center = c)
                }
                val px = xOf(playhead.coerceIn(lo, hi).toFloat())
                drawLine(head, Offset(px, pad * 0.4f), Offset(px, size.height - pad * 0.4f), strokeWidth = 2.dp.toPx())
            },
    )
}

private fun log2(v: Float): Float = ln(v.coerceIn(0.01f, 64f)) / ln(2f)

/** Encosta nos valores redondos do gráfico quando passa perto. */
private fun snapGraphSpeed(s: Float): Float {
    for (v in floatArrayOf(0.25f, 0.5f, 1f, 2f, 4f, 8f)) if (abs(s - v) / v < 0.04f) return v
    return (s * 100f).roundToInt() / 100f
}

private fun graphSpeedLabel(v: Float): String = when {
    v < 1f -> com.aurea.aurea.ui.ds.numeroPtBr(v, 2).trimEnd('0') + "x"
    else -> "${v.roundToInt()}x"
}
