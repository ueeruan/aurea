package com.aurea.aurea.editor.panels

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * Curva de tempo (remapeamento): quadro da FONTE (vertical) em função do tempo
 * da camada (horizontal). Tocar na curva cria um ponto (sem mudar nada até
 * mover); arrastar move; segurar apaga. A inclinação é a velocidade: reta
 * deitada = congelado, descendo = de trás para a frente.
 */
@Composable
internal fun TimeRemapGraph(store: EditorStore) {
    val data by remember(store) { derivedStateOf { store.timeRemap } }
    val q = data ?: return
    var selected by remember(store.primary) { mutableIntStateOf(-1) }
    val n = q[0].toInt()
    if (n < 1) return
    val t0 = q[1]
    val t1 = max(q[2], t0 + 1f)
    val keys = (0 until n).map { i -> FloatArray(7) { q[5 + i * 7 + it] } }
    val srcMax = if (q[3] > 0f) q[3] else max(1f, keys.maxOf { it[1] } * 1.2f)
    val playhead = (store.detail?.localPlayhead ?: 0).toFloat()
    val pad = 14f

    Column(Modifier.fillMaxWidth()) {
        Box(
            Modifier.fillMaxWidth().height(170.dp).clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip),
        ) {
            Canvas(
                Modifier
                    .fillMaxWidth()
                    .height(170.dp)
                    .pointerInput(n, t0, t1, srcMax) {
                        val w = size.width.toFloat()
                        val h = size.height.toFloat()
                        fun px(k: FloatArray) = Offset(pad + (k[0] - t0) / (t1 - t0) * (w - 2 * pad), h - pad - k[1] / srcMax * (h - 2 * pad))
                        fun frameAt(x: Float) = (t0 + (x - pad) / (w - 2 * pad) * (t1 - t0)).roundToInt().toLong()
                        fun valueAt(y: Float) = ((h - pad - y) / (h - 2 * pad) * srcMax).coerceIn(0f, srcMax)
                        fun hit(o: Offset): Int {
                            val cur = store.timeRemap ?: return -1
                            val m = cur[0].toInt()
                            var best = -1
                            var bestD = 28.dp.toPx()
                            for (i in 0 until m) {
                                val p = px(FloatArray(7) { cur[5 + i * 7 + it] })
                                val d = hypot(p.x - o.x, p.y - o.y)
                                if (d < bestD) { bestD = d; best = i }
                            }
                            return best
                        }
                        detectTapGestures(
                            onTap = { o ->
                                val i = hit(o)
                                selected = if (i >= 0) i else store.remapInsert(frameAt(o.x))
                            },
                            onLongPress = { o ->
                                val i = hit(o)
                                if (i >= 0 && store.remapRemove(i)) selected = -1
                            },
                        )
                    }
                    .pointerInput(n, t0, t1, srcMax, "arrasto") {
                        val w = size.width.toFloat()
                        val h = size.height.toFloat()
                        fun px(k: FloatArray) = Offset(pad + (k[0] - t0) / (t1 - t0) * (w - 2 * pad), h - pad - k[1] / srcMax * (h - 2 * pad))
                        var dragging = -1
                        var pos = Offset.Zero
                        detectDragGestures(
                            onDragStart = { o ->
                                val cur = store.timeRemap
                                dragging = -1
                                if (cur != null) {
                                    var bestD = 28.dp.toPx()
                                    for (i in 0 until cur[0].toInt()) {
                                        val p = px(FloatArray(7) { cur[5 + i * 7 + it] })
                                        val d = hypot(p.x - o.x, p.y - o.y)
                                        if (d < bestD) { bestD = d; dragging = i }
                                    }
                                }
                                pos = o
                                if (dragging >= 0) {
                                    selected = dragging
                                    store.beginGesture("mover ponto da curva de tempo")
                                }
                            },
                            onDrag = { change, delta ->
                                if (dragging >= 0) {
                                    change.consume()
                                    pos += delta
                                    val f = (t0 + (pos.x - pad) / (w - 2 * pad) * (t1 - t0)).roundToInt().toLong()
                                    val v = ((h - pad - pos.y) / (h - 2 * pad) * srcMax).coerceIn(0f, srcMax)
                                    store.remapMove(dragging, f, v)
                                }
                            },
                            onDragEnd = { if (dragging >= 0) store.endGesture(); dragging = -1 },
                            onDragCancel = { if (dragging >= 0) store.endGesture(); dragging = -1 },
                        )
                    },
            ) {
                val w = size.width
                val h = size.height
                fun x(t: Float) = pad + (t - t0) / (t1 - t0) * (w - 2 * pad)
                fun y(v: Float) = h - pad - v / srcMax * (h - 2 * pad)
                // Grade: quartos do tempo e da fonte.
                for (i in 1..3) {
                    val gx = pad + (w - 2 * pad) * i / 4f
                    val gy = pad + (h - 2 * pad) * i / 4f
                    drawLine(Color.White.copy(alpha = 0.06f), Offset(gx, pad), Offset(gx, h - pad), 1f)
                    drawLine(Color.White.copy(alpha = 0.06f), Offset(pad, gy), Offset(w - pad, gy), 1f)
                }
                // A curva, trecho a trecho, com a interpolação do ponto da esquerda.
                val path = Path()
                path.moveTo(x(keys[0][0]), y(keys[0][1]))
                for (i in 0 until keys.size - 1) {
                    val a = keys[i]
                    val b = keys[i + 1]
                    val ease = Ease(a[2].toInt(), a[3], a[4], a[5], a[6])
                    for (s in 1..32) {
                        val u = s / 32f
                        val v = a[1] + (b[1] - a[1]) * ease.transform(u)
                        path.lineTo(x(a[0] + (b[0] - a[0]) * u), y(v))
                    }
                }
                drawPath(path, AureaColors.Accent, style = Stroke(width = 2.5.dp.toPx()))
                // Cabeçote.
                if (playhead in t0..t1) {
                    drawLine(Color(0xFFFF5A5A), Offset(x(playhead), pad * 0.5f), Offset(x(playhead), h - pad * 0.5f), 1.5.dp.toPx())
                }
                keys.forEachIndexed { i, k ->
                    val c = Offset(x(k[0]), y(k[1]))
                    drawCircle(Color.Black.copy(alpha = 0.5f), 8.dp.toPx(), c)
                    drawCircle(if (i == selected) Color.White else AureaColors.Accent, 6.dp.toPx(), c)
                }
            }
        }
        Spacer(Modifier.height(6.dp))
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            val speed = q[4]
            val label = when {
                kotlin.math.abs(speed) < 0.005f -> "Congelado no cabeçote"
                speed < 0f -> "Velocidade no cabeçote: ${"%.2f".format(-speed)}× ao contrário"
                else -> "Velocidade no cabeçote: ${"%.2f".format(speed)}×"
            }
            Text(label, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
        }
        if (selected in 0 until n) {
            Spacer(Modifier.height(6.dp))
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Text("Ponto ${selected + 1}", style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
                val cur = keys[selected][2].toInt()
                listOf(Interp.LINEAR to "Linear", Interp.EASE_IN_OUT to "Suave", Interp.HOLD to "Congelar").forEach { (m, text) ->
                    val on = cur == m
                    Box(
                        Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                            .tocavel(onClick = { store.remapInterp(selected, m) }).padding(horizontal = 10.dp, vertical = 6.dp),
                    ) {
                        Text(text, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
                    }
                }
            }
        }
        Spacer(Modifier.height(4.dp))
        Text(
            "Toque na curva para criar um ponto, arraste para mudar o tempo, segure para apagar.",
            style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, lineHeight = 15.sp, color = AureaColors.Muted)),
        )
    }
}
