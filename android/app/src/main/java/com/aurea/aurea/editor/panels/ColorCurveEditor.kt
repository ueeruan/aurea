package com.aurea.aurea.editor.panels

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.ui.theme.AureaColors
import kotlin.math.roundToInt

/** The graph uses the same native spline as the rendered LUT. */
@Composable
internal fun ColorCurveEditor(env: PanelEnv, effect: Int, param: Int) {
    val store = env.store
    val layer = store.primary ?: return
    var channel by remember(layer, effect) { mutableIntStateOf(0) }
    var selected by remember(layer, effect) { mutableIntStateOf(0) }
    var points by remember(layer, effect, param) { mutableStateOf(FloatArray(0)) }
    var samples by remember(layer, effect, param) { mutableStateOf(FloatArray(0)) }
    var gesturing by remember { mutableStateOf(false) }
    fun reload() {
        points = store.effectCurve(effect, param, channel)
        samples = store.effectCurve(effect, param, channel, true)
        selected = selected.coerceIn(0, (points.size / 2 - 1).coerceAtLeast(0))
    }
    fun begin() { if (!gesturing) { gesturing = true; store.beginGesture("curva de cor") } }
    fun finish() { if (gesturing) { gesturing = false; store.endGesture(); reload() } }
    fun edit(action: Int, x: Float = 0f, y: Float = 0f) {
        val result = store.editEffectCurve(effect, param, channel, action, selected, x, y)
        if (result >= 0) selected = result
        reload()
    }
    LaunchedEffect(layer, effect, param, channel, store.sceneSettingsRevision) { if (!gesturing) reload() }
    DisposableEffect(layer, effect, param) { onDispose { finish() } }
    val channelLabels = listOf("RGB", stringResource(R.string.curve_color_red), stringResource(R.string.curve_color_green), stringResource(R.string.curve_color_blue))
    val graphLabel = stringResource(R.string.curve_color_graph)
    val color = listOf(AureaColors.Text, Color(0xffff7474), Color(0xff68d69b), Color(0xff73b8ff))[channel]
    Column(Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            channelLabels.forEachIndexed { index, label ->
                FilterChip(channel == index, { finish(); channel = index; selected = 0 },
                    label = { Text(if (index == 0) label else listOf("", "R", "G", "B")[index]) },
                    modifier = Modifier.weight(1f).heightIn(min = 44.dp).testTag("fx.curve.channel.$index").semantics { contentDescription = label })
            }
        }
        Canvas(Modifier.fillMaxWidth().height(190.dp).background(AureaColors.Chip)
            .testTag("fx.curve.graph").semantics { contentDescription = graphLabel }
            .pointerInput(layer, effect, param, channel) {
                awaitEachGesture {
                    val down = awaitFirstDown()
                    val pad = 18.dp.toPx()
                    val width = (size.width - pad * 2).coerceAtLeast(1f)
                    val height = (size.height - pad * 2).coerceAtLeast(1f)
                    fun xy(at: Offset) = Offset((at.x - pad) / width, 1f - (at.y - pad) / height)
                    val nearest = (0 until points.size / 2).minByOrNull {
                        val at = Offset(pad + points[it * 2] * width, pad + (1f - points[it * 2 + 1]) * height)
                        (at - down.position).getDistance()
                    }
                    val hit = nearest?.takeIf {
                        val at = Offset(pad + points[it * 2] * width, pad + (1f - points[it * 2 + 1]) * height)
                        (at - down.position).getDistance() <= 24.dp.toPx()
                    }
                    begin()
                    try {
                        if (hit != null) selected = hit else { val at = xy(down.position); edit(1, at.x, at.y) }
                        down.consume()
                        do {
                            val event = awaitPointerEvent()
                            val change = event.changes.firstOrNull { it.id == down.id } ?: break
                            if (change.pressed && change.position != change.previousPosition) {
                                val at = xy(change.position); edit(0, at.x, at.y)
                            }
                            change.consume()
                        } while (event.changes.any { it.pressed })
                    } finally { finish() }
                }
            }) {
            val pad = 18.dp.toPx(); val w = size.width - 2 * pad; val h = size.height - 2 * pad
            fun at(x: Float, y: Float) = Offset(pad + x * w, pad + (1 - y) * h)
            for (i in 0..4) {
                val v = i / 4f
                drawLine(AureaColors.Muted.copy(alpha = .18f), at(v, 0f), at(v, 1f))
                drawLine(AureaColors.Muted.copy(alpha = .18f), at(0f, v), at(1f, v))
            }
            drawLine(AureaColors.Muted.copy(alpha = .35f), at(0f, 0f), at(1f, 1f))
            val path = Path()
            samples.forEachIndexed { i, y -> val p = at(i.toFloat() / (samples.size - 1), y); if (i == 0) path.moveTo(p.x, p.y) else path.lineTo(p.x, p.y) }
            drawPath(path, color, style = androidx.compose.ui.graphics.drawscope.Stroke(2.dp.toPx()))
            for (i in 0 until points.size / 2) {
                val p = at(points[i * 2], points[i * 2 + 1])
                drawCircle(if (i == selected) AureaColors.Accent else color, if (i == selected) 7.dp.toPx() else 5.dp.toPx(), p)
                drawCircle(AureaColors.Chip, 2.dp.toPx(), p)
            }
        }
        Text(stringResource(R.string.curve_color_hint), color = AureaColors.Muted, style = MaterialTheme.typography.bodySmall)
        Row(Modifier.horizontalScroll(rememberScrollState())) {
            for (i in 0 until points.size / 2) {
                val label = stringResource(R.string.curve_color_point) + " ${i + 1}"
                FilterChip(selected == i, { finish(); selected = i }, { Text("${i + 1}") },
                    modifier = Modifier.heightIn(min = 44.dp).padding(end = 4.dp).testTag("fx.curve.point.$i").semantics { contentDescription = label })
            }
        }
        if (points.size >= 4) {
            for (axis in 0..1) {
                val label = stringResource(if (axis == 0) R.string.curve_color_input else R.string.curve_color_output)
                val value = points[selected * 2 + axis]
                Text("$label ${(value * 255).roundToInt()}", color = AureaColors.Text)
                Slider(value, onValueChange = { begin(); edit(0, if (axis == 0) it else points[selected * 2], if (axis == 1) it else points[selected * 2 + 1]) },
                    onValueChangeFinished = { finish() }, enabled = axis == 1 || selected in 1 until points.size / 2 - 1,
                    modifier = Modifier.testTag("fx.curve.axis.$axis").semantics { contentDescription = label })
            }
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            TextButton({ finish(); edit(3) }, Modifier.heightIn(min = 44.dp).testTag("fx.curve.reset")) { Text(stringResource(R.string.panel_redefinir)) }
            TextButton({ finish(); edit(2) }, Modifier.heightIn(min = 44.dp).testTag("fx.curve.delete"), enabled = selected in 1 until points.size / 2 - 1) { Text(stringResource(R.string.common_delete)) }
        }
    }
}
