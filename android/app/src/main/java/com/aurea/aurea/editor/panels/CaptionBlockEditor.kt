package com.aurea.aurea.editor.panels
import com.aurea.aurea.R
import androidx.compose.ui.res.stringResource
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.aurea.aurea.captions.CaptionsState
import com.aurea.aurea.ui.i18n.ltrPlain
import org.json.JSONArray
import org.json.JSONObject

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun CaptionBlockEditor(state: CaptionsState, playhead: Int) {
    val track = state.track ?: return
    var selected by remember(track.layer) { mutableStateOf(setOf<Long>()) }
    var body by remember { mutableStateOf("") }
    var start by remember { mutableStateOf("") }
    var end by remember { mutableStateOf("") }
    fun edit(op: String, extra: JSONObject.() -> Unit = {}) {
        state.editBlocks(JSONObject().put("op", op).put("ids", JSONArray(selected.toList())).apply(extra))
    }
    Text(stringResource(R.string.edt_cap_track_hint))
    Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(5.dp)) {
        track.segments.forEach { block ->
            FilterChip(selected = block.id in selected, onClick = {
                selected = if (block.id in selected) selected - block.id else selected + block.id
                body = block.text; start = block.start.toString(); end = block.end.toString()
            }, label = { Text("${ltrPlain("${block.start}–${block.end}")}\n${block.text}", maxLines = 2) })
        }
    }
    if (selected.isNotEmpty()) {
        FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            TextButton(onClick = { edit("move") { put("delta", -1) } }) { Text(stringResource(R.string.edt_cap_back_frame)) }
            TextButton(onClick = { edit("move") { put("delta", 1) } }) { Text(stringResource(R.string.edt_cap_fwd_frame)) }
            TextButton(onClick = { edit("split") { put("frame", playhead) } }, enabled = selected.size == 1) { Text(stringResource(R.string.edt_cap_split_here)) }
            TextButton(onClick = { edit("merge"); selected = emptySet() }, enabled = selected.size > 1) { Text(stringResource(R.string.panel_unir)) }
            TextButton(onClick = { edit("delete"); selected = emptySet() }) { Text(stringResource(R.string.common_delete)) }
            TextButton(onClick = { selected = emptySet() }) { Text(stringResource(R.string.editor_limpar_selecao)) }
        }
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
