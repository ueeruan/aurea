package com.aurea.aurea.editor.panels

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.calculateCentroid
import androidx.compose.foundation.gestures.calculatePan
import androidx.compose.foundation.gestures.calculateZoom
import androidx.compose.foundation.layout.*
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
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
import androidx.compose.ui.platform.testTag
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
    val handles = remember(layer, track, revision, speed, fps) { if (speed) speedHandles(store, layer, track, fps) else emptyList() }
    val currentHandles by rememberUpdatedState(handles)
    val fullFrom = first.time
    val fullTo = max(fullFrom + 1, track.last().time)
    val full = remember(layer, first.property, first.effectIndex, first.paramIndex, fullFrom, fullTo, revision, speed, fps) {
        graphSamples(store.queryTrackCurve(layer, first, fullFrom, fullTo), fullFrom, fullTo, fps, speed)
    }
    fun fit(): GraphViewport {
        val values = if (speed) full.map { it.value } + handles.map { it.velocity } + 0f else full.map { it.value } + track.map { it.value }
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
    var multi by remember(layer, first.property, first.effectIndex, first.paramIndex) { mutableStateOf(false) }
    var picked by remember(layer, first.property, first.effectIndex, first.paramIndex) { mutableStateOf(emptySet<Int>()) }
    LaunchedEffect(track) { picked = picked.intersect(track.map { it.time }.toSet()) }
    val selected = store.selectedKeyframe?.second?.time
    Column(Modifier.fillMaxSize()) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            if (!speed) TextButton(onClick = { multi = !multi; if (!multi) picked = emptySet() }, modifier = Modifier.testTag("curve.multi")) {
                Text(if (multi) "Done (${picked.size})" else "Select", fontSize = 11.sp)
            }
            Text(if (speed) "${"%.3g".format(view.high)} /s" else "%.3g".format(view.high), fontSize = 10.sp, color = AureaColors.Muted, modifier = Modifier.weight(1f))
            Text("−", Modifier.size(48.dp).tocavel { view = view.transform(1 / 1.5f, 0f, 0f) }.wrapContentSize(), color = Color.White)
            Text("+", Modifier.size(48.dp).tocavel { view = view.transform(1.5f, 0f, 0f) }.wrapContentSize(), color = Color.White)
            Text(stringResource(R.string.panel_ajustar), Modifier.tocavel { view = fit() }.padding(8.dp), fontSize = 11.sp, color = AureaColors.Accent)
        }
        if (multi && !speed) Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.SpaceEvenly) {
            TextButton(onClick = { picked = track.map { it.time }.toSet() }) { Text("All", fontSize = 11.sp) }
            TextButton(onClick = { store.copySelectedKeys(layer, track.filter { it.time in picked }) }, enabled = picked.isNotEmpty()) { Text("Copy", fontSize = 11.sp) }
            TextButton(onClick = {
                val row = store.layers.firstOrNull { it.id == layer }
                val target = track.maxOf { it.time }.toLong() + 1 + (row?.startFrame ?: 0) - (row?.offsetFrames ?: 0)
                if (target in Int.MIN_VALUE.toLong()..Int.MAX_VALUE.toLong() && store.copySelectedKeys(layer, track.filter { it.time in picked })) {
                    store.pasteSelectedKeysAt(layer, target.toInt())
                }
            }, enabled = picked.isNotEmpty()) { Text("Duplicate", fontSize = 11.sp) }
            TextButton(onClick = { store.pasteKeyframes(listOf(layer)) }) { Text("Paste", fontSize = 11.sp) }
            TextButton(onClick = { if (store.editSelectedKeys(layer, track.filter { it.time in picked }, 0, true)) picked = emptySet() }, enabled = picked.isNotEmpty()) { Text("Delete", fontSize = 11.sp) }
        }
        Canvas(Modifier.weight(1f).fillMaxWidth().clipToBounds().testTag("curve.trackGraph").pointerInput(layer, first.property, first.effectIndex, first.paramIndex, speed) {
            awaitEachGesture {
                val down = awaitFirstDown()
                down.consume()
                if (size.width <= 0 || size.height <= 0) return@awaitEachGesture
                val initial = view
                fun point(key: KeyframeRow) = Offset((key.time - initial.from) / initial.duration * size.width,
                    size.height - (key.value - initial.low) / initial.range * size.height)
                val radius = 24.dp.toPx()
                fun handlePoint(h: SpeedHandle) = Offset((h.frame - initial.from) / initial.duration * size.width,
                    size.height - (h.velocity - initial.low) / initial.range * size.height)
                var handle = currentHandles.minByOrNull { (handlePoint(it) - down.position).getDistanceSquared() }
                    ?.takeIf { (handlePoint(it) - down.position).getDistanceSquared() <= radius * radius }
                var key = if (speed) null else currentTrack.minByOrNull { (point(it) - down.position).getDistanceSquared() }
                    ?.takeIf { (point(it) - down.position).getDistanceSquared() <= radius * radius }
                if (key != null) store.selectKeyframe(layer, key)
                var currentTime = key?.time ?: 0
                val previous = key?.let { k -> currentTrack.lastOrNull { it.time < k.time }?.time }
                val next = key?.let { k -> currentTrack.firstOrNull { it.time > k.time }?.time }
                var began = false
                val initialPicked = picked
                val groupKeys = if (multi && key != null) currentTrack.filter { it.time in (if (key.time in picked) picked else setOf(key.time)) } else emptyList()
                var groupDelta = 0
                try {
                    while (true) {
                        val event = awaitPointerEvent()
                        val change = event.changes.firstOrNull { it.id == down.id } ?: break
                        if (!change.pressed) {
                            if (multi && !began && key != null) picked = if (key.time in initialPicked) initialPicked - key.time else initialPicked + key.time
                            break
                        }
                        if (event.changes.count { it.pressed } > 1 && (key != null || handle != null)) {
                            if (began) { store.endGesture(); began = false }
                            key = null
                            handle = null
                        }
                        val activeKey = key
                        val activeHandle = handle
                        if (activeHandle != null) {
                            val delta = change.position - down.position
                            if (!began && delta.getDistance() < 4.dp.toPx()) continue
                            if (!began) { store.beginGesture("editar velocidade do intervalo"); began = true }
                            val h = activeHandle.changed(activeHandle.frame + delta.x / size.width * initial.duration,
                                activeHandle.velocity - delta.y / size.height * initial.range)
                            if (h.all { it.isFinite() }) store.setKeyframeEasing(layer, activeHandle.key, Interp.BEZIER, h[0], h[1], h[2], h[3])
                        } else if (activeKey != null) {
                            val dy = change.position.y - down.position.y
                            val dx = change.position.x - down.position.x
                            if (!began && kotlin.math.hypot(dx, dy) < 4.dp.toPx()) continue
                            if (!began) { store.beginGesture("editar keyframe no gráfico"); began = true }
                            if (multi && groupKeys.isNotEmpty()) {
                                val delta = (dx / size.width * initial.duration).toInt()
                                val step = delta.toLong() - groupDelta
                                if (step in Int.MIN_VALUE.toLong()..Int.MAX_VALUE.toLong() && step != 0L && store.editSelectedKeys(layer, groupKeys.map { it.copy(time = it.time + groupDelta) }, step.toInt())) {
                                    groupDelta = delta
                                    picked = groupKeys.map { it.time + delta }.toSet()
                                }
                                change.consume()
                                continue
                            }
                            val target = graphDragFrame(activeKey.time, dx / size.width * initial.duration, previous, next)
                            var actual = activeKey.copy(time = currentTime)
                            if (target != currentTime) {
                                store.moveKeyframe(layer, actual, target)
                                currentTime = target
                                actual = actual.copy(time = target)
                            }
                            val value = activeKey.value - dy / size.height * initial.range
                            store.setGraphKeyframeValue(layer, actual, value)
                            store.selectKeyframe(layer, actual.copy(value = value))
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
            handles.forEach { h ->
                val p = point(h.frame, h.velocity)
                drawLine(AureaColors.Muted, point((if(h.incoming) h.end.time else h.key.time).toFloat(), h.velocity), p)
                drawCircle(AureaColors.Accent, 7.dp.toPx(), p, style = Stroke(2.dp.toPx()))
            }
            if (!speed) track.forEach { key ->
                drawCircle(if ((multi && key.time in picked) || (!multi && key.time == selected)) Color.White else AureaColors.Accent, 6.dp.toPx(), point(key.time.toFloat(), key.value))
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
