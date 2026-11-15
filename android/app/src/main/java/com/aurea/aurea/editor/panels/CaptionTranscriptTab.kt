package com.aurea.aurea.editor.panels

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.captions.CaptionWorkspace
import com.aurea.aurea.captions.CaptionsState
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.i18n.ltrPlain
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel

/**
 * TRANSCRIÇÃO: a fala em parágrafos (pausas longas e fins de frase), cada um
 * com o tempo de início. Tocar numa palavra a escolhe e leva a agulha até ela;
 * a escolhida pode ser corrigida (o tempo fica) ou tirada. "Até aqui" estende
 * a escolha a um trecho, e o trecho vira legendas sem mexer no resto da faixa.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun CaptionTranscriptTab(cap: CaptionsState, store: EditorStore) {
    val words = cap.words
    val fillers = cap.fillers
    val struckOn = cap.settings.removeFillers
    var chosen by remember(cap.layer) { mutableStateOf<Int?>(null) }
    var anchor by remember(cap.layer) { mutableStateOf<Int?>(null) }   // começo do trecho escolhido
    var reach by remember(cap.layer) { mutableStateOf<Int?>(null) }    // fim do trecho (o último toque)
    var finding by remember { mutableStateOf(false) }
    val paragraphs = remember(words) { CaptionWorkspace.paragraphStarts(words) }
    fun seekTo(index: Int) {
        val w = words.getOrNull(index) ?: return
        val frame = cap.wordFrame(w.start)
        if (frame >= 0) store.seek(frame.coerceAtMost(Int.MAX_VALUE.toLong()).toInt())
    }
    val range = anchor?.let { a -> val r = reach ?: a; minOf(a, r)..maxOf(a, r) } ?: chosen?.let { it..it }

    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(horizontal = 18.dp, vertical = 10.dp)) {
        item(key = "status") { CaptionJobStatus(cap) }
        if (words.isEmpty()) {
            item(key = "vazio") { CapNote(stringResource(R.string.cap053_no_transcript), AureaColors.Muted) }
            return@LazyColumn
        }
        item(key = "topo") {
            Column {
                val src = cap.source
                CapLabel(
                    if (src != null) pluralStringResource(R.plurals.pn_caption_transcript_source, words.size, src, words.size)
                    else pluralStringResource(R.plurals.pn_caption_transcript, words.size, words.size),
                )
                Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(vertical = 6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    CapAction(stringResource(R.string.cap053_find_replace), primary = finding) { finding = !finding }
                    if (range != null) {
                        CapAction(stringResource(R.string.cap053_create_from_selection), primary = true, enabled = cap.busy == null) {
                            cap.createFromWords(range.first, range.last)
                        }
                        CapAction(stringResource(R.string.editor_limpar_selecao)) { chosen = null; anchor = null; reach = null }
                    }
                }
                if (finding) FindReplaceBox(onReplace = { find, with, exact -> cap.replaceInTranscript(find, with, exact).also { if (it > 0) { chosen = null; anchor = null; reach = null } } })
                if (anchor != null) CapNote(stringResource(R.string.cap053_select_hint), AureaColors.Muted)
                if (range != null && range.first != range.last) {
                    CapNote(stringResource(R.string.cap053_selection_n, range.last - range.first + 1), AureaColors.Accent)
                }
            }
        }
        items(paragraphs.size, key = { p -> "p${paragraphs[p]}" }, contentType = { "paragrafo" }) { p ->
            val from = paragraphs[p]
            val to = paragraphs.getOrNull(p + 1) ?: words.size
            Column(Modifier.fillMaxWidth().padding(bottom = 10.dp)) {
                Text(ltrPlain(CaptionWorkspace.clock(words[from].start)), Modifier.padding(bottom = 4.dp),
                    style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = AureaColors.Muted)))
                FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    for (i in from until to) {
                        val inRange = range != null && i in range
                        TranscriptWord(words[i].text, selected = inRange, struck = struckOn && i in fillers) {
                            // Com o começo marcado, o toque fecha o trecho; senão escolhe a palavra.
                            if (anchor != null) reach = i else chosen = i
                            seekTo(i)
                        }
                    }
                }
                val c = chosen
                if (c != null && c in from until to && anchor == null) {
                    words.getOrNull(c)?.let { w ->
                        WordEditor(w.text, ltrPlain("${com.aurea.aurea.ui.ds.numeroPtBr(w.start.toFloat(), 2)} s"),
                            onDone = { t -> cap.editWord(c, t); chosen = null },
                            onExtend = { anchor = c; reach = c })
                    }
                }
            }
        }
        item(key = "fim") { Spacer(Modifier.height(24.dp)) }
    }
}

private val WordStyle = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text))
private val WordStruckStyle = AureaType.Base.merge(
    TextStyle(fontSize = 14.sp, color = AureaColors.Muted, textDecoration = TextDecoration.LineThrough),
)

@Composable
private fun TranscriptWord(text: String, selected: Boolean, struck: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.clip(RoundedCornerShape(6.dp)).background(if (selected) AureaColors.AccentDim else androidx.compose.ui.graphics.Color.Transparent)
            .tocavel(role = Role.Button, onClick = onClick).semantics { this.selected = selected }
            .padding(horizontal = 4.dp, vertical = 3.dp),
    ) {
        Text(text, style = if (struck) WordStruckStyle else WordStyle)
    }
}

/** A palavra escolhida: corrigir (o tempo fica), tirar, ou começar um trecho a partir dela. */
@Composable
private fun WordEditor(initial: String, time: String, onDone: (String) -> Unit, onExtend: () -> Unit) {
    var text by remember(initial) { mutableStateOf(initial) }
    Column(Modifier.fillMaxWidth().padding(top = 8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(time, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
            Box(Modifier.weight(1f).clip(RoundedCornerShape(8.dp)).background(AureaColors.Chip).padding(10.dp)) {
                BasicTextField(text, onValueChange = { text = it }, singleLine = true, cursorBrush = SolidColor(AureaColors.Accent),
                    textStyle = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text)), modifier = Modifier.fillMaxWidth())
            }
        }
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(top = 6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            CapAction(stringResource(R.string.pn_ok), primary = true) { onDone(text) }
            CapAction(stringResource(R.string.pn_caption_delete_word)) { onDone("") }
            CapAction(stringResource(R.string.cap053_select_from_here)) { onExtend() }
        }
    }
}
