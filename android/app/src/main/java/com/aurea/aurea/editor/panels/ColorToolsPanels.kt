package com.aurea.aurea.editor.panels

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.StringRes
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.engine.EffectParam
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import kotlin.math.hypot
import kotlin.math.roundToInt

// =============================================================================
//  FILTROS e AJUSTAR do modo de edição (0.3.0). Cada um tem a sua tela — antes
//  Efeitos, Filtros e Ajustar abriam o mesmo editor de efeitos.
//
//  Os dois mexem na "Correção de cor" do motor (aurea.color.grade), cada um
//  numa instância própria (o papel 1 = Filtro, 2 = Ajustar; EngineColor.cpp):
//  trocar de filtro não apaga o ajuste fino, e o ajuste age sobre o look.
//  No clipe escolhido vale para ele; no nível de cima, para a camada de ajuste.
// =============================================================================

/** Índices dos parâmetros da Correção de cor (ColorGradeEffect.cpp). */
internal object Grade {
    const val KEY = "aurea.color.grade"
    const val LOG = 0; const val TEMPERATURE = 1; const val TINT = 2; const val EXPOSURE = 3; const val CONTRAST = 4
    const val PIVOT = 5; const val HIGHLIGHTS = 6; const val SHADOWS = 7; const val WHITES = 8; const val BLACKS = 9
    const val BRIGHTNESS = 10; const val SATURATION = 11; const val VIBRANCE = 12
    const val LIFT = 13; const val LIFT_LUMA = 14; const val GAMMA = 15; const val GAMMA_LUMA = 16
    const val GAIN = 17; const val GAIN_LUMA = 18; const val OFFSET = 19; const val OFFSET_LUMA = 20
    const val CURVES = 21; const val HUE_CURVES = 22; const val HSL = 23
    const val QUALIFIER = 47; const val Q_HUE = 48; const val Q_WIDTH = 49; const val Q_SOFT = 54
    const val Q_INVERT = 55; const val Q_SHOW = 56; const val Q_HUE_SHIFT = 57; const val Q_SAT = 58; const val Q_EXPOSURE = 59
    const val FADE = 60; const val SHADOW_TINT = 61; const val HIGHLIGHT_TINT = 62; const val TINT_BALANCE = 63
    const val SHARPEN = 64; const val VIGNETTE = 65; const val GRAIN = 68
    const val LUT = 70; const val LUT_INTENSITY = 71; const val MIX = 76; const val ROLE = 77; const val LOOK = 78
    const val ROLE_FILTER = 1; const val ROLE_ADJUST = 2
}

/** Os looks na ordem dos presets do motor (ColorGrade::presets). */
private val LookNames = listOf(
    R.string.look_cinema, R.string.look_warm, R.string.look_cool, R.string.look_vintage, R.string.look_mono,
    R.string.look_noir, R.string.look_vivid, R.string.look_faded, R.string.look_sunset, R.string.look_teal,
    R.string.look_film, R.string.look_anime, R.string.look_matte,
)

private fun List<EffectParam>.at(index: Int): EffectParam? = firstOrNull { it.index == index }

/** A instância do papel [role] na camada escolhida (criada na primeira vez). */
@Composable
private fun rememberGradeEffect(env: PanelEnv, role: Int): Int? {
    val store = env.store
    val layer = store.primary
    var effectId by remember(layer, role) { mutableStateOf<Int?>(null) }
    LaunchedEffect(layer, role) { effectId = store.colorGradeFor(role) }
    // Desfazer pode tirar a instância: pede de novo quando ela some da pilha.
    val gone = effectId != null && store.effects.none { it.effectId == effectId }
    LaunchedEffect(gone) { if (gone) effectId = store.colorGradeFor(role) }
    return effectId
}

/** Um controle deslizante de um parâmetro, num gesto de desfazer por arrasto. */
@Composable
private fun GradeSlider(env: PanelEnv, effectId: Int, params: List<EffectParam>, index: Int, label: String,
                        min: Float, max: Float, component: Int = 0, tag: String = "grade.slider.$index") {
    val store = env.store
    val param = params.at(index) ?: return
    val current = param.value.getOrElse(component) { 0f }
    var dragging by remember { mutableStateOf(false) }
    var local by remember(effectId, index, component) { mutableFloatStateOf(current) }
    if (!dragging) local = current
    val latest by rememberUpdatedState(param)
    Column(Modifier.fillMaxWidth().padding(horizontal = 4.dp)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(label, color = AureaColors.Text, fontSize = 13.sp, modifier = Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text(if (max - min > 20f) "${local.roundToInt()}" else "%.2f".format(local), color = AureaColors.Muted, fontSize = 12.sp)
        }
        Slider(
            value = local.coerceIn(min, max),
            onValueChange = {
                if (!dragging) { dragging = true; store.beginGesture(label) }
                local = it
                store.setEffectParam(effectId, latest, it, component)
            },
            onValueChangeFinished = { if (dragging) { dragging = false; store.endGesture() } },
            valueRange = min..max,
            modifier = Modifier.fillMaxWidth().heightIn(min = 44.dp).testTag(tag).semantics { contentDescription = label },
        )
    }
}

// -----------------------------------------------------------------------------
// FILTROS
// -----------------------------------------------------------------------------
@Composable
internal fun FiltersPanel(env: PanelEnv) {
    val store = env.store
    val effectId = rememberGradeEffect(env, Grade.ROLE_FILTER) ?: return
    val params = store.effectParams[effectId].orEmpty()
    val look = params.at(Grade.LOOK)?.value?.getOrNull(0)?.roundToInt() ?: -1
    // Miniaturas do quadro do cabeçote; refeitas quando ele anda meio segundo.
    val bucket = (store.playhead / 15)
    val thumbs by produceState<List<ImageBitmap>?>(null, store.primary, bucket, store.projectGeneration) {
        var got: List<ImageBitmap>? = null
        for (attempt in 0 until 6) {
            got = withContext(Dispatchers.Default) { store.colorLookThumbnails(144)?.map { it.asImageBitmap() } }
            if (got != null) break
            delay(160L * (attempt + 1))
        }
        value = got
    }
    val names = listOf(R.string.grade_none) + LookNames
    val adjustment = store.layers.firstOrNull { it.id == store.primary }?.adjustment == true
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 10.dp, vertical = 8.dp)) {
        LazyRow(Modifier.fillMaxWidth().testTag("grade.filters"), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            itemsIndexed(names) { i, name ->
                val value = i - 1
                val selected = value == look
                val label = stringResource(name)
                Column(Modifier.width(74.dp).testTag("grade.look.$value").semantics { contentDescription = label }
                    .tocavel(haptic = true) { store.applyColorLook(effectId, value) },
                    horizontalAlignment = Alignment.CenterHorizontally) {
                    Box(Modifier.size(70.dp).clip(RoundedCornerShape(10.dp))
                        .border(if (selected) 2.dp else 0.dp, if (selected) AureaColors.Accent else Color.Transparent, RoundedCornerShape(10.dp))
                        .background(AureaColors.Chip)) {
                        thumbs?.getOrNull(i)?.let { Image(it, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop) }
                    }
                    Spacer(Modifier.height(4.dp))
                    Text(label, fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis,
                        color = if (selected) AureaColors.Accent else AureaColors.Text)
                }
            }
        }
        Spacer(Modifier.height(10.dp))
        if (look >= 0) GradeSlider(env, effectId, params, Grade.MIX, stringResource(R.string.grade_intensity), 0f, 100f, tag = "grade.filter.intensity")
        if (adjustment) Text(stringResource(R.string.grade_global_hint), color = AureaColors.Muted, fontSize = 12.sp, modifier = Modifier.padding(4.dp))
    }
}

// -----------------------------------------------------------------------------
// AJUSTAR
// -----------------------------------------------------------------------------
private class Tool(val index: Int, @StringRes val label: Int, val min: Float, val max: Float, val neutral: Float = 0f)

private val BasicTools = listOf(
    Tool(Grade.BRIGHTNESS, R.string.grade_brightness, -100f, 100f),
    Tool(Grade.CONTRAST, R.string.grade_contrast, -100f, 100f),
    Tool(Grade.SATURATION, R.string.grade_saturation, 0f, 200f, 100f),
    Tool(Grade.EXPOSURE, R.string.grade_exposure, -5f, 5f),
    Tool(Grade.TEMPERATURE, R.string.grade_temperature, -100f, 100f),
    Tool(Grade.TINT, R.string.grade_tint, -100f, 100f),
    Tool(Grade.HIGHLIGHTS, R.string.grade_highlights, -100f, 100f),
    Tool(Grade.SHADOWS, R.string.grade_shadows, -100f, 100f),
    Tool(Grade.WHITES, R.string.grade_whites, -100f, 100f),
    Tool(Grade.BLACKS, R.string.grade_blacks, -100f, 100f),
    Tool(Grade.VIBRANCE, R.string.grade_vibrance, -100f, 100f),
    Tool(Grade.SHARPEN, R.string.grade_sharpen, 0f, 100f),
    Tool(Grade.VIGNETTE, R.string.grade_vignette, -100f, 100f),
    Tool(Grade.GRAIN, R.string.grade_grain, 0f, 100f),
    Tool(Grade.FADE, R.string.grade_fade, 0f, 100f),
)

private val Bands = listOf(
    R.string.grade_band_red to Color(0xFFE53935), R.string.grade_band_orange to Color(0xFFFB8C00),
    R.string.grade_band_yellow to Color(0xFFFDD835), R.string.grade_band_green to Color(0xFF43A047),
    R.string.grade_band_aqua to Color(0xFF00ACC1), R.string.grade_band_blue to Color(0xFF1E88E5),
    R.string.grade_band_purple to Color(0xFF8E24AA), R.string.grade_band_magenta to Color(0xFFD81B60),
)

@Composable
internal fun AdjustPanel(env: PanelEnv) {
    val store = env.store
    val effectId = rememberGradeEffect(env, Grade.ROLE_ADJUST) ?: return
    val params = store.effectParams[effectId].orEmpty()
    var tab by rememberSaveable { mutableIntStateOf(0) }
    var scopes by rememberSaveable { mutableStateOf(false) }
    Column(Modifier.fillMaxSize()) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            FilterChip(scopes, { scopes = !scopes }, label = { Text(stringResource(R.string.grade_scopes)) },
                modifier = Modifier.heightIn(min = 40.dp).testTag("grade.scopes"))
            Spacer(Modifier.weight(1f))
            TextButton({ store.resetColorGrade(effectId) }, Modifier.heightIn(min = 44.dp).testTag("grade.reset")) {
                Text(stringResource(R.string.grade_reset))
            }
        }
        if (scopes) ScopesView(env)
        ParamTabs(
            labels = listOf(stringResource(R.string.grade_tab_basic), stringResource(R.string.grade_tab_hsl),
                stringResource(R.string.grade_tab_curves), stringResource(R.string.grade_tab_wheels), stringResource(R.string.grade_tab_more)),
            selected = tab,
            onSelect = { tab = it },
        )
        Box(Modifier.weight(1f).fillMaxWidth()) {
            when (tab) {
                0 -> BasicTab(env, effectId, params)
                1 -> HslTab(env, effectId, params)
                2 -> CurvesTab(env, effectId)
                3 -> WheelsTab(env, effectId, params)
                else -> MoreTab(env, effectId, params)
            }
        }
    }
}

@Composable
private fun BasicTab(env: PanelEnv, effectId: Int, params: List<EffectParam>) {
    var tool by rememberSaveable { mutableIntStateOf(0) }
    Column(Modifier.fillMaxSize().padding(horizontal = 8.dp, vertical = 4.dp)) {
        LazyRow(Modifier.fillMaxWidth().testTag("grade.tools"), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            itemsIndexed(BasicTools) { i, t ->
                val v = params.at(t.index)?.value?.getOrNull(0) ?: t.neutral
                val changed = kotlin.math.abs(v - t.neutral) > 1e-3f
                val label = stringResource(t.label)
                Column(Modifier.width(68.dp).testTag("grade.tool.${t.index}").semantics { contentDescription = label }
                    .tocavel { tool = i }, horizontalAlignment = Alignment.CenterHorizontally) {
                    Box(Modifier.size(40.dp).clip(CircleShape)
                        .background(if (i == tool) AureaColors.AccentDim else AureaColors.Chip), contentAlignment = Alignment.Center) {
                        Text(if (changed) "${v.roundToInt()}" else "•", fontSize = 11.sp,
                            color = if (changed) AureaColors.Accent else AureaColors.Muted)
                    }
                    Spacer(Modifier.height(4.dp))
                    Text(label, fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis,
                        color = if (i == tool) AureaColors.Accent else AureaColors.Text)
                }
            }
        }
        Spacer(Modifier.height(8.dp))
        val t = BasicTools[tool.coerceIn(0, BasicTools.lastIndex)]
        GradeSlider(env, effectId, params, t.index, stringResource(t.label), t.min, t.max)
    }
}

@Composable
private fun HslTab(env: PanelEnv, effectId: Int, params: List<EffectParam>) {
    var band by rememberSaveable { mutableIntStateOf(0) }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 8.dp, vertical = 4.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Bands.forEachIndexed { i, (name, color) ->
                val label = stringResource(name)
                Box(Modifier.size(34.dp).clip(CircleShape).background(color)
                    .border(if (i == band) 3.dp else 0.dp, AureaColors.Text, CircleShape)
                    .testTag("grade.band.$i").semantics { contentDescription = label }.tocavel { band = i })
            }
        }
        Spacer(Modifier.height(6.dp))
        Text(stringResource(Bands[band].first), color = AureaColors.Text, fontSize = 14.sp)
        val base = Grade.HSL + band * 3
        GradeSlider(env, effectId, params, base, stringResource(R.string.grade_hue), -100f, 100f)
        GradeSlider(env, effectId, params, base + 1, stringResource(R.string.grade_saturation), -100f, 100f)
        GradeSlider(env, effectId, params, base + 2, stringResource(R.string.grade_luminance), -100f, 100f)
    }
}

@Composable
private fun CurvesTab(env: PanelEnv, effectId: Int) {
    var hue by rememberSaveable { mutableStateOf(false) }
    val hueLabels = listOf(stringResource(R.string.grade_hue_vs_hue), stringResource(R.string.grade_hue_vs_sat),
        stringResource(R.string.grade_hue_vs_luma), stringResource(R.string.grade_luma_vs_sat))
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 8.dp, vertical = 4.dp)) {
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            FilterChip(!hue, { hue = false }, label = { Text("RGB") }, modifier = Modifier.heightIn(min = 40.dp).testTag("grade.curves.rgb"))
            FilterChip(hue, { hue = true }, label = { Text(stringResource(R.string.grade_hue)) }, modifier = Modifier.heightIn(min = 40.dp).testTag("grade.curves.hue"))
        }
        if (hue) ColorCurveEditor(env, effectId, Grade.HUE_CURVES, channelNames = hueLabels, hueBackdrop = true)
        else ColorCurveEditor(env, effectId, Grade.CURVES)
    }
}

private class Wheel(val point: Int, val luma: Int?, @StringRes val label: Int)

private val Wheels = listOf(
    Wheel(Grade.LIFT, Grade.LIFT_LUMA, R.string.grade_lift), Wheel(Grade.GAMMA, Grade.GAMMA_LUMA, R.string.grade_gamma),
    Wheel(Grade.GAIN, Grade.GAIN_LUMA, R.string.grade_gain), Wheel(Grade.OFFSET, Grade.OFFSET_LUMA, R.string.grade_offset),
    Wheel(Grade.SHADOW_TINT, null, R.string.grade_shadow_tint), Wheel(Grade.HIGHLIGHT_TINT, null, R.string.grade_highlight_tint),
)

@Composable
private fun WheelsTab(env: PanelEnv, effectId: Int, params: List<EffectParam>) {
    val store = env.store
    var which by rememberSaveable { mutableIntStateOf(0) }
    val w = Wheels[which.coerceIn(0, Wheels.lastIndex)]
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 8.dp, vertical = 4.dp),
        horizontalAlignment = Alignment.CenterHorizontally) {
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Wheels.forEachIndexed { i, item ->
                FilterChip(i == which, { which = i }, label = { Text(stringResource(item.label), fontSize = 12.sp) },
                    modifier = Modifier.heightIn(min = 40.dp).testTag("grade.wheel.tab.$i"))
            }
        }
        val param = params.at(w.point)
        if (param != null) {
            val label = stringResource(w.label)
            ColorWheel(
                x = param.value.getOrElse(0) { 0f }, y = param.value.getOrElse(1) { 0f }, label = label,
                onBegin = { store.beginGesture(label) },
                onChange = { x, y -> store.setEffectPoint(effectId, param, x, y) },
                onEnd = { store.endGesture() },
                modifier = Modifier.padding(vertical = 8.dp).testTag("grade.wheel.${w.point}"),
            )
            Text(stringResource(R.string.grade_wheel_hint), color = AureaColors.Muted, fontSize = 11.sp)
        }
        w.luma?.let { GradeSlider(env, effectId, params, it, stringResource(R.string.grade_luminance), -100f, 100f) }
        if (w.point == Grade.SHADOW_TINT || w.point == Grade.HIGHLIGHT_TINT)
            GradeSlider(env, effectId, params, Grade.TINT_BALANCE, stringResource(R.string.grade_tint_balance), -100f, 100f)
    }
}

/**
 * Roda de cor: o ponto no disco é (cos, sen) da matiz × força — a mesma conta
 * do `wheel()` do color_grade.frag (matiz 0 = vermelho, sentido anti-horário
 * com y para cima). Toque duplo zera.
 */
@Composable
private fun ColorWheel(x: Float, y: Float, label: String, onBegin: () -> Unit, onChange: (Float, Float) -> Unit,
                       onEnd: () -> Unit, modifier: Modifier = Modifier) {
    val change by rememberUpdatedState(onChange)
    val begin by rememberUpdatedState(onBegin)
    val end by rememberUpdatedState(onEnd)
    // Cores no sentido horário a partir das 3 h (Compose): matiz = 1 − t.
    val ring = remember { List(13) { i -> Color.hsv(((1f - i / 12f) * 360f).mod(360f), 1f, 1f) } }
    Canvas(modifier.size(200.dp).semantics { contentDescription = label }
        .pointerInput(Unit) {
            detectTapGestures(onDoubleTap = { begin(); change(0f, 0f); end() })
        }
        .pointerInput(Unit) {
            fun at(o: Offset): Pair<Float, Float> {
                val r = size.width / 2f
                var px = (o.x - r) / r
                var py = -(o.y - r) / r
                val d = hypot(px, py)
                if (d > 1f) { px /= d; py /= d }
                return px to py
            }
            detectDragGestures(
                onDragStart = { o -> begin(); val (a, b) = at(o); change(a, b) },
                onDragEnd = { end() },
                onDragCancel = { end() },
                onDrag = { c, _ -> val (a, b) = at(c.position); change(a, b) },
            )
        }) {
        val r = size.minDimension / 2f
        val center = Offset(size.width / 2f, size.height / 2f)
        drawCircle(Brush.sweepGradient(ring, center), r, center)
        drawCircle(Brush.radialGradient(listOf(Color(0xFF8C8C8C), Color(0x008C8C8C)), center, r), r, center)
        drawCircle(Color.White.copy(alpha = .35f), r, center, style = Stroke(1.dp.toPx()))
        drawLine(Color.White.copy(alpha = .25f), Offset(center.x - r, center.y), Offset(center.x + r, center.y))
        drawLine(Color.White.copy(alpha = .25f), Offset(center.x, center.y - r), Offset(center.x, center.y + r))
        val puck = Offset(center.x + x.coerceIn(-1f, 1f) * r, center.y - y.coerceIn(-1f, 1f) * r)
        drawCircle(Color.Black.copy(alpha = .45f), 11.dp.toPx(), puck)
        drawCircle(Color.White, 9.dp.toPx(), puck, style = Stroke(2.5.dp.toPx()))
    }
}

@Composable
private fun MoreTab(env: PanelEnv, effectId: Int, params: List<EffectParam>) {
    val store = env.store
    val layer = store.primary
    val lut = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null && layer != null) store.importColorLut(uri, layer, effectId)
    }
    val profiles = listOf(stringResource(R.string.grade_log_none), "S-Log3 (Sony)", "LogC3 (ARRI)", "V-Log (Panasonic)", "Apple Log", "Cineon")
    val profile = params.at(Grade.LOG)?.value?.getOrNull(0)?.roundToInt() ?: 0
    val logParam = params.at(Grade.LOG)
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 8.dp, vertical = 4.dp)) {
        Text(stringResource(R.string.grade_log_profile), color = AureaColors.Text, fontSize = 13.sp)
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            profiles.forEachIndexed { i, name ->
                FilterChip(i == profile, { if (logParam != null) store.setEffectParam(effectId, logParam, i.toFloat()) },
                    label = { Text(name, fontSize = 12.sp) }, modifier = Modifier.heightIn(min = 40.dp).testTag("grade.log.$i"))
            }
        }
        Spacer(Modifier.height(8.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.grade_lut), color = AureaColors.Text, fontSize = 13.sp, modifier = Modifier.weight(1f))
            TextButton({ lut.launch(arrayOf("*/*")) }, Modifier.heightIn(min = 44.dp).testTag("grade.lut.import")) {
                Text(stringResource(R.string.grade_lut_import))
            }
        }
        GradeSlider(env, effectId, params, Grade.LUT_INTENSITY, stringResource(R.string.grade_lut_intensity), 0f, 100f)
        Spacer(Modifier.height(8.dp))
        val qualifier = params.at(Grade.QUALIFIER)
        val on = (qualifier?.value?.getOrNull(0) ?: 0f) >= .5f
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.grade_secondary), color = AureaColors.Text, fontSize = 13.sp, modifier = Modifier.weight(1f))
            Switch(on, { if (qualifier != null) store.setEffectParam(effectId, qualifier, if (it) 1f else 0f) },
                modifier = Modifier.testTag("grade.secondary"))
        }
        if (on) {
            GradeSlider(env, effectId, params, Grade.Q_HUE, stringResource(R.string.grade_q_hue), 0f, 360f)
            GradeSlider(env, effectId, params, Grade.Q_WIDTH, stringResource(R.string.grade_q_width), 1f, 180f)
            GradeSlider(env, effectId, params, Grade.Q_SOFT, stringResource(R.string.grade_q_softness), 0f, 100f)
            for ((index, res) in listOf(Grade.Q_SHOW to R.string.grade_q_show, Grade.Q_INVERT to R.string.grade_q_invert)) {
                val p = params.at(index) ?: continue
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(res), color = AureaColors.Text, fontSize = 13.sp, modifier = Modifier.weight(1f))
                    Switch((p.value.getOrNull(0) ?: 0f) >= .5f, { store.setEffectParam(effectId, p, if (it) 1f else 0f) },
                        modifier = Modifier.testTag("grade.switch.$index"))
                }
            }
            GradeSlider(env, effectId, params, Grade.Q_HUE_SHIFT, stringResource(R.string.grade_q_hue_shift), -180f, 180f)
            GradeSlider(env, effectId, params, Grade.Q_SAT, stringResource(R.string.grade_saturation), -100f, 100f)
            GradeSlider(env, effectId, params, Grade.Q_EXPOSURE, stringResource(R.string.grade_exposure), -3f, 3f)
        }
        GradeSlider(env, effectId, params, Grade.PIVOT, stringResource(R.string.grade_pivot), 0f, 100f)
    }
}

/** Histograma, forma de onda, parade e vetorscópio do quadro do cabeçote (EngineColor.cpp). */
@Composable
private fun ScopesView(env: PanelEnv) {
    val store = env.store
    var kind by rememberSaveable { mutableIntStateOf(0) }
    val data by produceState<com.aurea.aurea.state.EditorStore.Scopes?>(null, store.curveRevision, store.playhead / 5, store.primary) {
        delay(120)
        value = withContext(Dispatchers.Default) { store.colorScopes(192) }
    }
    val labels = listOf(R.string.grade_scope_histogram, R.string.grade_scope_waveform, R.string.grade_scope_parade, R.string.grade_scope_vectorscope)
    Column(Modifier.fillMaxWidth().padding(horizontal = 8.dp)) {
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            labels.forEachIndexed { i, res ->
                FilterChip(i == kind, { kind = i }, label = { Text(stringResource(res), fontSize = 11.sp) },
                    modifier = Modifier.heightIn(min = 36.dp).testTag("grade.scope.$i"))
            }
        }
        Canvas(Modifier.fillMaxWidth().height(118.dp).background(Color(0xFF101010)).testTag("grade.scope.view")) {
            val s = data ?: return@Canvas
            when (kind) {
                0 -> {
                    val colors = listOf(Color(0xFFFF5252), Color(0xFF69F0AE), Color(0xFF448AFF), Color.White)
                    val peak = (0 until 1024).maxOf { s.histogram[it] }.coerceAtLeast(1).toFloat()
                    for (c in 0..3) {
                        var prev: Offset? = null
                        for (i in 0 until 256) {
                            val p = Offset(i / 255f * size.width, size.height - s.histogram[c * 256 + i] / peak * size.height)
                            prev?.let { drawLine(colors[c].copy(alpha = if (c == 3) .55f else .8f), it, p, 1.2f) }
                            prev = p
                        }
                    }
                }
                1, 2 -> {
                    val parade = kind == 2
                    val cols = s.columns.coerceAtLeast(1)
                    val groups = if (parade) 3 else 1
                    val peak = if (parade) s.parade.maxOfOrNull { it.toInt() and 0xffff } ?: 1 else s.waveform.maxOfOrNull { it.toInt() and 0xffff } ?: 1
                    val cw = size.width / (cols * groups)
                    val ch = size.height / 128f
                    val tint = listOf(Color(0xFFFF5252), Color(0xFF69F0AE), Color(0xFF448AFF))
                    for (g in 0 until groups) for (x in 0 until cols) for (yb in 0 until 128) {
                        val v = (if (parade) s.parade[(g * cols + x) * 128 + yb] else s.waveform[x * 128 + yb]).toInt() and 0xffff
                        if (v == 0) continue
                        val a = (0.15f + 0.85f * kotlin.math.sqrt(v / peak.coerceAtLeast(1).toFloat())).coerceIn(0f, 1f)
                        drawRect(if (parade) tint[g].copy(alpha = a) else Color(0xFF9EFFA8).copy(alpha = a),
                            Offset((g * cols + x) * cw, size.height - (yb + 1) * ch), androidx.compose.ui.geometry.Size(cw + .5f, ch + .5f))
                    }
                }
                else -> {
                    val side = size.minDimension
                    val ox = (size.width - side) / 2f
                    val cell = side / 128f
                    val peak = s.vectorscope.maxOfOrNull { it.toInt() and 0xffff }?.coerceAtLeast(1) ?: 1
                    drawCircle(Color.White.copy(alpha = .2f), side / 2f, Offset(ox + side / 2f, side / 2f), style = Stroke(1f))
                    for (vy in 0 until 128) for (vx in 0 until 128) {
                        val v = s.vectorscope[vy * 128 + vx].toInt() and 0xffff
                        if (v == 0) continue
                        val a = (0.2f + 0.8f * kotlin.math.sqrt(v / peak.toFloat())).coerceIn(0f, 1f)
                        drawRect(Color(0xFF9EFFA8).copy(alpha = a), Offset(ox + vx * cell, vy * cell), androidx.compose.ui.geometry.Size(cell + .5f, cell + .5f))
                    }
                }
            }
        }
    }
}
