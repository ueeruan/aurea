package com.aurea.aurea.editor.panels

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.calculateCentroid
import androidx.compose.foundation.gestures.calculatePan
import androidx.compose.foundation.gestures.calculateZoom
import androidx.compose.foundation.layout.*
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.abs

/** Actual track values from the shared evaluator. Value dragging writes the
 * original track; speed is a finite frame-interval derivative, in units/s. */
@Composable
internal fun TrackGraph(store: EditorStore, layer: Long, track: List<KeyframeRow>, speed: Boolean) {
    val first = track.firstOrNull() ?: return
    val fps = store.project.fps.coerceAtLeast(1f)
    val revision = store.curveRevision
    val fullFrom = first.time
    val fullTo = max(fullFrom + 1, track.last().time)
    val full = remember(layer, first.property, first.effectIndex, first.paramIndex, fullFrom, fullTo, revision, speed, fps) {
        graphSamples(store.queryTrackCurve(layer, first, fullFrom, fullTo), fullFrom, fullTo, fps, speed)
    }
    fun fit(): GraphViewport {
        val values = if (speed) full.map { it.value } + 0f else full.map { it.value } + track.map { it.value }
        val low = values.minOrNull() ?: 0f
        val high = values.maxOrNull() ?: 1f
        val margin = max(0.1f, max(high - low, abs(high) * 0.05f) * 0.12f)
        val timeMargin = max(1f, (fullTo.toLong() - fullFrom).toFloat() * 0.06f)
        return GraphViewport(fullFrom - timeMargin, fullTo + timeMargin, low - margin, high + margin)
    }
    var view by remember(layer, first.property, first.effectIndex, first.paramIndex, speed) { mutableStateOf(fit()) }
    val from = floor(view.from).toInt()
    val to = max(from + 1, ceil(view.to).toInt())
    val samples = remember(layer, first.property, first.effectIndex, first.paramIndex, from, to, revision, speed, fps) {
        graphSamples(store.queryTrackCurve(layer, first, from, to), from, to, fps, speed)
    }
    val currentTrack by rememberUpdatedState(track)
    val selected = store.selectedKeyframe?.second?.time
    Column(Modifier.fillMaxSize()) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(if (speed) "${"%.3g".format(view.high)} /s" else "%.3g".format(view.high), fontSize = 10.sp, color = AureaColors.Muted, modifier = Modifier.weight(1f))
            Text("−", Modifier.size(36.dp).tocavel { view = view.transform(1 / 1.5f, 0f, 0f) }.wrapContentSize(), color = Color.White)
            Text("+", Modifier.size(36.dp).tocavel { view = view.transform(1.5f, 0f, 0f) }.wrapContentSize(), color = Color.White)
            Text(stringResource(R.string.panel_ajustar), Modifier.tocavel { view = fit() }.padding(8.dp), fontSize = 11.sp, color = AureaColors.Accent)
        }
        Canvas(Modifier.weight(1f).fillMaxWidth().clipToBounds().pointerInput(layer, first.property, first.effectIndex, first.paramIndex, speed) {
            awaitEachGesture {
                val down = awaitFirstDown()
                if (size.width <= 0 || size.height <= 0) return@awaitEachGesture
                val initial = view
                fun point(key: KeyframeRow) = Offset((key.time - initial.from) / initial.duration * size.width,
                    size.height - (key.value - initial.low) / initial.range * size.height)
                val radius = 24.dp.toPx()
                val key = if (speed) null else currentTrack.minByOrNull { (point(it) - down.position).getDistanceSquared() }
                    ?.takeIf { (point(it) - down.position).getDistanceSquared() <= radius * radius }
                if (key != null) store.selectKeyframe(layer, key)
                var began = false
                try {
                    while (true) {
                        val event = awaitPointerEvent()
                        val change = event.changes.firstOrNull { it.id == down.id } ?: break
                        if (!change.pressed) break
                        if (key != null) {
                            if (event.changes.count { it.pressed } > 1) break
                            val dy = change.position.y - down.position.y
                            if (!began && abs(dy) < 1f) continue
                            if (!began) { store.beginGesture("editar valor do keyframe"); began = true }
                            store.setGraphKeyframeValue(layer, key, key.value - dy / size.height * initial.range)
                        } else {
                            val pan = event.calculatePan()
                            val zoom = event.calculateZoom()
                            val center = event.calculateCentroid()
                            if (center.x.isFinite() && center.y.isFinite()) {
                                view = view.transform(zoom, pan.x / size.width, pan.y / size.height,
                                    (center.x / size.width).coerceIn(0f, 1f), (1f - center.y / size.height).coerceIn(0f, 1f))
                            }
                        }
                        event.changes.forEach { it.consume() }
                    }
                } finally { if (began) store.endGesture() }
            }
        }) {
            fun point(frame: Float, value: Float) = Offset((frame - view.from) / view.duration * size.width,
                size.height - (value - view.low) / view.range * size.height)
            for (i in 1..3) {
                drawLine(Color.White.copy(alpha = 0.1f), Offset(size.width * i / 4, 0f), Offset(size.width * i / 4, size.height))
                drawLine(Color.White.copy(alpha = 0.1f), Offset(0f, size.height * i / 4), Offset(size.width, size.height * i / 4))
            }
            val zero = point(view.from, 0f).y
            if (zero in 0f..size.height) drawLine(Color.White.copy(alpha = 0.3f), Offset(0f, zero), Offset(size.width, zero))
            val path = Path()
            samples.forEachIndexed { i, sample ->
                val p = point(sample.frame, sample.value)
                if (i == 0) path.moveTo(p.x, p.y) else path.lineTo(p.x, p.y)
            }
            drawPath(path, AureaColors.Accent, style = Stroke(2.dp.toPx()))
            if (!speed) track.forEach { key ->
                drawCircle(if (key.time == selected) Color.White else AureaColors.Accent, 6.dp.toPx(), point(key.time.toFloat(), key.value))
            }
            store.detail?.localPlayhead?.let { frame ->
                val x = point(frame.toFloat(), 0f).x
                if (x in 0f..size.width) drawLine(AureaColors.Danger, Offset(x, 0f), Offset(x, size.height), 1.dp.toPx())
            }
        }
        Row(Modifier.fillMaxWidth()) {
            Text("${"%.3g".format(view.low)}${if (speed) " /s" else ""}", fontSize = 10.sp, color = AureaColors.Muted, modifier = Modifier.weight(1f))
            Text("${from}–${to} f", fontSize = 10.sp, color = AureaColors.Muted)
        }
    }
}
