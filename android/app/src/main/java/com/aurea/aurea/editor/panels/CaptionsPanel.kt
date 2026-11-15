package com.aurea.aurea.editor.panels

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
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
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.captions.CaptionSettings
import com.aurea.aurea.captions.CaptionsState
import com.aurea.aurea.captions.SpeechLanguages
import com.aurea.aurea.captions.WhisperQuality
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.tocavel

/**
 * LEGENDAS (0.5.3, no jeito do Premiere, com a cara do AUREA): quatro abas de
 * ícone na moldura comum — um assunto por aba (FUNCTIONS_IA.md §3.1).
 *
 *  - **Criar**: transcrever (nuvem e depois o modelo do aparelho) e a janela de
 *    opções de criação (modelo, linhas, caracteres, duração mínima, intervalo…);
 *  - **Transcrição**: a fala em parágrafos, corrigir palavra, buscar/substituir,
 *    criar legendas de um trecho;
 *  - **Legendas**: a lista de blocos (entrada–saída e texto), o centro do
 *    trabalho: editar, ‹ › de quadro, dividir, unir, inserir, apagar, exportar;
 *  - **Estilo**: faixa toda ou só as escolhidas — modelos, aparência, texto e
 *    caixa, posição.
 *
 * As regras de edição moram no motor (`edit_caption_track`); a tela só manda.
 * Desenho e comportamento: build/reference/am-ui-20261113/CAPTIONS_PREMIERE.md.
 */
@Composable
internal fun CaptionsPanel(env: PanelEnv) {
    val store = env.store
    val cap = store.captions
    val sourceLayers = store.layers.filter { it.kind == 1 || it.kind == 3 }
    val selectedCaption = remember(store.primary, store.curveRevision) { cap.isCaption(store.primary) }
    if (store.primary == null || (store.detail?.kind != 1 && store.detail?.kind != 3 && !selectedCaption)) {
        LazyColumn(Modifier.fillMaxSize().padding(18.dp)) {
            item { Text(stringResource(R.string.sh_add_captions_need_speech)) }
            sourceLayers.forEach { layer -> item { CapAction(layer.name) { store.select(layer.id) } } }
        }
        return
    }
    val layerId = store.primary ?: return
    LaunchedEffect(layerId, store.curveRevision) { cap.open(layerId) }
    // Com a faixa pronta, o painel abre na lista (o centro do trabalho); sem ela, em Criar.
    var tab by androidx.compose.runtime.saveable.rememberSaveable(layerId) {
        androidx.compose.runtime.mutableIntStateOf(if (selectedCaption) 2 else 0)
    }
    Row(Modifier.fillMaxSize()) {
        Box(Modifier.weight(1f).fillMaxHeight()) {
            when (tab) {
                0 -> CaptionCreateTab(cap)
                1 -> CaptionTranscriptTab(cap, store)
                2 -> CaptionListTab(cap, store, onOwnStyle = { tab = 3 })
                else -> CaptionStyleTab(cap, store)
            }
        }
        Box(Modifier.width(0.7.dp).fillMaxHeight().background(AureaColors.ContentStrongLine))
        PanelIconTabs(listOf(
            PanelIconTab(CupertinoGlyph.Waveform, stringResource(R.string.fn_cap_tab_create), "captions.tab.create"),
            PanelIconTab(CupertinoGlyph.DocText, stringResource(R.string.fn_cap_tab_transcript), "captions.tab.transcript"),
            PanelIconTab(CupertinoGlyph.CaptionsBubble, stringResource(R.string.fn_cap_tab_list), "captions.tab.list"),
            PanelIconTab(CupertinoGlyph.Paintbrush, stringResource(R.string.tx_tab_style), "captions.tab.style"),
        ), tab, stringResource(R.string.panel_legendas)) { tab = it }
    }
}

/** Estado do trabalho (andamento, erro, aviso, cancelar): o mesmo em todas as abas. */
@Composable
internal fun CaptionJobStatus(cap: CaptionsState) {
    Column {
        cap.busy?.let {
            CapNote(it, AureaColors.Accent)
            CapAction(stringResource(R.string.common_cancel)) { cap.cancelTranscription() }
        }
        cap.error?.let { CapNote(it, AureaColors.Danger) }
        cap.notice?.let { CapNote(it, AureaColors.Muted) }
    }
}

/**
 * CRIAR: transcrever e a janela "Criar legendas" (as opções valem ao criar ou
 * refazer; estilo e posição mudam na hora pela aba Estilo).
 */
@Composable
private fun CaptionCreateTab(cap: CaptionsState) {
    val s = cap.settings
    val idle = cap.busy == null
    var languages by remember { mutableStateOf(false) }
    var translateEnglish by remember { mutableStateOf(false) }
    val srt = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri -> uri?.let { cap.importSrt(it) } }
    fun set(f: (CaptionSettings) -> CaptionSettings) = cap.update(custom = false, f = f)
    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(horizontal = 18.dp, vertical = 10.dp)) {
        item(key = "status") { CaptionJobStatus(cap) }
        item(key = "transcrever") {
            Column {
                CapSection(stringResource(R.string.cap053_transcribe_title))
                val auto = stringResource(R.string.cap025_lang_auto)
                val langName = cap.speechLanguage?.let(SpeechLanguages::endonym) ?: auto
                Row(Modifier.fillMaxWidth().padding(top = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.panel_idioma_fala), Modifier.weight(1f), style = AureaType.BodySmall)
                    CapChip("$langName  ▾", on = languages, enabled = idle, description = stringResource(R.string.panel_idioma_fala)) { languages = !languages }
                }
                if (languages) LanguageChips(cap) { languages = false }
                CapToggle(stringResource(R.string.beta_translate_english), translateEnglish) { translateEnglish = it }
                Row(Modifier.fillMaxWidth().padding(vertical = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    CapAction(
                        if (cap.words.isNotEmpty()) stringResource(R.string.panel_transcrever_novo) else stringResource(R.string.panel_gerar_legendas),
                        primary = cap.words.isEmpty(), enabled = idle,
                    ) { cap.transcribe(cap.speechLanguage, translateEnglish = translateEnglish) }
                    CapAction(stringResource(R.string.cap053_import), enabled = idle) {
                        srt.launch(arrayOf("application/x-subrip", "text/vtt", "text/*", "application/octet-stream"))
                    }
                }
                CapLabel(stringResource(R.string.cap041_offline_model))
                SpeechControls(cap)
            }
        }
        item(key = "criar") {
            Column {
                CapSection(stringResource(R.string.cap053_create_title))
                CaptionTemplateRow(cap)
                CapLabel(stringResource(R.string.pn_caption))
                CapChips(listOf(stringResource(R.string.pn_caption_grouped), stringResource(R.string.pn_caption_one_per_word)), s.mode) { i -> set { it.copy(mode = i) } }
                if (s.mode == 0) {
                    CapLabel(stringResource(R.string.pn_caption_lines))
                    CapChips(listOf("1", "2"), s.maxLines - 1) { i -> set { it.copy(maxLines = i + 1) } }
                    CapLabel(stringResource(R.string.pn_caption_chars_per_line))
                    val chars = listOf(12, 18, 24, 32, 42)
                    CapChips(chars.map { "$it" }, chars.indexOf(s.maxChars)) { i -> set { it.copy(maxChars = chars[i]) } }
                    CapLabel(stringResource(R.string.pn_caption_words_per_caption))
                    CapChips((1..8).map { "$it" }, s.maxWords - 1) { i -> set { it.copy(maxWords = i + 1) } }
                }
                CapLabel(stringResource(R.string.cap053_min_duration))
                val mins = listOf(0.5f, 0.8f, 1.2f, 2f)
                CapChips(mins.map { stringResource(R.string.cap053_seconds, com.aurea.aurea.ui.ds.numeroPtBr(it, 1)) },
                    mins.indexOfFirst { kotlin.math.abs(it - s.minDurationSec) < 0.01f }) { i -> set { it.copy(minDurationSec = mins[i]) } }
                CapLabel(stringResource(R.string.cap053_gap))
                val gaps = listOf(0, 2, 4, 8)
                CapChips(gaps.map { "$it" }, gaps.indexOf(s.gapFrames)) { i -> set { it.copy(gapFrames = gaps[i]) } }
                CapToggle(stringResource(R.string.cap053_break_sentence), s.breakOnSentence) { v -> set { it.copy(breakOnSentence = v) } }
                CapToggle(stringResource(R.string.pn_caption_break_pauses), s.breakOnPause) { v -> set { it.copy(breakOnPause = v) } }
                CapToggle(stringResource(R.string.pn_caption_uppercase), s.uppercase) { v -> set { it.copy(uppercase = v) } }
                CapToggle(stringResource(R.string.pn_caption_remove_fillers), s.removeFillers) { v -> set { it.copy(removeFillers = v) } }
                CapNote(stringResource(R.string.cap053_create_note), AureaColors.Muted)
                Row(Modifier.fillMaxWidth().padding(vertical = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    if (cap.captionCount == 0) {
                        CapAction(stringResource(R.string.pn_caption_create), primary = true, enabled = cap.words.isNotEmpty() && idle) { cap.generate() }
                    } else {
                        CapAction(stringResource(R.string.pn_caption_redo), primary = true, enabled = cap.words.isNotEmpty() && idle) { cap.generate() }
                        CapAction(stringResource(R.string.pn_caption_remove_n, cap.captionCount), enabled = idle) { cap.removeAll() }
                    }
                }
            }
        }
        item(key = "fim") { Spacer(Modifier.height(24.dp)) }
    }
}

/**
 * Idioma da fala (os sete do app mais os comuns; nome no próprio idioma).
 */
@Composable
private fun LanguageChips(cap: CaptionsState, onPicked: () -> Unit) {
    val context = LocalContext.current
    val idle = cap.busy == null
    val codes = listOf<String?>(null) + SpeechLanguages.codes
    val auto = stringResource(R.string.cap025_lang_auto)
    val spoken = remember(context) { SpeechLanguages.codes.associateWith { SpeechLanguages.spoken(context, it) } }
    CapWrapChips(
        options = codes.map { it?.let(SpeechLanguages::endonym) ?: auto },
        selected = codes.indexOf(cap.speechLanguage),
        descriptions = codes.map { code -> code?.let { spoken[it] } ?: auto },
        enabled = { idle },
    ) { cap.chooseLanguage(codes[it]); onPicked() }
}

/** O modelo do aparelho (usado sem internet ou sem conta): qualidade e download. */
@Composable
private fun SpeechControls(cap: CaptionsState) {
    val idle = cap.busy == null
    CapLabel(stringResource(R.string.cap025_quality))
    // Memória e arquivo baixado mudam entre uma transcrição e outra, não a cada quadro.
    val fits = remember(cap.busy) { WhisperQuality.entries.map { cap.qualityFits(it) } }
    val ready = remember(cap.busy) { WhisperQuality.entries.map { cap.qualityDownloaded(it) } }
    val unavailable = stringResource(R.string.cap025_quality_unavailable)
    val labels = WhisperQuality.entries.map { stringResource(it.label) }
    val hints = WhisperQuality.entries.map { stringResource(it.hint) }
    CapChips(
        options = labels,
        selected = cap.quality.ordinal,
        descriptions = WhisperQuality.entries.map { q ->
            labels[q.ordinal] + ". " + hints[q.ordinal] + (if (fits[q.ordinal]) "" else " " + unavailable)
        },
        enabled = { idle && fits[it] },
    ) { cap.chooseQuality(WhisperQuality.entries[it]) }
    val q = cap.quality
    CapNote(hints[q.ordinal], AureaColors.Muted)
    when {
        !fits[q.ordinal] -> CapNote(unavailable, AureaColors.Danger)
        ready[q.ordinal] -> CapNote(stringResource(R.string.cap025_quality_ready), AureaColors.Muted)
        else -> CapNote(stringResource(R.string.cap025_quality_needs_download, q.megabytes), AureaColors.Muted)
    }
}

// ---- peças comuns das abas de legenda (cara do AUREA) ----

@Composable
internal fun CapNote(text: String, color: androidx.compose.ui.graphics.Color) {
    Text(text, modifier = Modifier.padding(vertical = 6.dp), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = color)))
}

@Composable
internal fun CapLabel(text: String) {
    Spacer(Modifier.height(8.dp))
    Text(text, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
}

/** Título de seção da aba (um degrau acima de [CapLabel]). */
@Composable
internal fun CapSection(text: String) {
    Spacer(Modifier.height(10.dp))
    Text(text, Modifier.semantics { this.heading() },
        style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W700, color = AureaColors.Text)))
}

@Composable
internal fun CapChips(
    options: List<String>,
    selected: Int,
    descriptions: List<String>? = null,
    enabled: (Int) -> Boolean = { true },
    onPick: (Int) -> Unit,
) {
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).height(44.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        options.forEachIndexed { i, o -> CapChip(o, i == selected, enabled(i), descriptions?.getOrNull(i)) { onPick(i) } }
    }
}

/** Como [CapChips], mas quebra em linhas: a lista de idiomas inteira à vista. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun CapWrapChips(
    options: List<String>,
    selected: Int,
    descriptions: List<String>? = null,
    enabled: (Int) -> Boolean = { true },
    onPick: (Int) -> Unit,
) {
    FlowRow(
        Modifier.fillMaxWidth().padding(vertical = 6.dp),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        options.forEachIndexed { i, o -> CapChip(o, i == selected, enabled(i), descriptions?.getOrNull(i)) { onPick(i) } }
    }
}

/** Escolha única: o leitor de tela ouve o nome, "selecionado" e se está desligada. */
@Composable
internal fun CapChip(label: String, on: Boolean, enabled: Boolean = true, description: String? = null, onClick: () -> Unit) {
    Box(
        Modifier.clip(RoundedCornerShape(8.dp))
            .background(if (on) AureaColors.AccentDim else if (enabled) AureaColors.Chip else AureaColors.Chip.copy(alpha = 0.4f))
            .tocavel(enabled = enabled, role = Role.RadioButton, onClick = { if (enabled) onClick() })
            .semantics {
                selected = on
                if (description != null) contentDescription = description
                if (!enabled) disabled()
            }
            .padding(horizontal = 10.dp, vertical = 6.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp,
            color = if (on) AureaColors.Accent else if (enabled) AureaColors.Text else AureaColors.Muted)))
    }
}

@Composable
internal fun CapToggle(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        AureaToggle(checked = checked, onCheckedChange = onChange)
    }
}

@Composable
internal fun CapAction(label: String, primary: Boolean = false, enabled: Boolean = true, description: String? = null, onClick: () -> Unit) {
    Box(
        Modifier.clip(RoundedCornerShape(10.dp))
            .background(if (!enabled) AureaColors.Chip.copy(alpha = 0.4f) else if (primary) AureaColors.AccentDim else AureaColors.Chip)
            .tocavel(enabled = enabled, role = Role.Button, onClick = { if (enabled) onClick() })
            .semantics {
                if (description != null) contentDescription = description
                if (!enabled) disabled()
            }
            .padding(horizontal = 14.dp, vertical = 10.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600,
            color = if (!enabled) AureaColors.Muted else if (primary) AureaColors.Accent else AureaColors.Text)))
    }
}
