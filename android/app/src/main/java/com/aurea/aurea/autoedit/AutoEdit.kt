package com.aurea.aurea.autoedit

import android.app.Application
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.captions.WhisperModels
import com.aurea.aurea.captions.WhisperQuality
import com.aurea.aurea.captions.Word
import com.aurea.aurea.editor.MenuSection
import com.aurea.aurea.editor.ShellMenuSheet
import com.aurea.aurea.editor.SoundLibrary
import com.aurea.aurea.effects.HyperEffectGate
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.i18n.AppText
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import java.util.Locale

// =============================================================================
//  1.0.8 Edição automática. A tela só escolhe as opções, transcreve (Whisper
//  local, como as legendas) e manda tudo ao motor: as regras (cortes, zoom,
//  sons, música, voz, cor, legendas) moram em engine/src/autoedit e a montagem
//  em Engine::auto_edit — o par do iOS é AutoEditView.swift.
// =============================================================================

/** As opções da folha = o bloco "options" do pedido ao motor. */
data class AutoEditOptions(
    val intensity: Int = 1,       // 0 leve, 1 média, 2 forte
    val recording: Int = 0,       // 0 com retomadas, 1 contínua (entrevista)
    val language: String = "pt",
    val cuts: Boolean = true,
    val zoom: Boolean = true,
    val sounds: Boolean = true,
    val music: Boolean = true,
    val voice: Boolean = true,
    val color: Boolean = true,
    val captions: Boolean = true,
) {
    fun toJson(): JSONObject = JSONObject()
        .put("intensity", intensity).put("recording", recording).put("language", language)
        .put("cuts", cuts).put("zoom", zoom).put("sounds", sounds).put("music", music)
        .put("voice", voice).put("color", color).put("captions", captions)
}

/** Etapas mostradas (−1 = transcrevendo; 0..10 = as do motor, Engine::autoEditProgress). */
val AutoEditSteps = intArrayOf(
    R.string.autoedit_step_prepare, R.string.autoedit_step_audio, R.string.autoedit_step_cuts, R.string.autoedit_step_faces,
    R.string.autoedit_step_zoom, R.string.autoedit_step_sounds, R.string.autoedit_step_music, R.string.autoedit_step_voice,
    R.string.autoedit_step_color, R.string.autoedit_step_build, R.string.autoedit_step_build,
)

/** "aurea.light.light_leak" → id do tipo (FNV-1a 32, o mesmo de effect_type_id no motor). */
internal fun effectTypeId(key: String): Int {
    var h = 0x811C9DC5.toInt()
    for (b in key.toByteArray(Charsets.UTF_8)) { h = h xor (b.toInt() and 0xff); h *= 16777619 }
    return h
}

class AutoEditState(
    private val app: Application,
    private val engine: AureaEngine,
    private val scope: CoroutineScope,
    private val captionOptions: () -> String,
    private val whisperQuality: () -> WhisperQuality,
    private val onDone: (Long) -> Unit,
) {
    /** Camada de vídeo com a folha aberta (nulo = fechada). */
    var target by mutableStateOf<Long?>(null)
        private set
    var options by mutableStateOf(AutoEditOptions(language = defaultLanguage()))
    var running by mutableStateOf(false)
        private set
    /** −1 transcrevendo, 0..10 etapas do motor, nulo parado. */
    var step by mutableStateOf<Int?>(null)
        private set
    var percent by mutableIntStateOf(0)
        private set
    var error by mutableStateOf<String?>(null)
        private set
    /** Relatório do motor (JSON) da última edição que deu certo. */
    var report by mutableStateOf<JSONObject?>(null)
        private set
    private var job: Job? = null
    /** Home › Edição automática: abre a folha no 1º vídeo do projeto desta geração. */
    private var armedGeneration = -1L

    private fun defaultLanguage(): String = when (Locale.getDefault().language) { "en" -> "en"; "es" -> "es"; else -> "pt" }

    fun open(layer: Long) {
        if (running) return
        target = layer; error = null; report = null; step = null
    }

    fun close() {
        if (running) cancel()
        target = null
    }

    fun armForProject(generation: Long) { armedGeneration = generation }

    /** Chamado quando as camadas mudam: o vídeo importado pela Home abre a folha. */
    fun onLayers(generation: Long, firstVideo: Long?) {
        if (armedGeneration < 0) return
        if (generation > armedGeneration) { armedGeneration = -1; return }
        if (generation == armedGeneration && firstVideo != null) { armedGeneration = -1; open(firstVideo) }
    }

    fun cancel() {
        job?.cancel()
        engine.captionProgress(true)
        engine.autoEditProgress(true)
    }

    /**
     * Começa. [injectedWords] pula a transcrição (testes de aparelho: a fala
     * vem pronta, em segundos da mídia).
     */
    fun start(injectedWords: List<Word>? = null) {
        val layer = target ?: return
        if (running) return
        running = true; error = null; report = null; percent = 0
        step = if (injectedWords == null) -1 else 0
        val opts = options
        val captionJson = captionOptions()
        // Luz (light leak) é paga: só entra com o acesso (o mesmo portão do navegador de efeitos).
        val lightLeak = !HyperEffectGate.locked(effectTypeId("aurea.light.light_leak"))
        job = scope.launch {
            try {
                val words = injectedWords ?: withContext(Dispatchers.IO) { transcribe(layer, opts.language) }
                step = 0
                val request = withContext(Dispatchers.IO) { buildRequest(opts, words, captionJson, lightLeak) }
                val ticker = launch {
                    while (isActive) {
                        delay(250)
                        val p = engine.autoEditProgress()
                        if (p in 0 until 1000) { step = (p / 100).coerceIn(0, 10); percent = p % 100 }
                    }
                }
                val raw = try { withContext(Dispatchers.Default) { engine.autoEdit(layer, request) } } finally { ticker.cancel() }
                val result = runCatching { JSONObject(raw) }.getOrNull()
                if (result != null && result.optBoolean("ok")) {
                    report = result
                    onDone(result.optLong("layer"))
                } else {
                    error = message(result?.optString("error").orEmpty(), result?.optString("message").orEmpty())
                }
            } catch (cancelled: CancellationException) {
                error = AppText.get(app, R.string.autoedit_cancelled)
                throw cancelled
            } catch (failure: Exception) {
                error = (failure as? WhisperModels.Failure)?.message ?: AppText.get(app, R.string.autoedit_failed, failure.message.orEmpty())
            } finally {
                running = false
                step = null
            }
        }
    }

    private fun message(code: String, detail: String): String = when (code) {
        "speed" -> AppText.get(app, R.string.autoedit_error_speed)
        "short" -> AppText.get(app, R.string.autoedit_error_short)
        "layer" -> AppText.get(app, R.string.autoedit_need_video)
        "cancelled" -> AppText.get(app, R.string.autoedit_cancelled)
        else -> AppText.get(app, R.string.autoedit_failed, detail.ifEmpty { code })
    }

    /** Whisper local (o mesmo modelo das legendas): nenhum áudio sai do aparelho. */
    private suspend fun transcribe(layer: Long, language: String): List<Word> {
        val quality = WhisperModels.usable(app, whisperQuality())
        val model = WhisperModels.prepare(app, quality) { }
        val ticker = scope.launch {
            while (isActive) { delay(300); percent = engine.captionProgress().coerceIn(0, 99) }
        }
        try {
            return engine.transcribeLocal(layer, model.absolutePath, language).lineSequence().mapNotNull { line ->
                val f = line.split('\t', limit = 3)
                if (f.size != 3) null else Word(f[2], f[0].toDoubleOrNull() ?: return@mapNotNull null, f[1].toDoubleOrNull() ?: return@mapNotNull null)
            }.toList()
        } finally { ticker.cancel() }
    }

    private fun buildRequest(opts: AutoEditOptions, words: List<Word>, captionJson: String, lightLeak: Boolean): String {
        val list = JSONArray()
        words.forEach { list.put(JSONObject().put("text", it.text).put("start", it.start).put("end", it.end)) }
        // A biblioteca de sons do usuário (pastas = categorias); o motor escolhe
        // os sons por papel e a música, e sintetiza os papéis que faltarem.
        val library = JSONArray()
        runCatching { SoundLibrary.load(app) }.getOrDefault(emptyList()).forEach { folder ->
            folder.sounds.forEach { e ->
                library.put(JSONObject().put("path", e.source).put("name", e.title).put("category", folder.name)
                    .put("duration", e.durationMs / 1000.0))
            }
        }
        return JSONObject()
            .put("options", opts.toJson())
            .put("words", list)
            .put("library", library)
            .put("captionOptions", captionJson)
            .put("lightLeak", lightLeak)
            .toString()
    }
}

// -----------------------------------------------------------------------------
// A folha
// -----------------------------------------------------------------------------

@Composable
internal fun AutoEditSheet(store: EditorStore) {
    val state = store.autoEdit
    // Fechar a folha (gesto ou botão) cancela uma edição em andamento — igual no iOS.
    ShellMenuSheet(onDismiss = { state.close() }, maxHeightFraction = 0.88f) {
        Column(Modifier.fillMaxWidth().testTag("autoedit.sheet")) {
            Text(
                stringResource(R.string.autoedit_title),
                modifier = Modifier.padding(start = 20.dp, end = 20.dp, top = 6.dp).semantics { heading() },
                style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)),
            )
            Text(
                stringResource(R.string.autoedit_subtitle),
                modifier = Modifier.padding(horizontal = 20.dp, vertical = 4.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)),
            )
            val o = state.options
            val enabled = !state.running
            MenuSection(stringResource(R.string.autoedit_intensity))
            Segmented(
                listOf(R.string.autoedit_light, R.string.autoedit_medium, R.string.autoedit_strong), o.intensity, "autoedit.intensity", enabled,
            ) { state.options = o.copy(intensity = it) }
            MenuSection(stringResource(R.string.autoedit_recording))
            Segmented(listOf(R.string.autoedit_retakes, R.string.autoedit_continuous), o.recording, "autoedit.recording", enabled) {
                state.options = o.copy(recording = it)
            }
            MenuSection(stringResource(R.string.autoedit_language))
            val languages = listOf("pt", "en", "es")
            Segmented(listOf(R.string.autoedit_lang_pt, R.string.autoedit_lang_en, R.string.autoedit_lang_es),
                languages.indexOf(o.language).coerceAtLeast(0), "autoedit.language", enabled) { state.options = o.copy(language = languages[it]) }
            MenuSection(stringResource(R.string.autoedit_steps))
            StepToggle(R.string.autoedit_cuts, "cuts", o.cuts, enabled) { state.options = o.copy(cuts = it) }
            StepToggle(R.string.autoedit_zoom, "zoom", o.zoom, enabled) { state.options = o.copy(zoom = it) }
            StepToggle(R.string.autoedit_sounds, "sounds", o.sounds, enabled) { state.options = o.copy(sounds = it) }
            StepToggle(R.string.autoedit_music, "music", o.music, enabled) { state.options = o.copy(music = it) }
            StepToggle(R.string.autoedit_voice, "voice", o.voice, enabled) { state.options = o.copy(voice = it) }
            StepToggle(R.string.autoedit_color, "color", o.color, enabled) { state.options = o.copy(color = it) }
            StepToggle(R.string.autoedit_captions, "captions", o.captions, enabled) { state.options = o.copy(captions = it) }
            Spacer(Modifier.height(10.dp))
            state.step?.let { step ->
                val label = if (step < 0) stringResource(R.string.autoedit_step_transcribe) else stringResource(AutoEditSteps[step.coerceIn(0, 10)])
                Column(Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 6.dp).testTag("autoedit.progress")
                    .semantics(mergeDescendants = true) { stateDescription = "$label ${state.percent}%" }) {
                    Text("$label · ${state.percent}%", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Text)))
                    Spacer(Modifier.height(6.dp))
                    LinearProgressIndicator(progress = { state.percent / 100f }, modifier = Modifier.fillMaxWidth(), color = AureaColors.Accent)
                }
            }
            state.error?.let {
                Text(it, modifier = Modifier.padding(horizontal = 20.dp, vertical = 6.dp).testTag("autoedit.error"),
                    style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Warning)))
            }
            state.report?.let { r ->
                Text(
                    stringResource(R.string.autoedit_done, seconds(r.optDouble("sourceSeconds")), seconds(r.optDouble("editSeconds"))),
                    modifier = Modifier.padding(horizontal = 20.dp, vertical = 6.dp).testTag("autoedit.result"),
                    style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Text)),
                )
            }
            Row(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                if (state.running) {
                    ActionButton(R.string.autoedit_cancel, "autoedit.cancel", primary = false, Modifier.weight(1f)) { state.cancel() }
                } else {
                    ActionButton(R.string.autoedit_close, "autoedit.close", primary = false, Modifier.weight(1f)) { state.close() }
                    ActionButton(if (state.report != null) R.string.autoedit_again else R.string.autoedit_start, "autoedit.start", primary = true, Modifier.weight(1f)) {
                        state.start()
                    }
                }
            }
        }
    }
}

private fun seconds(s: Double): String = if (s.isNaN()) "—" else String.format(Locale.ROOT, "%d:%02d", (s / 60).toInt(), (s % 60).toInt())

@Composable
private fun Segmented(labels: List<Int>, selected: Int, tag: String, enabled: Boolean, onSelect: (Int) -> Unit) {
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp).height(36.dp)
            .clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip),
    ) {
        labels.forEachIndexed { index, res ->
            val on = index == selected
            val text = stringResource(res)
            Box(
                Modifier.weight(1f).height(36.dp).padding(3.dp).clip(RoundedCornerShape(8.dp))
                    .background(if (on) AureaColors.Accent else Color.Transparent)
                    .testTag("$tag.$index")
                    .semantics { this.selected = on; role = Role.Tab; contentDescription = text }
                    .tocavel(enabled = enabled, shrink = 1f) { onSelect(index) },
                contentAlignment = Alignment.Center,
            ) {
                Text(text, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600,
                    color = if (on) AureaColors.OnAccent else AureaColors.Text)))
            }
        }
    }
}

@Composable
private fun StepToggle(label: Int, key: String, checked: Boolean, enabled: Boolean, onChange: (Boolean) -> Unit) {
    val text = stringResource(label)
    Row(
        Modifier.fillMaxWidth().heightIn(min = 44.dp).padding(horizontal = 20.dp).testTag("autoedit.toggle.$key")
            .semantics(mergeDescendants = true) { contentDescription = text; stateDescription = if (checked) "on" else "off" },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(text, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Text)))
        AureaToggle(checked, onChange, enabled = enabled)
    }
}

@Composable
private fun ActionButton(label: Int, tag: String, primary: Boolean, modifier: Modifier, onClick: () -> Unit) {
    val text = stringResource(label)
    Box(
        modifier.height(46.dp).clip(RoundedCornerShape(12.dp))
            .background(if (primary) AureaColors.Accent else AureaColors.Chip)
            .testTag(tag)
            .semantics { role = Role.Button; contentDescription = text }
            .tocavel(shrink = 1f, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(text, style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600,
            color = if (primary) AureaColors.OnAccent else AureaColors.Text)))
    }
}
