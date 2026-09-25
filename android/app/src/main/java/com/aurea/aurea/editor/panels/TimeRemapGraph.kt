package com.aurea.aurea.editor.panels

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.calculatePan
import androidx.compose.foundation.gestures.calculateZoom
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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
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
internal fun TimeRemapGraph(store: EditorStore, expanded: Boolean = false) {
    var fullscreen by remember { mutableStateOf(false) }
    val graphHeight = if (expanded) max(240, androidx.compose.ui.platform.LocalConfiguration.current.screenHeightDp - 240).dp else 240.dp
    if (fullscreen) androidx.compose.ui.window.Dialog(onDismissRequest = { fullscreen = false }, properties = androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth = false)) {
        Column(Modifier.fillMaxWidth().background(AureaColors.Surface).padding(16.dp)) {
            androidx.compose.material3.TextButton(onClick = { fullscreen = false }) { Text(stringResource(R.string.editor_sair_tela_cheia)) }
            TimeRemapGraph(store, expanded = true)
        }
    }
    val data by remember(store) { derivedStateOf { store.timeRemap } }
    val q = data ?: return
    var selected by remember(store.primary) { mutableIntStateOf(-1) }
    val n = q[0].toInt()
    if (n < 1) return
    val fitFrom = q[1]
    val fitTo = max(q[2], fitFrom + 1f)
    val keys = (0 until n).map { i -> FloatArray(7) { q[5 + i * 7 + it] } }
    val fitMax = if (q[3] > 0f) q[3] else max(1f, keys.maxOf { it[1] } * 1.2f)
    var viewport by remember(store.primary) { mutableStateOf(GraphViewport(fitFrom, fitTo, 0f, fitMax)) }
    val t0 = viewport.from
    val t1 = viewport.to
    val srcMin = viewport.low
    val srcMax = viewport.high
    val srcRange = viewport.range
    val playhead = (store.detail?.localPlayhead ?: 0).toFloat()
    val pad = with(androidx.compose.ui.platform.LocalDensity.current) { 24.dp.toPx() }

    Column(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            androidx.compose.material3.TextButton(onClick = { viewport = viewport.transform(1/1.5f, 0f, 0f) }) { Text("−") }
            androidx.compose.material3.TextButton(onClick = { viewport = viewport.transform(1.5f, 0f, 0f) }) { Text("+") }
            androidx.compose.material3.TextButton(onClick = { viewport = GraphViewport(fitFrom, fitTo, 0f, fitMax) }) { Text(stringResource(R.string.panel_ajustar)) }
            if (!expanded) androidx.compose.material3.TextButton(onClick = { fullscreen = true }) { Text(stringResource(R.string.panel_expandir)) }
        }
        Box(
            Modifier.fillMaxWidth().height(graphHeight).clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip),
        ) {
            Canvas(
                Modifier
                    .fillMaxWidth()
                    .height(graphHeight)
                    .pointerInput(store.primary) {
                        awaitEachGesture {
                            val down = awaitFirstDown()
                            down.consume() // The graph owns this contact, not its parent ScrollView.
                            val initial = viewport
                            val w = max(1f, size.width - 2 * pad)
                            val h = max(1f, size.height - 2 * pad)
                            val cur = store.timeRemap ?: return@awaitEachGesture
                            val points = (0 until cur[0].toInt()).map { i ->
                                GraphHitPoint(i, pad + (cur[5+i*7] - initial.from) / initial.duration * w,
                                    size.height-pad-(cur[6+i*7]-initial.low)/initial.range*h)
                            }
                            var dragging = graphHitIndex(points, down.position.x, down.position.y, 24.dp.toPx(), selected)
                            val picked = points.firstOrNull { it.index == dragging }
                            val grab = picked?.let { Offset(it.x, it.y) - down.position } ?: Offset.Zero
                            if (dragging >= 0) selected = dragging
                            var began = false
                            var moved = false
                            try {
                                while (true) {
                                    val event = awaitPointerEvent()
                                    val change = event.changes.firstOrNull { it.id == down.id } ?: break
                                    if (!change.pressed) {
                                        if (!moved) {
                                            if (dragging >= 0 && change.uptimeMillis - down.uptimeMillis >= viewConfiguration.longPressTimeoutMillis) {
                                                if (store.remapRemove(dragging)) selected = -1
                                            } else if (dragging < 0) {
                                                val frame = (initial.from + (down.position.x-pad)/w*initial.duration).roundToInt().toLong()
                                                selected = store.remapInsert(frame)
                                            }
                                        }
                                        break
                                    }
                                    val displacement = change.position - down.position
                                    if (!moved && displacement.getDistance() < viewConfiguration.touchSlop && event.changes.count { it.pressed } < 2) continue
                                    moved = true
                                    if (event.changes.count { it.pressed } > 1) {
                                        if (began) { store.endGesture(); began = false }
                                        dragging = -1
                                    }
                                    if (dragging >= 0) {
                                        if (!began) { store.beginGesture("mover ponto da curva de tempo"); began = true }
                                        val position = change.position + grab
                                        val frame = (initial.from+(position.x-pad)/w*initial.duration).roundToInt().toLong()
                                        val source = (initial.low+(size.height-pad-position.y)/h*initial.range).coerceAtLeast(0f)
                                        val movedIndex = store.remapMove(dragging, frame, source)
                                        if (movedIndex >= 0) { dragging = movedIndex; selected = movedIndex }
                                    } else {
                                        val pan = event.calculatePan()
                                        viewport = viewport.transform(event.calculateZoom(), pan.x/w, pan.y/h)
                                    }
                                    event.changes.forEach { it.consume() }
                                }
                            } finally { if (began) store.endGesture() }
                        }
                    },
            ) {
                val w = size.width
                val h = size.height
                fun x(t: Float) = pad + (t - t0) / (t1 - t0) * (w - 2 * pad)
                fun y(v: Float) = h - pad - (v - srcMin) / srcRange * (h - 2 * pad)
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
                kotlin.math.abs(speed) < 0.005f -> stringResource(R.string.panel_congelado_cabecote)
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
                listOf(Interp.LINEAR to stringResource(R.string.panel_linear), Interp.EASE_IN_OUT to stringResource(R.string.panel_suave), Interp.HOLD to stringResource(R.string.panel_congelar)).forEach { (m, text) ->
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
            stringResource(R.string.panel_toque_curva_criar_ponto_arraste_mudar),
            style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, lineHeight = 15.sp, color = AureaColors.Muted)),
        )
    }
}
