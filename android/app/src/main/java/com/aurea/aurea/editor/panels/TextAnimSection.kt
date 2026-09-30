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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.foundation.layout.heightIn
import androidx.compose.material3.TextButton
import androidx.compose.ui.platform.testTag
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.ui.ds.KeypadRequest
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.ColorWell
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

/** Presets nativos do motor (mesma ordem de `text_preset_name`). */
private val TextPresets = listOf(
    R.string.edt_tp_pop, R.string.edt_tp_jump, R.string.pn_textpreset_slide, R.string.panel_escala, R.string.pn_textpreset_appear,
    R.string.panel_desfoque, R.string.pn_textpreset_word_highlight, R.string.pn_pop, R.string.pn_textpreset_typewriter,
    R.string.pn_textpreset_wave, R.string.pn_textpreset_elastic,
    // Os de `ExtraTextPresetNames`, na mesma ordem.
    R.string.edt_tp_x_bounce, R.string.edt_tp_x_soft_in, R.string.edt_tp_x_reveal, R.string.edt_tp_x_slide,
    R.string.edt_tp_x_quick_in, R.string.edt_tp_x_elastic_jump, R.string.edt_tp_x_word_jump, R.string.edt_tp_x_smooth,
) + com.aurea.aurea.presets.PackTextPresetLabels

/** Presets remain compatible; new custom animation is authored in the effect stack. */
@Composable
internal fun TextAnimSection(env: PanelEnv) {
    val store = env.store
    Spacer(Modifier.height(10.dp))
    Text(stringResource(R.string.panel_animacao), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).height(44.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        TextPresets.forEachIndexed { i, name -> AnimChip(stringResource(name), false) { store.applyTextPreset(i) } }
    }
    TextButton(modifier = Modifier.testTag("text.transform.add"), onClick = {
        store.addEffect(effectTypeId("aurea.text.transform"))
        env.onOpenPanel(EditorPanel.Effects)
    }) { Text(stringResource(R.string.text_transform_add)) }
}

@Composable
internal fun ChipRow(label: String, options: List<String>, selected: Int, onPick: (Int) -> Unit) {
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).height(40.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
        options.forEachIndexed { i, o -> AnimChip(o, i == selected) { onPick(i) } }
    }
}

@Composable
internal fun AnimChip(label: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
            .tocavel(onClick = onClick).padding(horizontal = 10.dp, vertical = 6.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
    }
}

/** Régua de um valor do animador, com o losango de keyframe (grava no cabeçote). */
/** Navigation follows the chosen property, including after trimming a layer. */
@Composable
internal fun AnimationTrackActions(env: PanelEnv, target: TrackKey, curveTag: String) {
        val store = env.store
        val layer = store.primary
        val detail = store.detail
        val track = store.primaryKeys().filter { it.property == target.property && it.effectIndex == target.effectIndex && it.paramIndex == target.paramIndex }.sortedBy { it.time }
        val local = detail?.localPlayhead ?: 0
        val previous = track.lastOrNull { it.time < local }
        val next = track.firstOrNull { it.time > local }
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).heightIn(min = 44.dp)) {
            TextButton(enabled = previous != null, onClick = { previous?.let { key -> detail?.let { store.seek(it.timelineFrame(key.time)) } } }) { Text(stringResource(R.string.panel_keyframe_anterior)) }
            TextButton(enabled = next != null, onClick = { next?.let { key -> detail?.let { store.seek(it.timelineFrame(key.time)) } } }) { Text(stringResource(R.string.panel_proximo_keyframe)) }
            TextButton(enabled = track.size >= 2, modifier = Modifier.testTag(curveTag), onClick = {
                if (layer != null && track.size >= 2) {
                    val key = track.lastOrNull { it.time <= local && it != track.last() } ?: track.first()
                    store.selectKeyframe(layer, key)
                    env.onOpenPanel(EditorPanel.Curve)
                }
            }) { Text(stringResource(R.string.panel_curva)) }
        }
}
