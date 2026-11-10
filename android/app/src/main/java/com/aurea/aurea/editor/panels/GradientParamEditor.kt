package com.aurea.aurea.editor.panels

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChange
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.ui.ds.AureaSlider
import com.aurea.aurea.ui.ds.ColorPickerSheet
import com.aurea.aurea.ui.ds.ColorWell
import com.aurea.aurea.ui.ds.displayToEngine
import com.aurea.aurea.ui.ds.engineToDisplay
import com.aurea.aurea.ui.ds.rgbaColor
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * Curva de um plugin: o gráfico captura o arrasto e ocupa o painel, então começa
 * fechado para os parâmetros seguintes continuarem ao alcance.
 */
@Composable
internal fun CollapsibleCurveEditor(env: PanelEnv, effect: Int, param: Int, label: String) {
    var open by remember(effect, param) { mutableStateOf(false) }
    Column(Modifier.fillMaxWidth()) {
        Row(
            Modifier.fillMaxWidth().heightIn(min = 44.dp).testTag("fx.curve.toggle.$param").semantics { contentDescription = label }
                .tocavel(shrink = 1f) { open = !open }.padding(horizontal = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(label, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
            CupertinoIcon(if (open) CupertinoGlyph.ChevronUp else CupertinoGlyph.ChevronDown, 14.dp, AureaColors.Muted)
        }
        if (open) ColorCurveEditor(env, effect, param)
    }
}

private const val MAX_STOPS = 16
private const val MIN_GAP = 0.001f

private fun stopColor(stops: FloatArray, i: Int) = FloatArray(4) { stops[i * 5 + 1 + it] }

/** A parada [i] em [t], sem passar as vizinhas: a ordem não muda durante o gesto. */
private fun moveStop(stops: FloatArray, i: Int, t: Float): FloatArray {
    val last = stops.size / 5 - 1
    val lo = if (i == 0) 0f else stops[(i - 1) * 5] + MIN_GAP
    val hi = if (i == last) 1f else stops[(i + 1) * 5] - MIN_GAP
    return stops.copyOf().also { it[i * 5] = t.coerceIn(lo, maxOf(lo, hi)) }
}

/** Uma parada nova em [t] com a cor que o degradê já tinha ali; devolve o índice dela. */
private fun insertStop(stops: FloatArray, t: Float): Pair<FloatArray, Int> {
    val n = stops.size / 5
    val at = (0 until n).firstOrNull { stops[it * 5] > t } ?: n
    val a = (at - 1).coerceAtLeast(0)
    val b = at.coerceAtMost(n - 1)
    val span = stops[b * 5] - stops[a * 5]
    val k = if (span > 0f) ((t - stops[a * 5]) / span).coerceIn(0f, 1f) else 0f
    val fresh = FloatArray(5) { c -> if (c == 0) t else stops[a * 5 + c] + (stops[b * 5 + c] - stops[a * 5 + c]) * k }
    return (stops.copyOfRange(0, at * 5) + fresh + stops.copyOfRange(at * 5, stops.size)) to at
}

/**
 * Parâmetro degradê (plugins, formato 3): a barra mostra o resultado, tocar
 * acrescenta ou escolhe uma parada e arrastar a move. Ordem, limites e desfazer
 * são do motor (Engine::set_effect_gradient). Par do iOS: `GradientParamEditor`.
 */
@Composable
internal fun GradientParamEditor(env: PanelEnv, effect: Int, param: Int, label: String) {
    val store = env.store
    val layer = store.primary ?: return
    var stops by remember(layer, effect, param) { mutableStateOf(store.effectGradient(effect, param)) }
    var selected by remember(layer, effect, param) { mutableIntStateOf(0) }
    var dragging by remember { mutableStateOf(false) }
    var picking by remember { mutableStateOf(false) }
    val latest by rememberUpdatedState(stops)
    val locked = store.detail?.locked == true
    LaunchedEffect(layer, effect, param, store.sceneSettingsRevision) {
        if (!dragging && !picking) {
            stops = store.effectGradient(effect, param)
            selected = selected.coerceIn(0, (stops.size / 5 - 1).coerceAtLeast(0))
        }
    }
    fun commit(next: FloatArray) {
        store.setEffectGradient(effect, param, next)
        stops = store.effectGradient(effect, param)
        selected = selected.coerceIn(0, (stops.size / 5 - 1).coerceAtLeast(0))
    }
    val count = stops.size / 5
    if (count < 2) return
    val accent = AureaColors.Accent
    Column(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
        Canvas(
            Modifier.fillMaxWidth().height(44.dp).testTag("fx.gradient.bar").semantics { contentDescription = label }
                .pointerInput(layer, effect, param, locked) {
                    if (locked) return@pointerInput
                    awaitEachGesture {
                        val down = awaitFirstDown()
                        val pad = 14.dp.toPx()
                        val width = (size.width - pad * 2).coerceAtLeast(1f)
                        val toT = { x: Float -> ((x - pad) / width).coerceIn(0f, 1f) }
                        var working = latest
                        val n = working.size / 5
                        val nearest = (0 until n).minByOrNull { abs(pad + working[it * 5] * width - down.position.x) } ?: 0
                        var index = nearest
                        var changed = false
                        if (abs(pad + working[nearest * 5] * width - down.position.x) > 24.dp.toPx()) {
                            if (n >= MAX_STOPS) return@awaitEachGesture
                            val (next, at) = insertStop(working, toT(down.position.x))
                            working = next
                            index = at
                            changed = true
                        }
                        selected = index
                        dragging = true
                        stops = working
                        while (true) {
                            val event = awaitPointerEvent()
                            val change = event.changes.firstOrNull { it.id == down.id } ?: break
                            if (!change.pressed) break
                            if (change.positionChange() != Offset.Zero) {
                                change.consume()
                                working = moveStop(working, index, toT(change.position.x))
                                stops = working
                                changed = true
                            }
                        }
                        dragging = false
                        if (changed) commit(working)
                    }
                },
        ) {
            val pad = 14.dp.toPx()
            val width = size.width - pad * 2
            val barTop = size.height / 2f - 10.dp.toPx()
            val colors = Array(count) { stops[it * 5].coerceIn(0f, 1f) to rgbaColor(engineToDisplay(stopColor(stops, it))) }
            drawRoundRect(Brush.horizontalGradient(colorStops = colors, startX = pad, endX = pad + width), Offset(pad, barTop), Size(width, 20.dp.toPx()), CornerRadius(6.dp.toPx()))
            for (i in 0 until count) {
                val center = Offset(pad + stops[i * 5] * width, size.height / 2f)
                drawCircle(rgbaColor(engineToDisplay(stopColor(stops, i))).copy(alpha = 1f), 10.dp.toPx(), center)
                drawCircle(if (i == selected) accent else Color.White, 10.dp.toPx(), center, style = Stroke(2.dp.toPx()))
            }
        }
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), verticalAlignment = Alignment.CenterVertically) {
            for (i in 0 until count) {
                val stopLabel = stringResource(R.string.plugv3_gradient_stop, (i + 1).toString())
                Box(Modifier.size(44.dp).testTag("fx.gradient.stop.$i").semantics { contentDescription = stopLabel }, contentAlignment = Alignment.Center) {
                    ColorWell(
                        rgbaColor(engineToDisplay(stopColor(stops, i))),
                        if (i == selected) Modifier.border(2.dp, accent, RoundedCornerShape(6.dp)) else Modifier,
                    ) { if (!locked) { if (i == selected) picking = true else selected = i } }
                }
            }
        }
        val position = stops[selected * 5]
        val positionLabel = stringResource(R.string.plugv3_gradient_position)
        val percent = "${(position * 100f).roundToInt()} %"
        Row(Modifier.fillMaxWidth().heightIn(min = 44.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(positionLabel, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            Box(Modifier.weight(1f)) {
                AureaSlider(
                    value = position, min = 0f, max = 1f, step = 0f, origin = 0f, snap = null,
                    onStart = { dragging = true },
                    onValue = { stops = moveStop(stops, selected, it) },
                    onEnd = { dragging = false; commit(stops) },
                    enabled = !locked, a11yLabel = positionLabel, a11yValue = percent,
                )
            }
            Text(percent, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            if (count > 2) {
                Box(
                    Modifier.heightIn(min = 44.dp).testTag("fx.gradient.remove").tocavel(enabled = !locked) {
                        val at = selected
                        selected = (at - 1).coerceAtLeast(0)
                        commit(stops.copyOfRange(0, at * 5) + stops.copyOfRange((at + 1) * 5, stops.size))
                    },
                    contentAlignment = Alignment.Center,
                ) {
                    Text(stringResource(R.string.plugv3_gradient_remove), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Danger)))
                }
            }
        }
    }
    if (picking) {
        val index = selected.coerceIn(0, count - 1)
        val initial = remember { engineToDisplay(stopColor(stops, index)) }
        ColorPickerSheet(
            initial = initial,
            onChange = { r, g, b, a ->
                val v = displayToEngine(r, g, b, a)
                stops = stops.copyOf().also { for (k in 0..3) it[index * 5 + 1 + k] = v[k] }
            },
            onDone = {
                picking = false
                commit(stops)
            },
        )
    }
}
