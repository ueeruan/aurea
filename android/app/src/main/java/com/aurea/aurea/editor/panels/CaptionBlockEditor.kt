package com.aurea.aurea.editor.panels
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.aurea.aurea.captions.CaptionsState
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
    Text("Faixa de legendas · selecione um ou vários blocos")
    Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(5.dp)) {
        track.segments.forEach { block ->
            FilterChip(selected = block.id in selected, onClick = {
                selected = if (block.id in selected) selected - block.id else selected + block.id
                body = block.text; start = block.start.toString(); end = block.end.toString()
            }, label = { Text("${block.start}–${block.end}\n${block.text}", maxLines = 2) })
        }
    }
    if (selected.isNotEmpty()) {
        FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            TextButton(onClick = { edit("move") { put("delta", -1) } }) { Text("← 1 quadro") }
            TextButton(onClick = { edit("move") { put("delta", 1) } }) { Text("1 quadro →") }
            TextButton(onClick = { edit("split") { put("frame", playhead) } }, enabled = selected.size == 1) { Text("Dividir aqui") }
            TextButton(onClick = { edit("merge"); selected = emptySet() }, enabled = selected.size > 1) { Text("Unir") }
            TextButton(onClick = { edit("delete"); selected = emptySet() }) { Text("Excluir") }
            TextButton(onClick = { selected = emptySet() }) { Text("Limpar seleção") }
        }
        if (selected.size == 1) {
            OutlinedTextField(body, { body = it }, label = { Text("Texto") }, modifier = Modifier.fillMaxWidth())
            TextButton(onClick = { edit("text") { put("text", body) } }) { Text("Aplicar texto mantendo os tempos") }
            Row {
                OutlinedTextField(start, { start = it }, label = { Text("Início · quadro") }, modifier = Modifier.weight(1f))
                OutlinedTextField(end, { end = it }, label = { Text("Fim · quadro") }, modifier = Modifier.weight(1f))
            }
            TextButton(onClick = { val a = start.toIntOrNull(); val b = end.toIntOrNull(); if (a != null && b != null) edit("trim") { put("start", a); put("end", b) } }) { Text("Ajustar duração") }
        }
    }
}
