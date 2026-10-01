package com.aurea.aurea.editor.panels

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

/** Parâmetro animável do animador de camada: id = LayerAnimParam; slot = 6 + id. */
private class LayerAnimParam(val id: Int, @StringRes val label: Int, val unit: String, val step: Float, val min: Float, val max: Float) {
    val slot get() = 6 + id
}

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

/**
 * ANIMADORES DA CAMADA (qualquer tipo): entrada "começa de", saída, força e
 * atraso entre letras, curva e wiggle — o mesmo padrão de cartão, régua e
 * losango da animação de texto. O motor avalia (preview = export).
 */
@Composable
internal fun LayerAnimSection(env: PanelEnv) {
    val store = env.store
    val list by remember(store) { derivedStateOf { store.layerAnimators } }
    Text(stringResource(R.string.la_animators), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    if (list.isEmpty()) {
        Text(
            stringResource(R.string.la_empty),
            modifier = Modifier.padding(top = 6.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)),
        )
    }
    list.forEachIndexed { index, v -> LayerAnimatorCard(env, index, v) }
    Spacer(Modifier.height(6.dp))
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).height(44.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        AnimChip(stringResource(R.string.la_add), false) { store.addLayerAnimator() }
        if (list.isNotEmpty()) AnimChip(stringResource(R.string.la_copy), false) { store.copyLayerAnimators() }
        AnimChip(stringResource(R.string.la_paste), false) { store.pasteLayerAnimators() }
    }
}

@Composable
private fun LayerAnimatorCard(env: PanelEnv, index: Int, v: FloatArray) {
    val store = env.store
    val isText = v[26] > 0.5f
    val unit = v[1].toInt()
    Spacer(Modifier.height(8.dp))
    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip.copy(alpha = 0.45f)).padding(8.dp)) {
        Row(Modifier.fillMaxWidth().height(40.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.la_animator_n, index + 1), modifier = Modifier.weight(1f),
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700)))
            AnimChip(stringResource(R.string.panel_remover), false) { store.removeLayerAnimator(index) }
            Spacer(Modifier.width(8.dp))
            AureaToggle(checked = v[0] > 0.5f, onCheckedChange = { store.setLayerAnimatorValues(index, mapOf(0 to if (it) 1f else 0f)) })
        }
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

        GroupLabel(stringResource(R.string.la_strength_delay))
        StrengthParams.forEach { LayerAnimRuler(env, index, it, v) }
        if (isText && unit != 0) LayerAnimRuler(env, index, DelayParam, v)

        GroupLabel(stringResource(R.string.la_from))
        val separate = v[4] > 0.5f
        FromParams.forEach { p ->
            if (p.id == 7 && !separate) return@forEach
            if (p.id == 11 && !(isText && unit != 0)) return@forEach
            LayerAnimRuler(env, index, p, v)
        }
        ToggleLine(stringResource(R.string.la_scale_separate), null, separate) { store.setLayerAnimatorValues(index, mapOf(4 to if (it) 1f else 0f)) }

        GroupLabel(stringResource(R.string.la_wiggle))
        WiggleParams.forEach { LayerAnimRuler(env, index, it, v) }
        Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.la_wiggle_seed), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp)))
            ValueBox(v[5].toLong().toString(), onTap = null)
            Spacer(Modifier.width(8.dp))
            AnimChip(stringResource(R.string.la_reroll), false) { store.rerollLayerAnimator(index) }
        }
    }
}

@Composable
private fun GroupLabel(text: String) {
    Text(text, modifier = Modifier.padding(top = 10.dp, bottom = 2.dp),
        style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
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
    val bit = 1 shl p.id
    val look = when {
        v[25].toInt() and bit != 0 -> KeyframeLook.KeyHere
        v[24].toInt() and bit != 0 -> KeyframeLook.Animated
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
