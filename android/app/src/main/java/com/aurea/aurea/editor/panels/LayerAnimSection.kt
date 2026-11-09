package com.aurea.aurea.editor.panels

import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import com.aurea.aurea.ui.ds.KeypadRequest
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.KeyframeDiamondIcon
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

/**
 * Parâmetro animável do animador de camada: id = LayerAnimParam. Posição na
 * consulta (Engine::layer_anim_slot): 0..17 em 6 + id; a onda (18..29) em
 * 32 + (id − 18), com os bits de keyframe em 44/45 (os antigos em 24/25).
 */
private class LayerAnimParam(val id: Int, @StringRes val label: Int, val unit: String, val step: Float, val min: Float, val max: Float) {
    val slot get() = if (id < FIRST_WAVE_PARAM) 6 + id else 32 + (id - FIRST_WAVE_PARAM)
    val animatedSlot get() = if (id < FIRST_WAVE_PARAM) 24 else 44
    val keyedSlot get() = if (id < FIRST_WAVE_PARAM) 25 else 45
    val bit get() = 1 shl (if (id < FIRST_WAVE_PARAM) id else id - FIRST_WAVE_PARAM)
}

private const val FIRST_WAVE_PARAM = 18
/** Forma da onda (0 seno, 1 triângulo), lida pelo set_layer_animator com a marca 2146 em 30. */
private const val WAVE_SHAPE_SLOT = 31

private val StrengthParams = listOf(
    LayerAnimParam(0, R.string.la_progress, "%", 0.5f, 0f, 100f),
    LayerAnimParam(1, R.string.la_strength, "%", 0.5f, 0f, 100f),
)
private val DelayParam = LayerAnimParam(2, R.string.la_delay, "ms", 2f, 0f, 10000f)
private val FromParams = listOf(
    LayerAnimParam(3, R.string.panel_opacidade, "%", 0.5f, 0f, 100f),
    LayerAnimParam(4, R.string.panel_posicao_x, "px", 1f, -20000f, 20000f),
    LayerAnimParam(5, R.string.panel_posicao_y, "px", 1f, -20000f, 20000f),
    LayerAnimParam(6, R.string.panel_escala, "%", 1f, 0f, 5000f),
    LayerAnimParam(7, R.string.fx_escala_y, "%", 1f, 0f, 5000f),
    LayerAnimParam(8, R.string.panel_rotacao, "°", 1f, -3600f, 3600f),
    LayerAnimParam(9, R.string.edt_rotation_x, "°", 1f, -3600f, 3600f),
    LayerAnimParam(10, R.string.edt_rotation_y, "°", 1f, -3600f, 3600f),
    LayerAnimParam(11, R.string.edt_prop_spacing, "px", 0.5f, -20000f, 20000f),
)
private val WiggleParams = listOf(
    LayerAnimParam(12, R.string.panel_posicao_x, "px", 1f, -20000f, 20000f),
    LayerAnimParam(13, R.string.panel_posicao_y, "px", 1f, -20000f, 20000f),
    LayerAnimParam(14, R.string.panel_escala, "%", 0.5f, 0f, 1000f),
    LayerAnimParam(15, R.string.panel_rotacao, "°", 0.5f, -3600f, 3600f),
    LayerAnimParam(16, R.string.la_wiggle_speed, "/s", 0.05f, 0f, 60f),
    LayerAnimParam(17, R.string.la_wiggle_hold, "%", 0.5f, 0f, 100f),
)
/** Onda (0.5.0): o animador automático do Alight Motion, em Hz e graus como no After Effects. */
private val WaveParams = listOf(
    LayerAnimParam(18, R.string.panel_posicao_x, "px", 1f, -20000f, 20000f),
    LayerAnimParam(20, R.string.la_wave_freq_x, "Hz", 0.01f, 0f, 60f),
    LayerAnimParam(19, R.string.panel_posicao_y, "px", 1f, -20000f, 20000f),
    LayerAnimParam(21, R.string.la_wave_freq_y, "Hz", 0.01f, 0f, 60f),
    LayerAnimParam(22, R.string.panel_rotacao, "°", 0.5f, -3600f, 3600f),
    LayerAnimParam(23, R.string.la_wave_rot_freq, "Hz", 0.01f, 0f, 60f),
    LayerAnimParam(24, R.string.panel_escala, "%", 0.5f, -1000f, 1000f),
    LayerAnimParam(25, R.string.la_wave_scale_freq, "Hz", 0.01f, 0f, 60f),
    LayerAnimParam(26, R.string.la_wave_opacity_low, "%", 0.5f, 0f, 100f),
    LayerAnimParam(27, R.string.la_wave_opacity_high, "%", 0.5f, 0f, 100f),
    LayerAnimParam(28, R.string.la_wave_opacity_freq, "Hz", 0.01f, 0f, 60f),
    LayerAnimParam(29, R.string.la_wave_phase, "°", 1f, -3600f, 3600f),
)

/**
 * ANIMADORES DA CAMADA (qualquer tipo), como a lista do Motion: uma linha por
 * animador; aberta, os ajustes vêm separados em Geral, Força e atraso, Começa de,
 * Tremor e Onda. O motor avalia (preview = export).
 */
@Composable
internal fun LayerAnimSection(env: PanelEnv) {
    val store = env.store
    val list by remember(store) { derivedStateOf { store.layerAnimators } }
    var open by remember(store.primary) { mutableStateOf(0) }
    PanelSection(stringResource(R.string.la_animators), CupertinoGlyph.Move, "layer.anim.sec", count = list.size) {
        if (list.isEmpty()) KitHint(stringResource(R.string.la_empty))
        list.forEachIndexed { index, v -> LayerAnimatorCard(env, index, v, open == index) { open = if (open == index) -1 else index } }
        PanelAddRow(stringResource(R.string.la_add), "layer.anim.add") { store.addLayerAnimator(); open = store.layerAnimators.size - 1 }
        PanelItemGrid(buildList {
            if (list.isNotEmpty()) add(PanelItem(CupertinoGlyph.DocOnDoc, stringResource(R.string.la_copy), "layer.anim.copy") { store.copyLayerAnimators() })
            add(PanelItem(CupertinoGlyph.DocOnClipboard, stringResource(R.string.la_paste), "layer.anim.paste") { store.pasteLayerAnimators() })
        })
    }
}

@Composable
private fun LayerAnimatorCard(env: PanelEnv, index: Int, v: FloatArray, open: Boolean, onToggle: () -> Unit) {
    val store = env.store
    var overlapGesture by remember(index) { mutableStateOf(false) }
    DisposableEffect(index) { onDispose { if (overlapGesture) store.endGesture() } }
    val isText = v[26] > 0.5f
    val unit = v[1].toInt()
    val on = v[0] > 0.5f
    val remove = stringResource(R.string.panel_remover)
    PanelNested { Column(Modifier.fillMaxWidth().padding(vertical = 3.dp).clip(RoundedCornerShape(5.dp)).background(AureaColors.FxLowSurface)) {
        Row(Modifier.fillMaxWidth().height(37.dp).padding(horizontal = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            Row(Modifier.weight(1f).fillMaxHeight().testTag("layer.anim.$index.open").tocavel(shrink = 1f, onClick = onToggle), verticalAlignment = Alignment.CenterVertically) {
                CupertinoIcon(if (open) CupertinoGlyph.ChevronDown else CupertinoGlyph.ChevronRight, 14.dp, AureaColors.Text.copy(alpha = if (open) 0.9f else 0.3f))
                Spacer(Modifier.width(8.dp))
                CupertinoIcon(CupertinoGlyph.Move, 18.dp, if (on) AureaColors.Text else AureaColors.Muted, autoMirror = false)
                Spacer(Modifier.width(8.dp))
                Text(stringResource(R.string.la_animator_n, index + 1), maxLines = 1,
                    style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = if (open) FontWeight.W700 else FontWeight.W600, color = if (on) AureaColors.Text else AureaColors.Muted)))
            }
            Box(Modifier.size(34.dp).testTag("layer.anim.$index.eye").tocavel(shrink = 1f) { store.setLayerAnimatorValues(index, mapOf(0 to if (on) 0f else 1f)) }, contentAlignment = Alignment.Center) {
                CupertinoIcon(if (on) CupertinoGlyph.Eye else CupertinoGlyph.EyeSlash, 18.dp, if (on) AureaColors.Text else AureaColors.Muted)
            }
            Box(Modifier.size(34.dp).testTag("layer.anim.$index.delete").semantics { contentDescription = remove }.tocavel(shrink = 1f) { store.removeLayerAnimator(index) }, contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.Trash, 18.dp, AureaColors.FxListIcon)
            }
        }
        if (!open) return@Column
        PanelSection(stringResource(R.string.fxo_general), CupertinoGlyph.SliderHorizontal3, "layer.anim.$index.sec.general", flat = true) {
            if (isText) {
                ChipRow(
                    stringResource(R.string.la_unit),
                    listOf(stringResource(R.string.la_unit_whole), stringResource(R.string.panel_letra), stringResource(R.string.panel_palavra), stringResource(R.string.panel_linha)),
                    unit,
                ) { store.setLayerAnimatorValues(index, mapOf(1 to it.toFloat())) }
            }
            ToggleLine(stringResource(R.string.la_exit), stringResource(R.string.la_exit_hint), v[2] > 0.5f) {
                store.setLayerAnimatorValues(index, mapOf(2 to if (it) 1f else 0f))
            }
            ChipRow(
                stringResource(R.string.la_ease),
                listOf(stringResource(R.string.la_ease_linear), stringResource(R.string.la_ease_smooth), stringResource(R.string.la_ease_inout), stringResource(R.string.la_ease_back)),
                v[3].toInt(),
            ) { store.setLayerAnimatorValues(index, mapOf(3 to it.toFloat())) }
        }
        PanelSection(stringResource(R.string.la_strength_delay), CupertinoGlyph.Speedometer, "layer.anim.$index.sec.strength", flat = true) {
            StrengthParams.forEach { LayerAnimRuler(env, index, it, v) }
            if (isText && unit != 0) {
                LayerAnimRuler(env, index, DelayParam, v)
                Text(stringResource(R.string.beta_overlap) + " · ${v[28].roundToInt()}%")
                androidx.compose.material3.Slider(value = v[28], valueRange = 0f..100f,
                    onValueChange = {
                        if (!overlapGesture) { store.beginGesture("overlap"); overlapGesture = true }
                        store.setLayerAnimatorValues(index, mapOf(28 to it))
                    },
                    onValueChangeFinished = { if (overlapGesture) { store.endGesture(); overlapGesture = false } },
                    modifier = Modifier.fillMaxWidth().height(48.dp).testTag("layer.anim.$index.overlap"))
            }
        }
        PanelSection(stringResource(R.string.la_from), CupertinoGlyph.ArrowDownRightSquare, "layer.anim.$index.sec.from", startOpen = false, flat = true) {
            val separate = v[4] > 0.5f
            FromParams.forEach { p ->
                if (p.id == 7 && !separate) return@forEach
                if (p.id == 11 && !(isText && unit != 0)) return@forEach
                LayerAnimRuler(env, index, p, v)
            }
            ToggleLine(stringResource(R.string.la_scale_separate), null, separate) { store.setLayerAnimatorValues(index, mapOf(4 to if (it) 1f else 0f)) }
        }
        PanelSection(stringResource(R.string.la_wiggle), CupertinoGlyph.Scribble, "layer.anim.$index.sec.wiggle", startOpen = false, flat = true) {
            WiggleParams.forEach { LayerAnimRuler(env, index, it, v) }
            Text(stringResource(R.string.la_wiggle_seed), modifier = Modifier.padding(top = 6.dp), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp)))
            Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
                ValueBox(v[5].toLong().toString(), onTap = null)
                Spacer(Modifier.width(8.dp))
                AnimChip(stringResource(R.string.la_reroll), false) { store.rerollLayerAnimator(index) }
            }
        }
        PanelSection(stringResource(R.string.la_wave), CupertinoGlyph.Waveform, "layer.anim.$index.sec.wave", startOpen = false, flat = true) {
            Text(stringResource(R.string.la_wave_hint), style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = AureaColors.Muted)))
            if (v.size > WAVE_SHAPE_SLOT) {
                Box(Modifier.fillMaxWidth().testTag("layer.anim.$index.wave.shape")) {
                    ChipRow(
                        stringResource(R.string.la_wave_shape),
                        listOf(stringResource(R.string.la_wave_sine), stringResource(R.string.la_wave_triangle)),
                        v[WAVE_SHAPE_SLOT].toInt(),
                    ) { store.setLayerAnimatorValues(index, mapOf(30 to 2146f, WAVE_SHAPE_SLOT to it.toFloat())) }
                }
            }
            WaveParams.forEach { if (v.size > it.keyedSlot) LayerAnimRuler(env, index, it, v) }
        }
    } }
}

@Composable
private fun ToggleLine(label: String, hint: String?, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp)))
            if (hint != null) Text(hint, style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = AureaColors.Muted)))
        }
        AureaToggle(checked = checked, onCheckedChange = onChange)
    }
}

/** Régua de um valor do animador, com o losango de keyframe (grava no cabeçote). */
@Composable
private fun LayerAnimRuler(env: PanelEnv, index: Int, p: LayerAnimParam, v: FloatArray) {
    val store = env.store
    val bit = p.bit
    val look = when {
        v[p.keyedSlot].toInt() and bit != 0 -> KeyframeLook.KeyHere
        v[p.animatedSlot].toInt() and bit != 0 -> KeyframeLook.Animated
        else -> KeyframeLook.None
    }
    val value = { store.layerAnimators.getOrNull(index)?.get(p.slot) ?: v[p.slot] }
    // Expressão no valor do animador (a mesma folha das outras propriedades).
    val exprKeys = listOf(com.aurea.aurea.engine.TrackKey(com.aurea.aurea.engine.TrackProperty.LAYER_ANIM_PARAM, index, p.id))
    val exprLook by androidx.compose.runtime.remember(store, index, p.id) {
        androidx.compose.runtime.derivedStateOf { store.expressionLook(exprKeys) }
    }
    val label = stringResource(p.label)
    val selected = store.timelineFocus == exprKeys
    val select = { store.focusLayerAnimator(exprKeys.single()) }
    PropertyCustomRow(
        label, selected = selected, onSelect = select, keyframe = look,
        modifier = Modifier.testTag("layer.anim.$index.param.${p.id}"),
        expression = exprLook, onExpression = { store.openExpression(label, exprKeys, 1f, p.unit) },
    ) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f).height(40.dp)) {
                TickRuler(
                    value = value,
                    unitsPerDp = p.step,
                    active = true,
                    modifier = Modifier.fillMaxSize().valueDrag(
                        enabled = true,
                        start = value,
                        unitsPerDp = { p.step },
                        min = p.min,
                        max = p.max,
                        onStart = { select(); store.beginGesture("animador") },
                        onValue = { store.setLayerAnimParam(index, p.id, it) },
                        onEnd = { store.endGesture() },
                    ),
                )
            }
            Spacer(Modifier.width(8.dp))
            val shown = v[p.slot]
            val decimals = if (p.step < 0.1f) 2 else if (p.step < 1f) 1 else 0
            ValueBox("${com.aurea.aurea.ui.ds.numeroPtBr(shown, decimals)}${p.unit}", onTap = {
                select()
                env.openKeypad(KeypadRequest(label, value(), p.unit, p.min, p.max, decimals) { store.setLayerAnimParam(index, p.id, it) })
            })
            Spacer(Modifier.width(4.dp))
            val keyAction = stringResource(if (look == KeyframeLook.KeyHere) R.string.panel_tirar_keyframe_daqui else R.string.panel_marcar_keyframe_aqui)
            Box(Modifier.size(40.dp, 44.dp).testTag("layer.anim.$index.key.${p.id}").semantics { contentDescription = "$keyAction · $label" }.tocavel(onClick = { select(); store.toggleLayerAnimKey(index, p.id) }), contentAlignment = Alignment.Center) {
                KeyframeDiamondIcon(look, enabled = true)
            }
        }
    }
    if (selected) AnimationTrackActions(env, exprKeys.single(), "layer.anim.$index.curve.${p.id}")
}
