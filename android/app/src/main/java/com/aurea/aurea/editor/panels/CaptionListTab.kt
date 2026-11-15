package com.aurea.aurea.editor.panels

import android.content.Context
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.captions.CaptionBlock
import com.aurea.aurea.captions.CaptionTrack
import com.aurea.aurea.captions.CaptionWorkspace
import com.aurea.aurea.captions.CaptionsState
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.i18n.ltrPlain
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject

/**
 * LEGENDAS (a lista, o centro do trabalho): cada bloco com entrada–saída e
 * texto; a linha da agulha fica marcada. Tocar escolhe e leva a agulha até o
 * bloco; o escolhido abre na linha (texto, ‹ › de quadro, dividir, unir,
 * inserir, apagar, estilo próprio). Toque longo: escolha múltipla.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
internal fun CaptionListTab(cap: CaptionsState, store: EditorStore, onOwnStyle: () -> Unit) {
    val track = cap.track
    val fps = store.project.fps.takeIf { it > 0f }?.toDouble() ?: 30.0
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val listState = rememberLazyListState()
    var multi by remember(track?.layer) { mutableStateOf(false) }
    var finding by remember { mutableStateOf(false) }
    var message by remember { mutableStateOf<String?>(null) }
    val exportedMsg = stringResource(R.string.cap053_exported)
    val failedMsg = stringResource(R.string.cap053_export_failed)
    fun save(uri: Uri, vtt: Boolean) {
        val text = cap.exportCaptions(vtt)
        scope.launch {
            val ok = withContext(Dispatchers.IO) { writeText(context, uri, text) }
            message = if (ok && text.isNotEmpty()) exportedMsg else failedMsg
        }
    }
    val exportSrt = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/x-subrip")) { uri -> uri?.let { save(it, false) } }
    val exportVtt = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("text/vtt")) { uri -> uri?.let { save(it, true) } }
    // A linha da agulha só recompõe quando o bloco muda (não a cada quadro da reprodução).
    val active by remember(track) { derivedStateOf { track?.let { CaptionWorkspace.activeIndex(it, store.playhead) } ?: -1 } }
    LaunchedEffect(active) {
        // Tocando sem nada escolhido, a lista acompanha a fala.
        if (active >= 0 && cap.selectedBlocks.isEmpty()) listState.animateScrollToItem(active + 2)
    }
    val newText = stringResource(R.string.cap053_new_caption_text)
    fun edit(op: String, ids: Collection<Long>, failure: Int = R.string.app_caption_edit_overlap, extra: JSONObject.() -> Unit = {}): Boolean =
        cap.editBlocks(JSONObject().put("op", op).put("ids", JSONArray(ids.toList())).apply(extra), failure)

    LazyColumn(Modifier.fillMaxSize(), state = listState, contentPadding = PaddingValues(horizontal = 14.dp, vertical = 10.dp)) {
        item(key = "status") { CaptionJobStatus(cap) }
        if (track == null) {
            item(key = "vazio") { CapNote(stringResource(R.string.cap053_no_captions), AureaColors.Muted) }
            return@LazyColumn
        }
        item(key = "ferramentas") {
            Column {
                Row(
                    Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(vertical = 4.dp),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    CapAction(stringResource(R.string.cap053_insert), primary = true) {
                        val before = track.segments.maxOfOrNull { it.id } ?: 0L
                        if (edit("insert", emptyList()) { put("playhead", store.playhead); put("text", newText) }) {
                            cap.track?.segments?.firstOrNull { it.id > before }?.let { cap.selectedBlocks = setOf(it.id); multi = false }
                        }
                    }
                    CapAction(stringResource(R.string.cap053_find_replace), primary = finding) { finding = !finding }
                    CapAction(stringResource(if (multi) R.string.cap053_done_choosing else R.string.cap053_choose_many), primary = multi) {
                        multi = !multi
                        if (!multi && cap.selectedBlocks.size > 1) cap.selectedBlocks = emptySet()
                    }
                    CapAction(stringResource(R.string.cap053_export_srt)) { exportSrt.launch("legendas.srt") }
                    CapAction(stringResource(R.string.cap053_export_vtt)) { exportVtt.launch("legendas.vtt") }
                }
                if (finding) FindReplaceBox(onReplace = { find, with, exact ->
                    val ids = if (multi) cap.selectedBlocks else emptySet()
                    val n = track.segments.filter { ids.isEmpty() || it.id in ids }.sumOf { countIn(it.text, find, exact) }
                    if (n > 0 && edit("replace", ids, R.string.cap053_not_found) { put("find", find); put("replace", with); put("matchCase", exact) }) n else 0
                })
                message?.let { CapNote(it, AureaColors.Muted) }
                val chosen = cap.selectedBlocks
                if (multi && chosen.isNotEmpty()) {
                    CapNote(stringResource(R.string.cap053_selected_n, chosen.size), AureaColors.Accent)
                    Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        CapAction(stringResource(R.string.panel_unir), enabled = chosen.size > 1) { edit("merge", chosen) }
                        CapAction(stringResource(R.string.cap053_regroup)) { edit("regroup", chosen) }
                        CapAction(stringResource(R.string.cap053_own_style)) { onOwnStyle() }
                        CapAction(stringResource(R.string.common_delete)) { edit("delete", chosen) }
                        CapAction(stringResource(R.string.editor_limpar_selecao)) { cap.selectedBlocks = emptySet() }
                    }
                }
            }
        }
        itemsIndexed(track.segments, key = { _, b -> b.id }) { i, block ->
            val chosen = block.id in cap.selectedBlocks
            CaptionRow(track, block, fps, chosen = chosen, playing = i == active, multi = multi, onTap = {
                if (multi) cap.selectedBlocks = if (chosen) cap.selectedBlocks - block.id else cap.selectedBlocks + block.id
                else {
                    cap.selectedBlocks = if (chosen) emptySet() else setOf(block.id)
                    store.seek(block.start + track.shift)
                }
            }, onLongTap = {
                multi = true
                cap.selectedBlocks = cap.selectedBlocks + block.id
            })
            if (!multi && chosen && cap.selectedBlocks.size == 1) {
                CaptionRowEditor(cap, store, track, i, fps, onOwnStyle, ::edit)
            }
        }
        item(key = "fim") { Spacer(Modifier.height(24.dp)) }
    }
}

/** Uma linha da lista: tempo de entrada–saída, texto (2 linhas) e o ponto de estilo próprio. */
@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun CaptionRow(
    track: CaptionTrack, block: CaptionBlock, fps: Double, chosen: Boolean, playing: Boolean, multi: Boolean,
    onTap: () -> Unit, onLongTap: () -> Unit,
) {
    val time = ltrPlain(CaptionWorkspace.timecode(block.start + track.shift, fps) + " – " + CaptionWorkspace.timecode(block.end + track.shift, fps))
    val ownStyle = stringResource(R.string.cap046_block_own_style)
    val chooseMany = stringResource(R.string.cap053_choose_many)
    Row(
        Modifier.fillMaxWidth().padding(vertical = 3.dp).clip(RoundedCornerShape(10.dp))
            .background(if (chosen) AureaColors.AccentDim else if (playing) AureaColors.Chip.copy(alpha = 0.9f) else AureaColors.Chip.copy(alpha = 0.45f))
            .combinedClickable(onClick = onTap, onLongClick = onLongTap, onLongClickLabel = chooseMany)
            .semantics(mergeDescendants = true) { selected = chosen }
            .heightIn(min = 52.dp).padding(horizontal = 12.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // Marca da agulha: a barra à esquerda na cor de destaque.
        Box(Modifier.width(3.dp).height(30.dp).clip(RoundedCornerShape(2.dp))
            .background(if (playing) AureaColors.Accent else androidx.compose.ui.graphics.Color.Transparent))
        Spacer(Modifier.width(8.dp))
        Column(Modifier.weight(1f)) {
            Text(time, style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = if (chosen) AureaColors.Accent else AureaColors.Muted)))
            Text(block.text.replace('\n', ' '), maxLines = 2,
                style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text)))
        }
        if (block.look != null) {
            Box(Modifier.padding(start = 6.dp).size(8.dp).clip(CircleShape).background(AureaColors.Accent).semantics { contentDescription = ownStyle })
        }
        if (multi) {
            Spacer(Modifier.width(8.dp))
            Text(if (chosen) "●" else "○", style = AureaType.Base.merge(TextStyle(fontSize = 16.sp, color = if (chosen) AureaColors.Accent else AureaColors.Muted)))
        }
    }
}

/** O bloco escolhido aberto na linha: texto, entrada/saída de quadro em quadro e as ações. */
@Composable
private fun CaptionRowEditor(
    cap: CaptionsState, store: EditorStore, track: CaptionTrack, index: Int, fps: Double, onOwnStyle: () -> Unit,
    edit: (String, Collection<Long>, Int, JSONObject.() -> Unit) -> Boolean,
) {
    val block = track.segments[index]
    var field by remember(block.id, block.text) { mutableStateOf(TextFieldValue(block.text, TextRange(block.text.length))) }
    val ids = listOf(block.id)
    val overlap = R.string.app_caption_edit_overlap
    Column(Modifier.fillMaxWidth().padding(start = 8.dp, end = 4.dp, bottom = 10.dp)) {
        Box(Modifier.fillMaxWidth().padding(top = 4.dp).clip(RoundedCornerShape(8.dp)).background(AureaColors.Chip).padding(10.dp)) {
            BasicTextField(field, onValueChange = { field = it }, cursorBrush = SolidColor(AureaColors.Accent), maxLines = 4,
                textStyle = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Text)), modifier = Modifier.fillMaxWidth())
        }
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(top = 6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            CapAction(stringResource(R.string.edt_cap_apply_text), primary = true, enabled = field.text.isNotBlank() && field.text != block.text) {
                edit("text", ids, overlap) { put("text", field.text.trim()) }
            }
            val words = CaptionWorkspace.wordsBefore(field.text, field.selection.start)
            CapAction(stringResource(R.string.cap053_split_cursor), enabled = field.text == block.text && words > 0) {
                edit("split_word", ids, R.string.cap053_split_failed) { put("words", words) }
            }
        }
        EdgeRow(stringResource(R.string.cap053_in), CaptionWorkspace.timecode(block.start + track.shift, fps),
            onBack = { edit("nudge", ids, overlap) { put("in", -1) } }, onForward = { edit("nudge", ids, overlap) { put("in", 1) } })
        EdgeRow(stringResource(R.string.cap053_out), CaptionWorkspace.timecode(block.end + track.shift, fps),
            onBack = { edit("nudge", ids, overlap) { put("out", -1) } }, onForward = { edit("nudge", ids, overlap) { put("out", 1) } })
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(top = 4.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            CapAction(stringResource(R.string.cap053_go_to)) { store.seek(block.start + track.shift) }
            CapAction(stringResource(R.string.edt_cap_split_here)) { edit("split", ids, R.string.cap053_split_failed) { put("playhead", store.playhead) } }
            val prev = track.segments.getOrNull(index - 1)
            val next = track.segments.getOrNull(index + 1)
            CapAction(stringResource(R.string.cap053_merge_prev), enabled = prev != null) {
                prev?.let { p -> if (edit("merge", listOf(p.id, block.id), overlap) {}) cap.selectedBlocks = setOf(p.id) }
            }
            CapAction(stringResource(R.string.cap053_merge_next), enabled = next != null) {
                next?.let { n -> edit("merge", listOf(block.id, n.id), overlap) {} }
            }
            CapAction(stringResource(R.string.cap053_own_style)) { onOwnStyle() }
            CapAction(stringResource(R.string.common_delete)) { edit("delete", ids, overlap) {} }
        }
    }
}

/** Entrada ou saída: o tempo e os ‹ › de um quadro (presos nos vizinhos pelo motor). */
@Composable
private fun EdgeRow(label: String, time: String, onBack: () -> Unit, onForward: () -> Unit) {
    val back = stringResource(R.string.cap053_frame_back)
    val forward = stringResource(R.string.cap053_frame_fwd)
    Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, Modifier.width(72.dp), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
        Text(ltrPlain(time), Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600)))
        CapAction("‹", description = "$label: $back", onClick = onBack)
        Spacer(Modifier.width(6.dp))
        CapAction("›", description = "$label: $forward", onClick = onForward)
    }
}

/**
 * Buscar e substituir (lista e transcrição). [onReplace] devolve quantas trocas
 * houve; zero mostra "nada encontrado".
 */
@Composable
internal fun FindReplaceBox(onReplace: (find: String, with: String, matchCase: Boolean) -> Int) {
    var find by remember { mutableStateOf("") }
    var with by remember { mutableStateOf("") }
    var exact by remember { mutableStateOf(false) }
    var result by remember { mutableStateOf<String?>(null) }
    val notFound = stringResource(R.string.cap053_not_found)
    val replacedFmt = stringResource(R.string.cap053_replaced_n)
    Column(Modifier.fillMaxWidth().padding(vertical = 6.dp)) {
        CapTextField(find, stringResource(R.string.cap053_find)) { find = it; result = null }
        Spacer(Modifier.height(6.dp))
        CapTextField(with, stringResource(R.string.cap053_replace_with)) { with = it }
        CapToggle(stringResource(R.string.cap053_match_case), exact) { exact = it }
        CapAction(stringResource(R.string.cap053_replace_all), primary = true, enabled = find.isNotEmpty()) {
            val n = onReplace(find, with, exact)
            result = if (n > 0) String.format(replacedFmt, n) else notFound
        }
        result?.let { CapNote(it, AureaColors.Muted) }
    }
}

/** Campo de uma linha com o rótulo por dentro quando vazio (cara do AUREA). */
@Composable
internal fun CapTextField(value: String, hint: String, onChange: (String) -> Unit) {
    Box(Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp)).background(AureaColors.Chip).padding(10.dp)
        .semantics { contentDescription = hint }) {
        if (value.isEmpty()) Text(hint, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Muted)))
        BasicTextField(value, onValueChange = onChange, singleLine = true, cursorBrush = SolidColor(AureaColors.Accent),
            textStyle = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text)), modifier = Modifier.fillMaxWidth())
    }
}

private fun countIn(text: String, find: String, exact: Boolean): Int {
    if (find.isEmpty()) return 0
    var n = 0
    var at = text.indexOf(find, ignoreCase = !exact)
    while (at >= 0) { n++; at = text.indexOf(find, at + find.length, ignoreCase = !exact) }
    return n
}

private fun writeText(context: Context, uri: Uri, text: String): Boolean = runCatching {
    context.contentResolver.openOutputStream(uri, "wt")?.use { it.write(text.toByteArray(Charsets.UTF_8)) } != null
}.getOrDefault(false)
