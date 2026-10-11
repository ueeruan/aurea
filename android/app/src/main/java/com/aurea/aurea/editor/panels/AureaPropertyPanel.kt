package com.aurea.aurea.editor.panels

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.*
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.*
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.engine.BuiltinPropertySpec
import com.aurea.aurea.engine.ParamType
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.*
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import java.util.Locale
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.sin

/** Metadata chooses controls; all reads, animation and history use the existing engine. */
@Composable
internal fun AureaPropertyPanel(
    store: EditorStore, domain: String, values: FloatArray, materialIndex: Int = -1,
    modifier: Modifier = Modifier, onOpenCurve: (() -> Unit)? = null,
) {
    val layer = store.primary
    val generation = store.projectGeneration
    val composition = store.composition?.id
    val identity = listOf(generation, composition, layer, domain, materialIndex)
    val properties = store.builtinPropertySchema?.panels?.get(domain)?.takeIf { rows ->
        rows.flatMap { it.bindingParams }.all { it in values.indices }
    }
    val enabled = layer != null && store.detail?.locked != true
    var selected by remember(identity) { mutableStateOf<String?>(null) }
    var keypad by remember(identity) { mutableStateOf<KeypadRequest?>(null) }
    var color by remember(identity) { mutableStateOf<ColorRequest?>(null) }
    var colorAlpha by remember(identity) { mutableStateOf(true) }
    var gesturing by remember(identity) { mutableStateOf(false) }
    val currentValues by rememberUpdatedState(values)
    fun currentOwner() = store.primary == layer && store.projectGeneration == generation && store.composition?.id == composition
    fun begin(label: String) {
        if (!gesturing && enabled && currentOwner()) { gesturing = true; store.beginGesture(label) }
    }
    fun end() { if (gesturing) { gesturing = false; store.endGesture() } }
    fun write(spec: BuiltinPropertySpec, c: Int, value: Float) {
        if (!enabled || !currentOwner() || !value.isFinite()) return
        val native = value.coerceIn(spec.typedMin, spec.typedMax)
        if (domain == "light") store.setLightParam(spec.bindingParams[c], native)
        else store.setMaterialParameter(materialIndex, spec.bindingParams[c], native)
    }
    DisposableEffect(identity) { onDispose { color?.finish(); end() } }

    val selectedSpec = properties?.firstOrNull { it.id == selected && it.visible(values) }
    val tracks = selectedSpec?.let { spec -> (0 until spec.components).mapNotNull { spec.track(domain, materialIndex, it) } }.orEmpty()
    DisposableEffect(store, identity, tracks) {
        store.timelineFocus = tracks
        onDispose { store.timelineFocus = null }
    }
    Column(modifier) {
        if (properties == null) {
            Text(stringResource(R.string.property_schema_unavailable), style = AureaType.BodySmall)
        } else properties.filter { it.visible(values) }.forEach { spec ->
            key(identity, spec.id) {
                val label = propertyLabel(spec)
                val look = propertyLook(store, spec, domain, materialIndex)
                fun choose() { selected = spec.id }
                @Composable fun keyButton(component: Int? = null) {
                    val cs = component?.let { listOf(it) } ?: (0 until spec.components).toList()
                    if (cs.any { spec.track(domain, materialIndex, it) != null }) {
                        val keyLook = propertyLook(store, spec, domain, materialIndex, component)
                        val description = stringResource(if (keyLook == KeyframeLook.KeyHere) R.string.panel_tirar_keyframe_daqui else R.string.panel_marcar_keyframe_aqui) + " · " + label
                        TextButton(enabled = enabled, onClick = {
                            choose(); if (currentOwner()) store.toggleBuiltinPropertyKeyframe(domain, materialIndex, spec, currentValues, component)
                        }, modifier = Modifier.size(48.dp).testTag("property.$domain.${spec.id}.key${component ?: "all"}")
                            .semantics { contentDescription = description }) {
                            KeyframeDiamondIcon(keyLook, enabled, Modifier.size(22.dp))
                        }
                    }
                }
                when (spec.type) {
                    ParamType.ENUM -> PropertyCustomRow(label, selected == spec.id, ::choose, minHeight = 48.dp) {
                        ChoiceChips(spec.options.map { optionLabel(it.id, it.label) },
                            spec.options.indexOfFirst { it.value.toFloat() == spec.value(values) },
                            onSelect = { if (enabled) { choose(); write(spec, 0, spec.options[it].value.toFloat()) } },
                            modifier = Modifier.testTag("property.$domain.${spec.id}"))
                    }
                    ParamType.BOOL -> PropertyCustomRow(label, selected == spec.id, ::choose, minHeight = 48.dp) {
                        AureaToggle(spec.value(values) >= .5f, { choose(); write(spec, 0, if (it) 1f else 0f) },
                            enabled = enabled, modifier = Modifier.testTag("property.$domain.${spec.id}").semantics { contentDescription = label })
                    }
                    ParamType.COLOR -> {
                        val nativeColor = FloatArray(4) { c -> if (c < spec.components) spec.value(values, c) else 1f }
                        val display = if (spec.colorSpace == "linear") engineToDisplay(nativeColor) else nativeColor
                        PropertyCustomRow(label, selected == spec.id, ::choose, keyframe = look, minHeight = 48.dp) {
                            Row(Modifier.heightIn(min = 48.dp), verticalAlignment = Alignment.CenterVertically) {
                                ColorWell(rgbaColor(display), Modifier.size(48.dp).testTag("property.$domain.${spec.id}.picker")
                                    .semantics { contentDescription = label }) {
                                    if (enabled) {
                                        choose(); begin(label); colorAlpha = spec.components == 4
                                        color = ColorRequest(display, { r, g, b, a ->
                                            val next = if (spec.colorSpace == "linear") displayToEngine(r, g, b, a) else floatArrayOf(r, g, b, a)
                                            for (c in 0 until spec.components) write(spec, c, next[c])
                                        }, ::end)
                                    }
                                }
                                Spacer(Modifier.weight(1f)); keyButton()
                            }
                        }
                        for (c in 0 until spec.components) {
                            val channelLabel = if (c < 3) "RGB"[c].toString() else stringResource(R.string.fxo_alpha_2)
                            val factor = if (c < 3) 255f else 100f
                            val channelValue = display[c] * factor
                            fun convert(v: Float): Float {
                                if (c >= 3 || spec.colorSpace != "linear") return v / factor
                                val next = display.copyOf().also { it[c] = v / factor }
                                return displayToEngine(next[0], next[1], next[2], next[3])[c]
                            }
                            Row(Modifier.heightIn(min = 48.dp), verticalAlignment = Alignment.CenterVertically) {
                                PropertyRow(channelLabel, channelValue, factor / 180f, 0f, factor,
                                    { formatProperty(it, spec.precision, if (c == 3) "%" else "") }, selected == spec.id, ::choose,
                                    { begin(label) }, { write(spec, c, convert(it)) }, ::end,
                                    onTapValue = { keypad = KeypadRequest("$label · $channelLabel", channelValue,
                                        if (c == 3) "%" else "", 0f, factor, spec.precision) { write(spec, c, convert(it)) } },
                                    enabled = enabled, keyframe = propertyLook(store, spec, domain, materialIndex, c),
                                    modifier = Modifier.weight(1f).testTag("property.$domain.${spec.id}.$c")
                                        .semantics {
                                            contentDescription = "$label · $channelLabel"
                                            progressBarRangeInfo = ProgressBarRangeInfo(channelValue, 0f..factor)
                                            if (!enabled) disabled() else setProgress { write(spec, c, convert(it.coerceIn(0f, factor))); true }
                                        })
                                keyButton(c)
                            }
                        }
                    }
                    else -> {
                        val scale = if (spec.unit == "%") 100f else 1f
                        val value = spec.value(values) * scale
                        val lo = spec.typedMin * scale; val hi = spec.typedMax * scale
                        if (spec.type == ParamType.ANGLE) NativePropertyDial(value, label, lo, hi, enabled,
                            { begin(label) }, { write(spec, 0, it / scale) }, ::end,
                            Modifier.fillMaxWidth().height(88.dp).testTag("property.$domain.${spec.id}.dial"))
                        Row(Modifier.heightIn(min = 48.dp), verticalAlignment = Alignment.CenterVertically) {
                            PropertyRow(label, value, (spec.sliderMax - spec.sliderMin) * scale / 180f,
                                lo, hi, { formatProperty(it, spec.precision, spec.unit) }, selected == spec.id, ::choose,
                                { begin(label) }, { write(spec, 0, it / scale) }, ::end,
                                onTapValue = { keypad = KeypadRequest(label, value, spec.unit, lo, hi, spec.precision) { write(spec, 0, it / scale) } },
                                enabled = enabled, keyframe = look,
                                modifier = Modifier.weight(1f).testTag("property.$domain.${spec.id}").semantics {
                                    contentDescription = label
                                    progressBarRangeInfo = ProgressBarRangeInfo(value.coerceIn(lo, hi), lo..hi)
                                    if (!enabled) disabled() else setProgress { write(spec, 0, it.coerceIn(lo, hi) / scale); true }
                                })
                            keyButton()
                        }
                    }
                }
                Spacer(Modifier.height(4.dp))
            }
        }
        val curveKeys = store.primaryKeys().filter { key -> tracks.any { it.matches(key.property, key.effectIndex, key.paramIndex) } }
        val curveTracks = tracks.map { binding -> curveKeys.filter { binding.matches(it.property, it.effectIndex, it.paramIndex) } }
        if (onOpenCurve != null && curveTracks.any { it.size >= 2 }) TextButton(onClick = {
            val local = store.detail?.localFrame(store.playhead) ?: return@TextButton
            val track = curveTrack(curveTracks)
            track.segmentStart(local)?.let { key ->
                if (layer != null && currentOwner()) { store.selectKeyframe(layer, key); onOpenCurve() }
            }
        }, enabled = enabled, modifier = Modifier.heightIn(min = 48.dp).testTag("property.$domain.curve")) {
            Text(stringResource(R.string.panel_editar_curva_propriedade))
        }
    }
    keypad?.let { request -> NumericKeypadSheet(request) { keypad = null } }
    color?.let { request ->
        DisposableEffect(request) { onDispose { request.finish() } }
        ColorPickerSheet(request.initial, withAlpha = colorAlpha, previewIdentity = identity,
            pickFromPreview = { store.captureBitmap(720) }, onChange = request.onChange,
            onDone = { request.finish(); color = null })
    }
}

private fun TrackKey.matches(property: Int, effect: Int, param: Int) = this.property == property && effectIndex == effect && paramIndex == param
private fun formatProperty(value: Float, precision: Int, unit: String): String =
    String.format(Locale.getDefault(), "%.${precision}f", value) + if (unit.isEmpty()) "" else " $unit"

private fun propertyLook(store: EditorStore, spec: BuiltinPropertySpec, domain: String, material: Int, component: Int? = null): KeyframeLook {
    val local = store.detail?.localFrame(store.playhead) ?: return KeyframeLook.None
    val tracks = (component?.let { listOf(it) } ?: (0 until spec.components).toList()).mapNotNull { spec.track(domain, material, it) }
    val keys = store.primaryKeys().filter { key -> tracks.any { it.matches(key.property, key.effectIndex, key.paramIndex) } }
    return if (keys.any { it.time == local }) KeyframeLook.KeyHere else if (keys.isNotEmpty()) KeyframeLook.Animated else KeyframeLook.None
}

@Composable
private fun propertyLabel(spec: BuiltinPropertySpec): String = when (spec.id) {
    "kind" -> stringResource(R.string.scene_light_kind)
    "intensity" -> stringResource(R.string.panel_intensidade)
    "color", "baseColor" -> stringResource(R.string.panel_cor)
    "range" -> stringResource(R.string.scene_light_range)
    "coneAngle" -> stringResource(R.string.scene_light_cone)
    "penumbra" -> stringResource(R.string.scene_light_penumbra)
    "castShadows" -> stringResource(R.string.scene_light_shadows)
    "shadowBias" -> stringResource(R.string.scene_shadow_bias)
    "shadowStrength" -> stringResource(R.string.scene_shadow_strength)
    "metallic" -> stringResource(R.string.pn_t3d_metallic)
    "roughness" -> stringResource(R.string.pn_t3d_roughness)
    else -> spec.label
}

@Composable
private fun optionLabel(id: String, fallback: String): String = when (id) {
    "directional" -> stringResource(R.string.scene_light_directional)
    "point" -> stringResource(R.string.scene_light_point)
    "spot" -> stringResource(R.string.scene_light_spot)
    else -> fallback
}

/** Angular geometry follows the existing transform dial; bounds remain native metadata. */
@Composable
private fun NativePropertyDial(value: Float, label: String, min: Float, max: Float, enabled: Boolean,
    begin: () -> Unit, write: (Float) -> Unit, end: () -> Unit, modifier: Modifier) {
    val send by rememberUpdatedState(write)
    val start by rememberUpdatedState(begin)
    val finish by rememberUpdatedState(end)
    Box(modifier.semantics {
        contentDescription = label
        progressBarRangeInfo = ProgressBarRangeInfo(value.coerceIn(min, max), min..max)
        if (!enabled) disabled() else setProgress { send(it.coerceIn(min, max)); true }
    }, contentAlignment = Alignment.Center) {
        Canvas(Modifier.size(80.dp).pointerInput(enabled, min, max) {
            if (!enabled) return@pointerInput
            var active = false
            fun angle(p: Offset): Float {
                val raw = Math.toDegrees(atan2((p.y - size.height / 2f).toDouble(), (p.x - size.width / 2f).toDouble())).toFloat()
                return (if (raw < 0) raw + 360f else raw).coerceIn(min, max)
            }
            try {
                detectDragGestures(onDragStart = { active = true; start(); send(angle(it)) },
                    onDragEnd = { if (active) { active = false; finish() } },
                    onDragCancel = { if (active) { active = false; finish() } }) { change, _ ->
                    change.consume(); send(angle(change.position))
                }
            } finally { if (active) finish() }
        }) {
            val radius = size.minDimension / 2 - 8.dp.toPx()
            drawCircle(AureaColors.DialTrack, radius, center, style = Stroke(2.dp.toPx()))
            val radians = Math.toRadians(value.toDouble())
            val tip = center + Offset((cos(radians) * radius).toFloat(), (sin(radians) * radius).toFloat())
            drawLine(AureaColors.Accent, center, tip, 2.dp.toPx())
            drawCircle(AureaColors.Accent, 5.dp.toPx(), tip)
        }
    }
}
