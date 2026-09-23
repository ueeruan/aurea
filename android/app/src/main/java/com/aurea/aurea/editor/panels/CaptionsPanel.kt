package com.aurea.aurea.editor.panels

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.captions.CaptionSettings
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel

// O índice é o `style` salvo nas opções da legenda; o rótulo sai do idioma do app.
private val CaptionStyles = listOf(
    R.string.pn_caption_style_classic, R.string.panel_caixa, R.string.pn_caption_style_highlight,
    R.string.pn_caption_style_neon, R.string.pn_karaoke, R.string.pn_pop,
)
// Nome do idioma da fala no próprio idioma (endônimo); nulo = "Automático", traduzido.
private val Languages = listOf<Pair<String?, String?>>(null to null, "pt" to "Português", "en" to "English", "es" to "Español")

/**
 * LEGENDAS da camada de vídeo/áudio: gerar (Groq, com a chave dos Ajustes) ou
 * importar SRT; estilo, agrupamento, destaque; a transcrição palavra a palavra
 * para corrigir; criar/refazer/remover as camadas (desfazer volta tudo).
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun CaptionsPanel(env: PanelEnv) {
    val store = env.store
    val cap = store.captions
    val layerId = store.primary ?: return
    LaunchedEffect(layerId) { cap.open(layerId) }
    var language by remember { mutableStateOf<String?>(null) }
    var editing by remember { mutableStateOf<Int?>(null) }
    val srt = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri -> uri?.let { cap.importSrt(it) } }
    val s = cap.settings
    fun set(f: (CaptionSettings) -> CaptionSettings) { cap.settings = f(cap.settings) }

    // Fase 8D: LazyColumn. A transcrição de um vídeo longo tem 2000–5000
    // palavras; num FlowRow dentro de um Column rolável TODAS eram compostas e
    // medidas ao abrir o painel (e todas recompostas ao tocar numa). Agora a
    // fala vai em pedaços de [WORDS_PER_ITEM] palavras e só os pedaços na tela
    // existem; o editor da palavra aparece logo abaixo do pedaço dela.
    val words = cap.words
    val fillers = cap.fillers
    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(horizontal = 18.dp, vertical = 10.dp)) {
        item(key = "opcoes", contentType = "opcoes") {
            Column {
                cap.busy?.let { Note(it, AureaColors.Accent) }
                cap.error?.let { Note(it, AureaColors.Danger) }
                if (!cap.hasGroqKey) {
                    Note(stringResource(R.string.panel_sem_chave_servico_transcricao_use_arquivo), AureaColors.Muted)
                }
                Label(stringResource(R.string.panel_idioma_fala))
                Chips(Languages.map { it.second ?: stringResource(R.string.pn_caption_lang_auto) }, Languages.indexOfFirst { it.first == language }) { language = Languages[it].first }
                Row(Modifier.fillMaxWidth().padding(vertical = 6.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Action(if (cap.words.isEmpty()) stringResource(R.string.panel_gerar_legendas) else stringResource(R.string.panel_transcrever_novo), primary = true, enabled = cap.hasGroqKey && cap.busy == null) {
                        cap.transcribe(language)
                    }
                    Action(stringResource(R.string.panel_importar_legenda_srt), enabled = cap.busy == null) { srt.launch(arrayOf("application/x-subrip", "text/*", "application/octet-stream")) }
                }

                Label(stringResource(R.string.panel_estilo))
                Chips(CaptionStyles.map { stringResource(it) }, s.style) { i -> set { it.copy(style = i) } }
                Label(stringResource(R.string.pn_caption))
                Chips(listOf(stringResource(R.string.pn_caption_grouped), stringResource(R.string.pn_caption_one_per_word)), s.mode) { i -> set { it.copy(mode = i) } }
                if (s.mode == 0) {
                    Label(stringResource(R.string.pn_caption_words_per_caption))
                    Chips((1..6).map { "$it" }, s.maxWords - 1) { i -> set { it.copy(maxWords = i + 1) } }
                    Label(stringResource(R.string.pn_caption_chars_per_line))
                    Chips(listOf("12", "18", "24", "32"), listOf(12, 18, 24, 32).indexOf(s.maxChars)) { i -> set { it.copy(maxChars = listOf(12, 18, 24, 32)[i]) } }
                    Label(stringResource(R.string.pn_caption_lines))
                    Chips(listOf("1", "2", "3"), s.maxLines - 1) { i -> set { it.copy(maxLines = i + 1) } }
                }
                Label(stringResource(R.string.pn_caption_screen_height))
                val ys = listOf(0.2f, 0.5f, 0.78f)
                Chips(listOf(stringResource(R.string.pn_caption_pos_top), stringResource(R.string.pn_caption_pos_middle), stringResource(R.string.pn_caption_pos_bottom)), ys.indexOfFirst { kotlin.math.abs(it - s.posY) < 0.01f }) { i -> set { it.copy(posY = ys[i]) } }
                Label(stringResource(R.string.panel_tamanho))
                val sizes = listOf(0.045f, 0.065f, 0.09f)
                Chips(listOf(stringResource(R.string.pn_size_small_short), stringResource(R.string.pn_size_medium_short), stringResource(R.string.pn_size_large_short)), sizes.indexOfFirst { kotlin.math.abs(it - s.sizeFrac) < 0.001f }) { i -> set { it.copy(sizeFrac = sizes[i]) } }
                Toggle(stringResource(R.string.pn_caption_highlight_word), s.highlight) { v -> set { it.copy(highlight = v) } }
                Toggle(stringResource(R.string.pn_caption_uppercase), s.uppercase) { v -> set { it.copy(uppercase = v) } }
                Toggle(stringResource(R.string.pn_caption_break_pauses), s.breakOnPause) { v -> set { it.copy(breakOnPause = v) } }
                Toggle(stringResource(R.string.pn_caption_remove_fillers), s.removeFillers) { v -> set { it.copy(removeFillers = v) } }

                Row(Modifier.fillMaxWidth().padding(vertical = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Action(if (cap.captionCount > 0) stringResource(R.string.pn_caption_redo) else stringResource(R.string.pn_caption_create), primary = true, enabled = words.isNotEmpty() && cap.busy == null) { cap.generate() }
                    if (cap.captionCount > 0) Action(stringResource(R.string.pn_caption_remove_n, cap.captionCount), enabled = cap.busy == null) { cap.removeAll() }
                }
                if (words.isNotEmpty()) {
                    val src = cap.source
                    Label(
                        if (src != null) pluralStringResource(R.plurals.pn_caption_transcript_source, words.size, src, words.size)
                        else pluralStringResource(R.plurals.pn_caption_transcript, words.size, words.size),
                    )
                }
            }
        }

        if (words.isNotEmpty()) {
            val chunks = (words.size + WORDS_PER_ITEM - 1) / WORDS_PER_ITEM
            items(chunks, contentType = { "fala" }) { c ->
                val from = c * WORDS_PER_ITEM
                val to = minOf(words.size, from + WORDS_PER_ITEM)
                FlowRow(
                    Modifier.padding(bottom = 6.dp),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    for (i in from until to) {
                        val struck = s.removeFillers && i in fillers
                        WordChip(words[i].text, selected = editing == i, struck = struck) { editing = i }
                    }
                }
                val e = editing
                if (e != null && e in from until to) {
                    words.getOrNull(e)?.let { w ->
                        WordEditor(w.text, "${com.aurea.aurea.ui.ds.numeroPtBr(w.start.toFloat(), 2)} s", onDone = { t -> cap.editWord(e, t); editing = null })
                        Spacer(Modifier.height(8.dp))
                    }
                }
            }
        }
        item(key = "fim") { Spacer(Modifier.height(24.dp)) }
    }
}

/** Palavras por item da lista preguiçosa (um parágrafo curto). */
private const val WORDS_PER_ITEM = 24

private val WordStyle = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Text))
private val WordStruckStyle = AureaType.Base.merge(
    TextStyle(fontSize = 13.sp, color = AureaColors.Muted, textDecoration = TextDecoration.LineThrough),
)

@Composable
private fun WordChip(text: String, selected: Boolean, struck: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.clip(RoundedCornerShape(6.dp)).background(if (selected) AureaColors.AccentDim else AureaColors.Chip)
            .tocavel(onClick = onClick).padding(horizontal = 8.dp, vertical = 4.dp),
    ) {
        Text(text, style = if (struck) WordStruckStyle else WordStyle)
    }
}

@Composable
private fun WordEditor(initial: String, time: String, onDone: (String) -> Unit) {
    var text by remember(initial) { mutableStateOf(initial) }
    Spacer(Modifier.height(10.dp))
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(time, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
        Box(Modifier.weight(1f).clip(RoundedCornerShape(8.dp)).background(AureaColors.Chip).padding(10.dp)) {
            BasicTextField(text, onValueChange = { text = it }, singleLine = true, cursorBrush = SolidColor(AureaColors.Accent),
                textStyle = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text)), modifier = Modifier.fillMaxWidth())
        }
        Action(stringResource(R.string.pn_ok), primary = true) { onDone(text) }
        Action(stringResource(R.string.pn_caption_delete_word)) { onDone("") }
    }
}

@Composable
private fun Note(text: String, color: androidx.compose.ui.graphics.Color) {
    Text(text, modifier = Modifier.padding(vertical = 6.dp), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = color)))
}

@Composable
private fun Label(text: String) {
    Spacer(Modifier.height(8.dp))
    Text(text, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
}

@Composable
private fun Chips(options: List<String>, selected: Int, onPick: (Int) -> Unit) {
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).height(44.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        options.forEachIndexed { i, o ->
            val on = i == selected
            Box(
                Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                    .tocavel(onClick = { onPick(i) }).padding(horizontal = 10.dp, vertical = 6.dp),
            ) {
                Text(o, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
            }
        }
    }
}

@Composable
private fun Toggle(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        AureaToggle(checked = checked, onCheckedChange = onChange)
    }
}

@Composable
private fun Action(label: String, primary: Boolean = false, enabled: Boolean = true, onClick: () -> Unit) {
    Box(
        Modifier.clip(RoundedCornerShape(10.dp))
            .background(if (!enabled) AureaColors.Chip.copy(alpha = 0.4f) else if (primary) AureaColors.AccentDim else AureaColors.Chip)
            .tocavel(onClick = { if (enabled) onClick() }).padding(horizontal = 14.dp, vertical = 10.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600,
            color = if (!enabled) AureaColors.Muted else if (primary) AureaColors.Accent else AureaColors.Text)))
    }
}
