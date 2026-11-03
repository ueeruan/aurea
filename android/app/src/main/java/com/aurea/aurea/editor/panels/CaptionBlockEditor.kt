package com.aurea.aurea.editor.panels
import com.aurea.aurea.R
import androidx.compose.ui.res.stringResource
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.aurea.aurea.captions.CaptionsState
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.i18n.ltrPlain
import org.json.JSONArray
import org.json.JSONObject

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun CaptionBlockEditor(state: CaptionsState, store: EditorStore) {
    val track = state.track ?: return
    val playhead = store.playhead
    var selected by remember(track.layer) { mutableStateOf(setOf<Long>()) }
    // 0.4.6: "Estilo" dos blocos marcados (o mesmo modelo/aparência da faixa, só neles).
    var styling by remember(track.layer) { mutableStateOf(false) }
    var body by remember { mutableStateOf("") }
    var start by remember { mutableStateOf("") }
    var end by remember { mutableStateOf("") }
    fun edit(op: String, extra: JSONObject.() -> Unit = {}) {
        state.editBlocks(JSONObject().put("op", op).put("ids", JSONArray(selected.toList())).apply(extra))
    }
    Row(verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
        Text(stringResource(R.string.edt_cap_track_hint), Modifier.weight(1f))
        // 0.4.6: escolher todos os blocos de uma vez (para apagar ou mover juntos).
        val all = track.segments.isNotEmpty() && selected.size == track.segments.size
        TextButton(onClick = { selected = if (all) emptySet() else track.segments.map { it.id }.toSet() }) {
            Text(stringResource(if (all) R.string.editor_limpar_selecao else R.string.home_select_all))
        }
    }
    Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(5.dp)) {
        val ownStyle = stringResource(R.string.cap046_block_own_style)
        track.segments.forEach { block ->
            FilterChip(selected = block.id in selected, onClick = {
                selected = if (block.id in selected) selected - block.id else selected + block.id
                body = block.text; start = block.start.toString(); end = block.end.toString()
            }, label = { Text("${ltrPlain("${block.start}–${block.end}")}\n${block.text}", maxLines = 2) },
                // Bloco com estilo próprio: um ponto na cor de destaque.
                leadingIcon = if (block.look == null) null else {
                    { Box(Modifier.size(8.dp).clip(CircleShape).background(AureaColors.Accent).semantics { contentDescription = ownStyle }) }
                })
        }
    }
    if (selected.isNotEmpty()) {
        FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            TextButton(onClick = { edit("move") { put("delta", -1) } }) { Text(stringResource(R.string.edt_cap_back_frame)) }
            TextButton(onClick = { edit("move") { put("delta", 1) } }) { Text(stringResource(R.string.edt_cap_fwd_frame)) }
            TextButton(onClick = { edit("split") { put("frame", playhead) } }, enabled = selected.size == 1) { Text(stringResource(R.string.edt_cap_split_here)) }
            TextButton(onClick = { edit("merge"); selected = emptySet() }, enabled = selected.size > 1) { Text(stringResource(R.string.panel_unir)) }
            TextButton(onClick = { edit("delete"); selected = emptySet() }) { Text(stringResource(R.string.common_delete)) }
            TextButton(onClick = { styling = !styling }) { Text(stringResource(R.string.panel_estilo) + if (styling) "  ▴" else "  ▾") }
            TextButton(onClick = { selected = emptySet() }) { Text(stringResource(R.string.editor_limpar_selecao)) }
        }
        if (styling) CaptionBlockStyle(state, store, selected)
        if (selected.size == 1) {
            OutlinedTextField(body, { body = it }, label = { Text(stringResource(R.string.panel_texto)) }, modifier = Modifier.fillMaxWidth())
            TextButton(onClick = { edit("text") { put("text", body) } }) { Text(stringResource(R.string.edt_cap_apply_text)) }
            Row {
                OutlinedTextField(start, { start = it }, label = { Text(stringResource(R.string.edt_cap_start_frame)) }, modifier = Modifier.weight(1f))
                OutlinedTextField(end, { end = it }, label = { Text(stringResource(R.string.edt_cap_end_frame)) }, modifier = Modifier.weight(1f))
            }
            TextButton(onClick = { val a = start.toIntOrNull(); val b = end.toIntOrNull(); if (a != null && b != null) edit("trim") { put("start", a); put("end", b) } }) { Text(stringResource(R.string.edt_cap_set_duration)) }
        }
    }
}

/**
 * Modelos e aparência só dos blocos [ids]: os mesmos controles da faixa
 * ([CaptionTemplateRow], [CaptionCustomize]) apontados para os blocos.
 */
@Composable
private fun CaptionBlockStyle(state: CaptionsState, store: EditorStore, ids: Set<Long>) {
    val look = remember(ids) { state.blockLook(ids) }
    val s = look.settings
    Text(stringResource(R.string.cap046_block_style_n, ids.size), Modifier.padding(top = 6.dp), color = AureaColors.Muted)
    CaptionTemplateRow(look)
    Row(Modifier.fillMaxWidth(), verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
        Text(stringResource(R.string.pn_caption_highlight_word), Modifier.weight(1f))
        Switch(s.highlight, { v -> look.update(custom = false) { it.copy(highlight = v) } }, enabled = look.lookEnabled)
    }
    Row(Modifier.fillMaxWidth(), verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
        Text(stringResource(R.string.pn_caption_uppercase), Modifier.weight(1f))
        Switch(s.uppercase, { v -> look.update(custom = false) { it.copy(uppercase = v) } }, enabled = look.lookEnabled)
    }
    CaptionCustomize(look, store)
    val own = state.track?.segments?.any { it.id in ids && it.look != null } == true
    TextButton(onClick = { look.clear() }, enabled = own && look.lookEnabled) { Text(stringResource(R.string.cap046_block_use_track)) }
}
