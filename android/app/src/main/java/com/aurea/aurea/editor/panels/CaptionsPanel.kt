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
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.verticalScroll
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

private val CaptionStyles = listOf("Clássico", "Caixa", "Destaque", "Neon", "Karaokê", "Pop")
private val Languages = listOf(null to "Automático", "pt" to "Português", "en" to "English", "es" to "Español")

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

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 18.dp, vertical = 10.dp)) {
        cap.busy?.let { Note(it, AureaColors.Accent) }
        cap.error?.let { Note(it, AureaColors.Danger) }
        if (!cap.hasGroqKey) {
            Note("Sem chave da Groq: gere com um arquivo SRT, ou coloque a chave em Ajustes › Legendas. O áudio só é enviado quando você toca em Gerar legendas.", AureaColors.Muted)
        }
        Label("Idioma da fala")
        Chips(Languages.map { it.second }, Languages.indexOfFirst { it.first == language }) { language = Languages[it].first }
        Row(Modifier.fillMaxWidth().padding(vertical = 6.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Action(if (cap.words.isEmpty()) "Gerar legendas" else "Transcrever de novo", primary = true, enabled = cap.hasGroqKey && cap.busy == null) {
                cap.transcribe(language)
            }
            Action("Importar SRT", enabled = cap.busy == null) { srt.launch(arrayOf("application/x-subrip", "text/*", "application/octet-stream")) }
        }

        Label("Estilo")
        Chips(CaptionStyles, s.style) { i -> set { it.copy(style = i) } }
        Label("Legenda")
        Chips(listOf("Agrupadas", "Uma por palavra"), s.mode) { i -> set { it.copy(mode = i) } }
        if (s.mode == 0) {
            Label("Palavras por legenda")
            Chips((1..6).map { "$it" }, s.maxWords - 1) { i -> set { it.copy(maxWords = i + 1) } }
            Label("Caracteres por linha")
            Chips(listOf("12", "18", "24", "32"), listOf(12, 18, 24, 32).indexOf(s.maxChars)) { i -> set { it.copy(maxChars = listOf(12, 18, 24, 32)[i]) } }
            Label("Linhas")
            Chips(listOf("1", "2", "3"), s.maxLines - 1) { i -> set { it.copy(maxLines = i + 1) } }
        }
        Label("Posição (área segura)")
        val ys = listOf(0.2f, 0.5f, 0.78f)
        Chips(listOf("Alto", "Meio", "Baixo"), ys.indexOfFirst { kotlin.math.abs(it - s.posY) < 0.01f }) { i -> set { it.copy(posY = ys[i]) } }
        Label("Tamanho")
        val sizes = listOf(0.045f, 0.065f, 0.09f)
        Chips(listOf("P", "M", "G"), sizes.indexOfFirst { kotlin.math.abs(it - s.sizeFrac) < 0.001f }) { i -> set { it.copy(sizeFrac = sizes[i]) } }
        Toggle("Destacar a palavra falada", s.highlight) { v -> set { it.copy(highlight = v) } }
        Toggle("MAIÚSCULAS", s.uppercase) { v -> set { it.copy(uppercase = v) } }
        Toggle("Quebrar nas pausas", s.breakOnPause) { v -> set { it.copy(breakOnPause = v) } }
        Toggle("Tirar vícios (hum, ahn, tipo…)", s.removeFillers) { v -> set { it.copy(removeFillers = v) } }

        Row(Modifier.fillMaxWidth().padding(vertical = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Action(if (cap.captionCount > 0) "Refazer legendas" else "Criar legendas", primary = true, enabled = cap.words.isNotEmpty()) { cap.generate() }
            if (cap.captionCount > 0) Action("Remover (${cap.captionCount})") { cap.removeAll() }
        }

        if (cap.words.isNotEmpty()) {
            Label("Transcrição${cap.source?.let { " · $it" } ?: ""} — toque para corrigir")
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                cap.words.forEachIndexed { i, w ->
                    val filler = i in cap.fillers
                    Box(
                        Modifier.clip(RoundedCornerShape(6.dp)).background(if (editing == i) AureaColors.AccentDim else AureaColors.Chip)
                            .tocavel(onClick = { editing = i }).padding(horizontal = 8.dp, vertical = 4.dp),
                    ) {
                        Text(
                            w.text,
                            style = AureaType.Base.merge(
                                TextStyle(
                                    fontSize = 13.sp,
                                    color = if (filler && s.removeFillers) AureaColors.Muted else AureaColors.Text,
                                    textDecoration = if (filler && s.removeFillers) TextDecoration.LineThrough else null,
                                ),
                            ),
                        )
                    }
                }
            }
            editing?.let { i -> cap.words.getOrNull(i)?.let { w -> WordEditor(w.text, "%.2f s".format(w.start), onDone = { t -> cap.editWord(i, t); editing = null }) } }
        }
        Spacer(Modifier.height(24.dp))
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
        Action("OK", primary = true) { onDone(text) }
        Action("Tirar") { onDone("") }
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
