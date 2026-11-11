package com.aurea.aurea.editor.timeline

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.i18n.LocaleDirection
import com.aurea.aurea.ui.theme.AureaColors

// =============================================================================
// Timeline do AE (EngineAeTimeline.cpp): os pedaços de tela que o Android
// soma à timeline — duração e comentário das marcas, o editor da marca de
// CAMADA, o passo do "Escalonar" e a opção de exportar só a área de trabalho.
// O mesmo no iOS (TimelineView.swift / EditorView.swift / ExportView.swift).
// =============================================================================

/** Duração (quadros, com o tempo ao lado) e comentário de uma marca. */
@Composable
internal fun MarkerExtraFields(
    duration: String,
    onDuration: (String) -> Unit,
    comment: String,
    onComment: (String) -> Unit,
    fps: Float,
) {
    OutlinedTextField(duration, { if (it.length <= 9) onDuration(it.filter(Char::isDigit)) }, singleLine = true,
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
        label = { Text(stringResource(R.string.aet_marker_duration)) },
        supportingText = { Text(duration.toIntOrNull()?.let { Timecode.format(it.coerceAtLeast(0), fps) } ?: "—") },
        modifier = Modifier.testTag("marker.duration"))
    OutlinedTextField(comment, { if (it.length <= 1000) onComment(it) }, minLines = 2, maxLines = 4,
        label = { Text(stringResource(R.string.aet_marker_comment)) },
        placeholder = { Text(stringResource(R.string.aet_marker_comment_hint)) },
        modifier = Modifier.testTag("marker.comment"))
}

private fun layerMarkerPacked(index: Int): Int = MarkerColors.pack(MarkerColors.LEGACY[index])

/**
 * Editor da marca da CAMADA (a que anda com o clipe e que `thisLayer.marker`
 * lê): nome, quadro, duração, comentário e cor. Uma marca nova só nasce no
 * Salvar, como a da régua.
 */
@Composable
internal fun LayerMarkerEditor(store: EditorStore) {
    val editing = store.layerMarkerEditing ?: return
    val (layer, original) = editing
    val isNew = store.layerMarkerEditingIsNew
    if (!isNew && !store.layerMarkers.has(layer, original)) {
        LaunchedEffect(editing) { store.closeLayerMarkerEditor() }
        return
    }
    val text = remember(editing, isNew) { if (isNew) null else store.layerMarkerText(layer, original) }
    var name by remember(editing, isNew) { mutableStateOf(text?.first.orEmpty()) }
    var comment by remember(editing, isNew) { mutableStateOf(text?.second.orEmpty()) }
    var frame by remember(editing, isNew) { mutableStateOf(original.toString()) }
    var duration by remember(editing, isNew) { mutableStateOf((if (isNew) 0 else store.layerMarkerDuration(layer, original)).toString()) }
    var color by remember(editing, isNew) { mutableIntStateOf(if (isNew) MarkerColors.ENGINE_DEFAULT else store.layerMarkerColor(layer, original)) }
    var failure by remember(editing) { mutableStateOf(false) }
    val layerName = store.layers.firstOrNull { it.id == layer }?.name.orEmpty()
    LocaleDirection {
        AlertDialog(
            onDismissRequest = { store.closeLayerMarkerEditor() },
            title = { Text(stringResource(R.string.aet_layer_marker_edit)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    if (layerName.isNotEmpty()) Text(layerName, fontSize = 12.sp, color = AureaColors.Muted)
                    OutlinedTextField(name, { if (it.length <= 200) name = it }, singleLine = true,
                        label = { Text(stringResource(R.string.marker_name)) },
                        placeholder = { Text(stringResource(R.string.marker_name_hint)) },
                        modifier = Modifier.testTag("layerMarker.name"))
                    OutlinedTextField(frame, { frame = it.filter(Char::isDigit) }, singleLine = true,
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                        label = { Text(stringResource(R.string.marker_frame)) },
                        supportingText = { Text(frame.toIntOrNull()?.let { Timecode.format(it, store.project.fps) } ?: "—") })
                    TextButton(onClick = { frame = store.playhead.toString() }) { Text(stringResource(R.string.marker_at_playhead)) }
                    MarkerExtraFields(duration, { duration = it }, comment, { comment = it }, store.project.fps)
                    Row(Modifier.fillMaxWidth()) {
                        val chosen = MarkerColors.legacyIndex(color)
                        MarkerColors.LEGACY.forEachIndexed { i, argb ->
                            val description = stringResource(R.string.marker_color, i + 1)
                            Box(Modifier.weight(1f).height(44.dp).clickable { color = layerMarkerPacked(i) }
                                .semantics { contentDescription = description }.testTag("layerMarker.color.$i"),
                                contentAlignment = Alignment.Center) {
                                Box(Modifier.size(if (chosen == i) 32.dp else 22.dp).background(Color(argb), CircleShape))
                            }
                        }
                    }
                    Text(stringResource(if (failure) R.string.marker_error else R.string.aet_layer_marker_hint), fontSize = 12.sp)
                    if (!isNew) {
                        TextButton(onClick = { store.deleteLayerMarker(layer, original); store.closeLayerMarkerEditor() },
                            modifier = Modifier.testTag("layerMarker.delete")) { Text(stringResource(R.string.common_delete)) }
                    }
                }
            },
            confirmButton = {
                TextButton(onClick = {
                    val target = frame.toIntOrNull()
                    val length = duration.ifEmpty { "0" }.toIntOrNull()
                    if (target != null && length != null &&
                        store.editLayerMarker(layer, if (isNew) -1 else original, target, color, name, length, comment)) {
                        store.closeLayerMarkerEditor()
                    } else failure = true
                }, modifier = Modifier.testTag("layerMarker.save")) { Text(stringResource(R.string.common_save)) }
            },
            dismissButton = { TextButton(onClick = { store.closeLayerMarkerEditor() }) { Text(stringResource(R.string.common_cancel)) } },
        )
    }
}

/**
 * O passo do "Escalonar" (`Engine::stagger_layers`, gap_system item 4): −/+
 * em quadros, positivo = de cima para baixo. Camadas inteiras ou só os
 * keyframes. Fica na barra das camadas escolhidas.
 */
@Composable
internal fun StaggerStepper(store: EditorStore, modifier: Modifier = Modifier) {
    var step by remember { mutableIntStateOf(Stagger.DEFAULT) }
    var keysOnly by remember { mutableStateOf(false) }
    val enabled = store.layers.count { it.id in store.selection && !it.locked } >= 2
    val stepDescription = stringResource(R.string.aet_stagger_step_value, step)
    Row(modifier.fillMaxWidth().padding(horizontal = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.aet_stagger), fontSize = 13.sp, color = AureaColors.Text, modifier = Modifier.padding(end = 6.dp))
        val less = stringResource(R.string.aet_stagger_less)
        val more = stringResource(R.string.aet_stagger_more)
        TextButton(onClick = { step = Stagger.step(step, -1) }, modifier = Modifier.semantics { contentDescription = less }.testTag("stagger.minus")) {
            Text("−", fontSize = 18.sp, color = AureaColors.Text)
        }
        Text("$step", fontSize = 14.sp, color = AureaColors.Text,
            modifier = Modifier.width(40.dp).semantics { contentDescription = stepDescription }.testTag("stagger.value"))
        TextButton(onClick = { step = Stagger.step(step, 1) }, modifier = Modifier.semantics { contentDescription = more }.testTag("stagger.plus")) {
            Text("+", fontSize = 18.sp, color = AureaColors.Text)
        }
        val keysLabel = stringResource(R.string.aet_stagger_keys_only)
        val on = stringResource(R.string.aet_on)
        val off = stringResource(R.string.aet_off)
        Text(keysLabel, fontSize = 12.sp, color = AureaColors.Muted, modifier = Modifier.padding(start = 4.dp))
        Switch(keysOnly, { keysOnly = it }, modifier = Modifier.padding(horizontal = 4.dp)
            .semantics { contentDescription = keysLabel; stateDescription = if (keysOnly) on else off }.testTag("stagger.keysOnly"))
        Box(Modifier.weight(1f))
        TextButton(onClick = { store.staggerSelection(step, keysOnly) }, enabled = enabled,
            modifier = Modifier.background(AureaColors.EditorPanel, RoundedCornerShape(8.dp)).testTag("stagger.apply")) {
            Text(stringResource(R.string.aet_stagger_apply), fontSize = 13.sp)
        }
    }
}

/** "Só a área de trabalho" da exportação (aparece quando a composição tem uma). */
@Composable
internal fun ExportWorkAreaOption(store: EditorStore, modifier: Modifier = Modifier) {
    val area = store.workArea ?: return
    val label = stringResource(R.string.aet_export_work_area)
    val on = stringResource(R.string.aet_on)
    val off = stringResource(R.string.aet_off)
    val fps = store.project.fps
    Row(modifier.fillMaxWidth().clickable { store.changeExportWorkAreaOnly(!store.exportWorkAreaOnly) }
        .semantics(mergeDescendants = true) { stateDescription = if (store.exportWorkAreaOnly) on else off }
        .testTag("export.workAreaOnly"), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(label, fontSize = 14.sp, color = AureaColors.Text)
            Text(stringResource(R.string.aet_export_work_area_range,
                Timecode.format(area.first, fps), Timecode.format(area.second, fps)), fontSize = 12.sp, color = AureaColors.Muted)
        }
        Switch(store.exportWorkAreaOnly, { store.changeExportWorkAreaOnly(it) })
    }
}
