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

/**
 * Faixa vertical do gráfico de velocidade, em log2: a MESMA do motor
 * (`edit_speed_key`: 0,05× a 16×). Antes era 0,1×–10×: a chave de 16× (ou o
 * 0,08× do preset "bala") era desenhada presa na borda e tocar nela já a
 * reescrevia para 10×.
 */
private const val SPEED_MIN = 0.05f
private const val SPEED_MAX = 16f
private const val SPEED_LOG_MIN = -4.321928f
private const val SPEED_LOG_MAX = 4f

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
                // O ponto é desenhado e tocado no MESMO lugar (preso na faixa).
                fun yOf(v: Float) = (pad + h() * (1f - (log2(v) - SPEED_LOG_MIN) / (SPEED_LOG_MAX - SPEED_LOG_MIN))).coerceIn(pad, pad + h())
                fun timeAt(x: Float) = lo + (((x - left) / w()).coerceIn(0f, 1f) * (hi - lo)).roundToInt()
                awaitEachGesture {
                    val down = awaitFirstDown()
                    down.consume()
                    val hit = if (!editable) -1 else readKeys.indices.minByOrNull { i ->
                        hypot(xOf(readKeys[i].first) - down.position.x, yOf(readKeys[i].second) - down.position.y)
                    }?.takeIf { i -> hypot(xOf(readKeys[i].first) - down.position.x, yOf(readKeys[i].second) - down.position.y) <= 22.dp.toPx() } ?: -1
                    var moving = false
                    if (hit >= 0) readSelect(hit) else readScrub(timeAt(down.position.x))
                    // Ponto pego: o arrasto é RELATIVO ao valor dele. Só o eixo que
                    // o dedo moveu muda — arrastar de lado não reescreve a velocidade
                    // (antes o valor saía da altura do dedo, arredondado e preso).
                    val origT = if (hit >= 0) readKeys[hit].first else 0
                    val origV = if (hit >= 0) readKeys[hit].second else 1f
                    val slop = 6.dp.toPx()
                    while (true) {
                        val change = awaitPointerEvent().changes.firstOrNull { it.id == down.id } ?: break
                        if (!change.pressed) break
                        change.consume()
                        if (hit >= 0) {
                            val delta = change.position - down.position
                            if (!moving && delta.getDistance() < 4.dp.toPx()) continue
                            if (!moving) { moving = true; readMoveStart() }
                            val t = if (abs(delta.x) > slop) timeAt(xOf(origT) + delta.x) else origT
                            val v = if (abs(delta.y) > slop) {
                                val log = log2(origV) - delta.y / h() * (SPEED_LOG_MAX - SPEED_LOG_MIN)
                                snapGraphSpeed(2f.pow(log)).coerceIn(SPEED_MIN, SPEED_MAX)
                            } else origV
                            readMove(hit, t, v)
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
                // Linhas de referência: 0,1× · 0,25× · 0,5× · 1× · 2× · 4× · 8× · 16×.
                for (v in floatArrayOf(0.1f, 0.25f, 0.5f, 1f, 2f, 4f, 8f, 16f)) {
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
    for (v in floatArrayOf(0.1f, 0.25f, 0.5f, 1f, 2f, 4f, 8f, 16f)) if (abs(s - v) / v < 0.04f) return v
    return ((s * 100f).roundToInt() / 100f).coerceAtLeast(SPEED_MIN)
}

private fun graphSpeedLabel(v: Float): String = when {
    v < 1f -> com.aurea.aurea.ui.ds.numeroPtBr(v, 2).trimEnd('0') + "x"
    else -> "${v.roundToInt()}x"
}
