package com.aurea.aurea.captions

import android.app.Application
import android.net.Uri
import android.provider.OpenableColumns
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.aurea.aurea.R
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.ui.i18n.AppText
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Job
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.isActive
import com.aurea.aurea.state.readBounded
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import org.json.JSONArray
import org.json.JSONObject

/** Opções da geração (espelham `text::CaptionOptions`). */
data class CaptionSettings(
    val mode: Int = 0,
    val maxWords: Int = 4,
    val maxChars: Int = 18,
    val maxLines: Int = 2,
    val style: Int = 2,
    val highlight: Boolean = true,
    val uppercase: Boolean = false,
    val breakOnPause: Boolean = true,
    val removeFillers: Boolean = true,
    val pauseSec: Float = 0.6f,
    val posY: Float = 0.78f,
    val sizeFrac: Float = 0.065f,
    val highlightColor: FloatArray = floatArrayOf(1f, 0.83f, 0f),
    // 0.4.6: aparência própria por cima do estilo (bits `CaptionLook.*`).
    val look: Int = 0,
    val fontFamily: String = "",
    val fontWeight: Int = 700,
    val fontItalic: Boolean = false,
    val fontPath: String = "",
    val fill: FloatArray = floatArrayOf(1f, 1f, 1f, 1f),
    val stroke: FloatArray = floatArrayOf(0f, 0f, 0f, 1f),
    val strokeFrac: Float = 0.08f,
    val box: Boolean = false,
    val boxColor: FloatArray = floatArrayOf(0f, 0f, 0f, 0.7f),
    val shadow: Boolean = false,
    // 0.5.3 (Criar legendas, no jeito do Premiere) e a aparência nova; padrões = o de antes.
    val minDurationSec: Float = 0.8f,
    val gapFrames: Int = 0,
    val breakOnSentence: Boolean = false,
    val align: Int = 1,
    val lineSpacing: Float = 1.15f,
    val boxPadding: Float = 0.3f,
    val boxRadius: Float = 0.25f,
    val shadowColor: FloatArray = floatArrayOf(0f, 0f, 0f, 0.6f),
    val shadowBlur: Float = 0.12f,
) {
    fun has(bit: Int) = look and bit != 0
    fun with(bit: Int, on: Boolean) = copy(look = if (on) look or bit else look and bit.inv())

    /** O bloco "caption" do preset (presets::caption_options_json): a forma que o motor lê. */
    fun toJson(): String = JSONObject().apply {
        put("mode", mode); put("maxWords", maxWords); put("maxChars", maxChars); put("maxLines", maxLines)
        put("style", style); put("highlight", highlight); put("uppercase", uppercase); put("breakOnPause", breakOnPause)
        put("pauseSec", pauseSec.toDouble()); put("posY", posY.toDouble()); put("sizeFrac", sizeFrac.toDouble())
        put("highlightColor", rgba(floatArrayOf(highlightColor[0], highlightColor[1], highlightColor[2], 1f)))
        put("removeFillers", removeFillers)
        put("look", look); put("fontFamily", fontFamily); put("fontWeight", fontWeight); put("fontItalic", fontItalic); put("fontPath", fontPath)
        put("fill", rgba(fill)); put("stroke", rgba(stroke)); put("strokeFrac", strokeFrac.toDouble())
        put("box", box); put("boxColor", rgba(boxColor)); put("shadow", shadow)
        put("minDurationSec", minDurationSec.toDouble()); put("gapFrames", gapFrames); put("breakOnSentence", breakOnSentence)
        put("align", align); put("lineSpacing", lineSpacing.toDouble()); put("boxPadding", boxPadding.toDouble())
        put("boxRadius", boxRadius.toDouble()); put("shadowColor", rgba(shadowColor)); put("shadowBlur", shadowBlur.toDouble())
    }.toString()

    companion object {
        private fun rgba(c: FloatArray) = JSONArray().apply { for (i in 0 until 4) put(c.getOrElse(i) { 1f }.toDouble().coerceIn(0.0, 1.0)) }
        private fun JSONObject.color(key: String, fallback: FloatArray): FloatArray {
            val a = optJSONArray(key) ?: return fallback
            return if (a.length() < 4) fallback else FloatArray(4) { a.optDouble(it, 1.0).toFloat() }
        }

        /** Lê o JSON do motor; o que faltar vem de [base]. */
        fun fromJson(json: String, base: CaptionSettings = CaptionSettings()): CaptionSettings {
            val o = runCatching { JSONObject(json) }.getOrNull() ?: return base
            val hc = o.color("highlightColor", floatArrayOf(base.highlightColor[0], base.highlightColor[1], base.highlightColor[2], 1f))
            return base.copy(
                mode = o.optInt("mode", base.mode), maxWords = o.optInt("maxWords", base.maxWords),
                maxChars = o.optInt("maxChars", base.maxChars), maxLines = o.optInt("maxLines", base.maxLines),
                style = o.optInt("style", base.style), highlight = o.optBoolean("highlight", base.highlight),
                uppercase = o.optBoolean("uppercase", base.uppercase), breakOnPause = o.optBoolean("breakOnPause", base.breakOnPause),
                pauseSec = o.optDouble("pauseSec", base.pauseSec.toDouble()).toFloat(),
                posY = o.optDouble("posY", base.posY.toDouble()).toFloat(), sizeFrac = o.optDouble("sizeFrac", base.sizeFrac.toDouble()).toFloat(),
                highlightColor = floatArrayOf(hc[0], hc[1], hc[2]),
                look = o.optInt("look", base.look), fontFamily = o.optString("fontFamily", base.fontFamily),
                fontWeight = o.optInt("fontWeight", base.fontWeight), fontItalic = o.optBoolean("fontItalic", base.fontItalic),
                fontPath = o.optString("fontPath", base.fontPath),
                fill = o.color("fill", base.fill), stroke = o.color("stroke", base.stroke),
                strokeFrac = o.optDouble("strokeFrac", base.strokeFrac.toDouble()).toFloat(),
                box = o.optBoolean("box", base.box), boxColor = o.color("boxColor", base.boxColor), shadow = o.optBoolean("shadow", base.shadow),
                minDurationSec = o.optDouble("minDurationSec", base.minDurationSec.toDouble()).toFloat(),
                gapFrames = o.optInt("gapFrames", base.gapFrames), breakOnSentence = o.optBoolean("breakOnSentence", base.breakOnSentence),
                align = o.optInt("align", base.align), lineSpacing = o.optDouble("lineSpacing", base.lineSpacing.toDouble()).toFloat(),
                boxPadding = o.optDouble("boxPadding", base.boxPadding.toDouble()).toFloat(),
                boxRadius = o.optDouble("boxRadius", base.boxRadius.toDouble()).toFloat(),
                shadowColor = o.color("shadowColor", base.shadowColor), shadowBlur = o.optDouble("shadowBlur", base.shadowBlur.toDouble()).toFloat(),
            )
        }
    }
}

/** Bits de `CaptionSettings.look` (iguais a text::kCaptionLook*). */
object CaptionLook {
    const val FONT = 1
    const val FILL = 2
    const val STROKE = 4
    const val BOX = 8
    const val SHADOW = 16
    /** 0.5.3: alinhamento e entrelinha (text::kCaptionLookLayout). */
    const val LAYOUT = 32
}

/** Aparência resolvida pelo motor (presets::caption_preview_json), para desenhar a amostra. */
data class CaptionPreview(
    val weight: Int, val italic: Boolean, val fill: FloatArray, val stroke: FloatArray, val strokeFrac: Float,
    val box: Boolean, val boxColor: FloatArray, val shadow: Boolean, val shadowColor: FloatArray, val glow: Boolean,
    val highlight: Boolean, val highlightColor: FloatArray, val uppercase: Boolean,
) {
    companion object {
        fun parse(o: JSONObject?): CaptionPreview? {
            o ?: return null
            fun c(k: String) = o.optJSONArray(k)?.let { a -> FloatArray(4) { a.optDouble(it, 1.0).toFloat() } } ?: floatArrayOf(1f, 1f, 1f, 1f)
            return CaptionPreview(o.optInt("weight", 700), o.optBoolean("italic"), c("fill"), c("stroke"), o.optDouble("strokeFrac").toFloat(),
                o.optBoolean("box"), c("boxColor"), o.optBoolean("shadow"), c("shadowColor"), o.optBoolean("glow"),
                o.optBoolean("highlight"), c("highlightColor"), o.optBoolean("uppercase"))
        }
    }
}

/** Um modelo pronto do motor (text::caption_templates). */
data class CaptionTemplateItem(val id: String, val preview: CaptionPreview)

/**
 * O que os controles de modelo e aparência mexem: a faixa inteira
 * ([CaptionsState]) ou só os blocos marcados ([CaptionBlockLook]).
 */
interface CaptionLookTarget {
    val settings: CaptionSettings
    val templates: List<CaptionTemplateItem>
    /** Modelo escolhido; nulo depois de personalizar à mão. */
    val templateId: String?
    val preview: CaptionPreview?
    val lookEnabled: Boolean
    fun update(custom: Boolean = true, f: (CaptionSettings) -> CaptionSettings)
    fun applyTemplate(id: String)
}

/**
 * 0.4.6: estilo só dos blocos marcados (o "Estilo" do editor de blocos). As
 * opções começam nas do primeiro bloco (as dele ou as da faixa); cada toque
 * manda a aparência ao motor (op "look"), um passo de desfazer.
 */
class CaptionBlockLook internal constructor(
    private val engine: AureaEngine,
    private val owner: CaptionsState,
    val ids: Set<Long>,
    initial: CaptionSettings,
) : CaptionLookTarget {
    override var settings by mutableStateOf(initial)
        private set
    override val templates: List<CaptionTemplateItem> get() = owner.templates
    override var templateId by mutableStateOf<String?>(null)
        private set
    override var preview by mutableStateOf<CaptionPreview?>(null)
        private set
    override val lookEnabled: Boolean get() = owner.lookEnabled

    init { refreshPreview() }

    private fun refreshPreview() {
        preview = runCatching { CaptionPreview.parse(JSONObject(engine.captionPreview(settings.toJson()))) }.getOrNull()
    }

    override fun update(custom: Boolean, f: (CaptionSettings) -> CaptionSettings) {
        val next = f(settings)
        if (next == settings) return
        settings = next
        if (custom) templateId = null
        refreshPreview()
        owner.editBlocks(JSONObject().put("op", "look").put("ids", JSONArray(ids.toList())).put("options", next.toJson()))
    }

    override fun applyTemplate(id: String) {
        val json = engine.captionWithTemplate(settings.toJson(), id)
        if (json.isEmpty()) return
        update(custom = false) { CaptionSettings.fromJson(json, it) }
        templateId = id
    }

    /** Volta os blocos ao estilo da faixa. */
    fun clear() {
        owner.editBlocks(JSONObject().put("op", "clear_look").put("ids", JSONArray(ids.toList())))
        settings = owner.settings
        templateId = null
        refreshPreview()
    }
}

/**
 * Estado das legendas da camada escolhida: a transcrição (do cache, do
 * Whisper no aparelho ou de um SRT), a edição palavra a palavra e a geração
 * das camadas.
 *
 * O áudio nunca sai do aparelho: [transcribe] roda o Whisper local; a rede só
 * baixa os pesos do modelo na primeira vez de cada qualidade.
 */
class CaptionsState(
    private val app: Application,
    private val engine: AureaEngine,
    private val scope: CoroutineScope,
    private val onModelChanged: () -> Unit,
) : CaptionLookTarget {
    private var generation = 0L
    private var transcription: Job? = null
    fun resetProject() {
        generation++
        transcription?.cancel()
        transcription = null
        layer = null
        track = null
        selectedBlocks = emptySet()
        captionCount = 0
        error = null
        notice = null
        setWords(emptyList(), null)
    }
    private val vault = KeyVault(app)
    private val cache = TranscriptCache(app)
    var track by mutableStateOf<CaptionTrack?>(null)
        private set
    fun capturePreset(name: String): String = track?.let { engine.saveCaptionBundle(it.layer, name) }.orEmpty()
    fun applyPreset(data: String) {
        val current = track ?: return
        error = if (engine.applyCaptionBundle(current.layer, data)) null else AppText.get(app, R.string.app_caption_preset_incompatible)
        loadTrackOptions()
        onModelChanged()
    }
    /** Estilo dos blocos [ids]: começa no do primeiro marcado (o dele ou o da faixa). */
    fun blockLook(ids: Set<Long>): CaptionBlockLook {
        val first = track?.segments?.firstOrNull { it.id in ids }
        val initial = first?.look?.let { CaptionSettings.fromJson(it, settings) } ?: settings
        return CaptionBlockLook(engine, this, ids, initial)
    }

    /**
     * Uma operação do motor na faixa (um passo de desfazer). Falhou: nada
     * mudou e [failure] vira o erro mostrado.
     */
    fun editBlocks(command: org.json.JSONObject, failure: Int = R.string.app_caption_edit_overlap): Boolean {
        val current = track ?: return false
        if (!engine.editCaptionTrack(current.layer, command.toString())) { error = AppText.get(app, failure); return false }
        error = null
        refreshTrack(current.layer)
        onModelChanged()
        return true
    }

    /** Relê a faixa (depois de editar, desfazer ou mexer na timeline). */
    fun refreshTrack(layerId: Long? = track?.layer) {
        val id = layerId ?: return
        track = parseCaptionTracks(engine.captionTracks()).firstOrNull { it.layer == id }
        val ids = track?.segments?.mapTo(HashSet()) { it.id }.orEmpty()
        if (!ids.containsAll(selectedBlocks)) selectedBlocks = selectedBlocks.filterTo(LinkedHashSet()) { it in ids }
    }

    /**
     * 0.5.3: os blocos escolhidos na aba Legendas. A aba Estilo aplica neles
     * (ou na faixa toda) e a timeline marca o bloco escolhido.
     */
    var selectedBlocks by mutableStateOf<Set<Long>>(emptySet())

    /** SRT ou WebVTT da faixa, nos tempos da timeline; vazio = sem faixa. */
    fun exportCaptions(vtt: Boolean): String = track?.let { engine.captionExport(it.layer, vtt) }.orEmpty()

    /** Buscar e substituir na transcrição (o tempo das palavras fica). Devolve as trocas. */
    fun replaceInTranscript(find: String, replacement: String, matchCase: Boolean): Int {
        val (next, n) = CaptionWorkspace.replaceWords(words, find, replacement, matchCase)
        if (n == 0) return 0
        setWords(next, source)
        persist()
        return n
    }

    /**
     * Legendas só do trecho [from]..[to] da transcrição. Com a faixa pronta, o
     * motor refaz os blocos que o trecho cobre (o resto fica); sem faixa, ela
     * nasce só com o trecho.
     */
    fun createFromWords(from: Int, to: Int) {
        if (busy != null || words.isEmpty()) return
        val a = from.coerceIn(0, words.lastIndex); val b = to.coerceIn(a, words.lastIndex)
        val part = words.subList(a, b + 1).let { if (settings.removeFillers) it.filterIndexed { i, _ -> (a + i) !in fillers } else it }
        if (part.isEmpty()) return
        if (track == null) { generate(part); return }
        val list = JSONArray()
        part.forEach { w -> list.put(JSONObject().put("text", w.text).put("start", w.start).put("end", w.end)) }
        editBlocks(JSONObject().put("op", "from_words").put("words", list), R.string.cap053_selection_failed)
    }

    var hasGroqKey by mutableStateOf(vault.has(KeyVault.GROQ))
        private set
    var layer by mutableStateOf<Long?>(null)
        private set
    fun wordFrame(seconds: Double): Long = layer?.let { engine.captionWordFrame(it, seconds) } ?: -1L
    var words by mutableStateOf<List<Word>>(emptyList())
        private set
    /** Palavras marcadas como vício de linguagem (mesmo critério do motor). */
    var fillers by mutableStateOf<Set<Int>>(emptySet())
        private set
    var busy by mutableStateOf<String?>(null)
        private set
    var error by mutableStateOf<String?>(null)
    override var settings by mutableStateOf(CaptionSettings())
    var captionCount by mutableStateOf(0)
        private set

    // 0.4.6: modelos prontos (do motor) e aparência própria antes e depois de gerar.
    override val templates: List<CaptionTemplateItem> by lazy {
        runCatching {
            val a = JSONArray(engine.captionTemplates())
            (0 until a.length()).mapNotNull { i ->
                val o = a.getJSONObject(i)
                CaptionPreview.parse(o.optJSONObject("preview"))?.let { CaptionTemplateItem(o.getString("id"), it) }
            }
        }.getOrDefault(emptyList())
    }
    /** Modelo escolhido; nulo depois de personalizar à mão. */
    override var templateId by mutableStateOf<String?>("highlight")
        private set
    /** Amostra da aparência atual (o motor resolve estilo + personalização). */
    override var preview by mutableStateOf<CaptionPreview?>(null)
        private set

    private fun refreshPreview() {
        preview = runCatching { CaptionPreview.parse(JSONObject(engine.captionPreview(settings.toJson()))) }.getOrNull()
    }

    /**
     * Troca as opções. Com a faixa pronta, estilo, aparência, tamanho e altura
     * mudam na hora (um passo de desfazer); agrupamento vale ao refazer.
     */
    override val lookEnabled: Boolean get() = busy == null

    override fun update(custom: Boolean, f: (CaptionSettings) -> CaptionSettings) {
        val next = f(settings)
        if (next == settings) return
        settings = next
        if (custom) templateId = null
        refreshPreview()
        val current = track ?: return
        if (engine.setCaptionOptions(current.layer, next.toJson())) { error = null; onModelChanged() }
    }

    override fun applyTemplate(id: String) {
        val json = engine.captionWithTemplate(settings.toJson(), id)
        if (json.isEmpty()) return
        update(custom = false) { CaptionSettings.fromJson(json, it) }
        templateId = id
    }

    /** As opções guardadas na faixa (o painel mostra o que está na tela). */
    private fun loadTrackOptions() {
        val current = track ?: return
        val json = engine.captionOptions(current.layer)
        if (json.isEmpty()) return
        val loaded = CaptionSettings.fromJson(json, settings).copy(removeFillers = settings.removeFillers)
        if (loaded != settings) {
            settings = loaded
            templateId = null
        }
        refreshPreview()
    }

    // 0.2.5: idioma da fala e qualidade ficam guardados entre aberturas do
    // painel (e entre sessões); o padrão é o idioma do app e a melhor qualidade.
    private val prefs = app.getSharedPreferences("aurea.captions", android.content.Context.MODE_PRIVATE)
    /** Código do Whisper ("pt", "ja"…); nulo = detectar automaticamente. */
    var speechLanguage by mutableStateOf(
        if (prefs.contains(KEY_LANGUAGE)) prefs.getString(KEY_LANGUAGE, "").orEmpty().takeIf { it in SpeechLanguages.codes }
        else SpeechLanguages.preferred(app),
    )
        private set
    var quality by mutableStateOf(
        WhisperQuality.entries.getOrNull(prefs.getInt(KEY_QUALITY, -1)) ?: WhisperModels.best(app),
    )
        private set
    /** Aviso sem erro (a qualidade pedida não coube na memória livre e outra foi usada). */
    var notice by mutableStateOf<String?>(null)
        private set

    fun chooseLanguage(code: String?) {
        speechLanguage = code?.takeIf { it in SpeechLanguages.codes }
        prefs.edit().putString(KEY_LANGUAGE, speechLanguage.orEmpty()).apply()
    }
    fun chooseQuality(value: WhisperQuality) {
        quality = value
        notice = null
        prefs.edit().putInt(KEY_QUALITY, value.ordinal).apply()
    }
    fun qualityFits(value: WhisperQuality) = WhisperModels.fits(app, value)
    fun qualityDownloaded(value: WhisperQuality) = WhisperModels.downloaded(app, value)

    /** De onde veio a transcrição mostrada ("Whisper", "SRT", "cache"). */
    var source by mutableStateOf<String?>(null)
        private set

    fun setGroqKey(key: String) {
        val k = key.trim()
        if (k.isEmpty()) return
        vault.put(KeyVault.GROQ, k)
        hasGroqKey = true
    }

    fun clearGroqKey() {
        vault.remove(KeyVault.GROQ)
        hasGroqKey = false
    }

    private fun mediaKey(path: String): String? {
        val size = if (path.startsWith("content://")) {
            runCatching {
                app.contentResolver.query(Uri.parse(path), arrayOf(OpenableColumns.SIZE), null, null, null)?.use { c ->
                    if (c.moveToFirst()) c.getLong(0) else -1L
                }
            }.getOrNull() ?: -1L
        } else {
            File(path).length()
        }
        return cache.keyFor(path, size)
    }

    /**
     * Abre a camada: carrega a transcrição guardada (se houver). Nada é enviado.
     * Fase 8D: o tamanho do arquivo (content resolver) e o JSON de milhares de
     * palavras são lidos fora da thread da UI — abrir o painel não engasga.
     */
    fun isCaption(layerId: Long?) = layerId != null && parseCaptionTracks(engine.captionTracks()).any { it.layer == layerId }

    fun open(layerId: Long) {
        val tracks = parseCaptionTracks(engine.captionTracks())
        val was = track?.layer
        track = tracks.firstOrNull { it.layer == layerId || it.source == layerId }
        if (track?.layer != was) selectedBlocks = emptySet() else refreshTrack()
        val sourceId = tracks.firstOrNull { it.layer == layerId }?.source ?: layerId
        openSource(sourceId)
        if (track != null) loadTrackOptions() else if (preview == null) refreshPreview()
    }
    private fun openSource(layerId: Long) {
        if (busy != null && layer != layerId) return
        if (layer != layerId) {
            generation++
            layer = layerId
            error = null
            setWords(emptyList(), null)
            val path = engine.layerMediaPath(layerId)
            if (path != null) {
                val request = generation
                scope.launch {
                    val cached = withContext(Dispatchers.IO) { runCatching { mediaKey(path)?.let { cache.load(it) } }.getOrNull() }
                    if (generation == request && layer == layerId && cached != null && words.isEmpty()) setWords(cached, AppText.get(app, R.string.i18n_caption_source_cache))
                }
            }
        }
        captionCount = engine.captionCount(layerId)
    }

    /** Palavras novas; os vícios (uma consulta ao motor por palavra) são marcados em segundo plano. */
    private fun setWords(list: List<Word>, from: String?) {
        words = list
        source = from
        if (list.isEmpty()) {
            fillers = emptySet()
            return
        }
        scope.launch {
            val f = withContext(Dispatchers.Default) { list.indices.filterTo(HashSet()) { engine.isFillerWord(list[it].text) } }
            if (words === list) fillers = f
        }
    }

    /** Guarda a transcrição (JSON + tamanho do arquivo) fora da thread da UI. */
    private fun persist() {
        val id = layer ?: return
        val path = engine.layerMediaPath(id) ?: return
        val snapshot = words
        scope.launch(Dispatchers.IO) { runCatching { mediaKey(path)?.let { cache.save(it, snapshot) } } }
    }

    /** Whisper local: nenhum áudio é enviado à rede. */
    fun cancelTranscription() {
        transcription?.cancel()
        engine.captionProgress(true)
    }
    /**
     * 0.4.1: primeiro a NUVEM (Whisper large-v3-turbo, [CloudCaptions]) quando há conta;
     * qualquer falha (sem rede, limite, servidor) cai no Whisper local sem perguntar.
     * A tradução para o inglês continua só no local.
     */
    fun transcribe(language: String? = speechLanguage, thenGenerate: Boolean = true, translateEnglish: Boolean = false) {
        if (busy != null) return
        val id = layer ?: return
        if (engine.layerMediaPath(id) == null) { error = AppText.get(app, R.string.app_caption_no_audio_layer); return }
        if (translateEnglish || !CloudCaptions.available(app)) { transcribeOnDevice(language, thenGenerate, translateEnglish); return }
        val request = generation
        busy = AppText.get(app, R.string.cap041_cloud_progress, 0); error = null; notice = null
        transcription = scope.launch {
            var fallback = false
            try {
                val result = withContext(Dispatchers.IO) {
                    val context = currentCoroutineContext()
                    CloudCaptions.transcribe(app, engine, id, language,
                        progress = { p -> scope.launch(Dispatchers.Main) { if (generation == request) busy = AppText.get(app, R.string.cap041_cloud_progress, p) } },
                        active = { context.isActive && generation == request })
                }
                if (generation != request || layer != id) return@launch
                busy = null
                if (result.isEmpty()) error = AppText.get(app, R.string.app_caption_no_speech)
                else { setWords(result, AppText.get(app, R.string.cap041_cloud_source)); persist(); if (thenGenerate) generate() }
            } catch (cancelled: CancellationException) { throw cancelled }
            catch (failure: Exception) { fallback = generation == request }
            finally {
                if (generation == request && busy != AppText.get(app, R.string.app_caption_creating)) busy = null
            }
            if (fallback) {
                notice = AppText.get(app, R.string.cap041_cloud_fallback)
                transcribeOnDevice(language, thenGenerate, translateEnglish = false)
            }
        }
    }

    /** Whisper local (o modelo baixado no aparelho): nenhum áudio vai para a rede. */
    private fun transcribeOnDevice(language: String?, thenGenerate: Boolean, translateEnglish: Boolean) {
        if (busy != null) return
        val id = layer ?: return
        val request = generation
        // A memória livre muda: a qualidade pedida cai para a melhor que cabe agora.
        val wanted = quality
        val chosen = WhisperModels.usable(app, wanted)
        if (chosen != wanted) notice = AppText.get(app, R.string.cap025_quality_fallback, AppText.get(app, chosen.label))
        busy = AppText.get(app, R.string.app_caption_preparing_whisper); error = null
        transcription = scope.launch {
            try {
                val result = withContext(Dispatchers.IO) {
                    val context = currentCoroutineContext()
                    val model = WhisperModels.prepare(app, chosen) { stage ->
                        context.ensureActive()
                        scope.launch(Dispatchers.Main) { if (context.isActive && generation == request) busy = stage }
                    }
                    context.ensureActive()
                    withContext(Dispatchers.Main) { if (generation == request) busy = AppText.get(app, R.string.app_caption_transcribing) }
                    val ticker = scope.launch {
                        while (true) {
                            kotlinx.coroutines.delay(400)
                            if (generation == request) busy = AppText.get(app, R.string.app_caption_whisper_progress, engine.captionProgress())
                        }
                    }
                    try {
                        engine.transcribeLocal(id, model.absolutePath, language.orEmpty(), translateEnglish).lineSequence().mapNotNull { line ->
                            val fields = line.split('\t', limit = 3)
                            if (fields.size != 3) null else Word(fields[2], fields[0].toDouble(), fields[1].toDouble())
                        }.toList()
                    } finally { ticker.cancel() }
                }
                if (generation != request || layer != id) return@launch
                busy = null
                if (result.isEmpty()) error = AppText.get(app, R.string.app_caption_no_speech)
                else { setWords(result, AppText.get(app, R.string.i18n_caption_source_whisper)); persist(); if (thenGenerate) generate() }
            } catch (cancelled: CancellationException) { throw cancelled }
            catch (failure: Exception) {
                if (generation == request) error = (failure as? WhisperModels.Failure)?.message ?: AppText.get(app, R.string.app_caption_transcription_failed)
            } finally {
                // A new generation owns its own busy state after transcribe hands off.
                if (generation == request && busy != AppText.get(app, R.string.app_caption_creating)) busy = null
            }
        }
    }

    /** Legendas de um arquivo SRT (sem internet, sem chave). */
    fun importSrt(uri: Uri) {
        if (busy != null) return
        val id = layer ?: return
        val request = generation
        busy = AppText.get(app, R.string.app_caption_creating)
        scope.launch {
            try {
                val parsed = withContext(Dispatchers.IO) {
                    val text = app.contentResolver.openInputStream(uri)?.use { it.readBounded(4 * 1024 * 1024).toString(Charsets.UTF_8) }
                    text?.let { engine.parseSrt(it) }?.lineSequence()?.mapNotNull { line ->
                        val p = line.split('\t', limit = 3)
                        if (p.size < 3) null else Word(p[2], p[0].toDoubleOrNull() ?: return@mapNotNull null, p[1].toDoubleOrNull() ?: return@mapNotNull null)
                    }?.toList().orEmpty()
                }
                if (generation != request || layer != id) return@launch
                if (parsed.isEmpty()) error = AppText.get(app, R.string.app_caption_srt_unreadable)
                else { error = null; setWords(parsed, "SRT"); persist() }
            } catch (cancelled: CancellationException) { throw cancelled }
            catch (failure: Exception) { if (generation == request) error = AppText.get(app, R.string.app_caption_srt_unreadable) }
            finally { busy = null }
        }
    }

    /** Corrige uma palavra ouvida errado (vazio = tira a palavra). */
    fun editWord(index: Int, text: String) {
        if (index !in words.indices) return
        val t = text.trim()
        val list = words.toMutableList()
        // Só a palavra mexida muda de vício (antes: as N palavras reconsultadas).
        val f = HashSet<Int>(fillers.size + 1)
        if (t.isEmpty()) {
            list.removeAt(index)
            for (i in fillers) if (i < index) f.add(i) else if (i > index) f.add(i - 1)
        } else {
            list[index] = list[index].copy(text = t)
            for (i in fillers) if (i != index) f.add(i)
            if (engine.isFillerWord(t)) f.add(index)
        }
        words = list
        fillers = f
        persist()
    }

    /** Cria (ou refaz) as camadas de legenda — um passo de desfazer. */
    fun generate(only: List<Word>? = null) {
        if (busy != null) return
        val request = generation
        val id = layer ?: return
        val source = only ?: words
        if (source.isEmpty()) return
        val options = settings.toJson()
        val texts = source.map { it.text }.toTypedArray()
        val times = DoubleArray(source.size * 2) { i -> if (i % 2 == 0) source[i / 2].start else source[i / 2].end }
        // Fase 8D: milhares de palavras viram milhares de camadas (texto medido
        // uma a uma): fora da thread da UI, com aviso enquanto faz.
        busy = AppText.get(app, R.string.app_caption_creating)
        scope.launch {
            try {
                val n = withContext(Dispatchers.Default) { engine.createCaptionsJson(id, texts, times, options) }
                if (generation != request || layer != id) return@launch
                if (n < 0) error = AppText.get(app, R.string.app_caption_create_failed, -n) else error = null
                captionCount = engine.captionCount(id)
                // A faixa nova já aparece nas abas Legendas e Estilo.
                track = parseCaptionTracks(engine.captionTracks()).firstOrNull { it.source == id }
                selectedBlocks = emptySet()
                onModelChanged()
            } catch (cancelled: CancellationException) { throw cancelled }
            catch (failure: Exception) { if (generation == request) error = AppText.get(app, R.string.app_caption_create_failed, 10) }
            finally { busy = null }
        }
    }

    fun removeAll() {
        val id = layer ?: return
        engine.removeCaptions(id)
        captionCount = engine.captionCount(id)
        track = null
        selectedBlocks = emptySet()
        onModelChanged()
    }

    private companion object {
        const val KEY_LANGUAGE = "speech_language"
        const val KEY_QUALITY = "whisper_quality"
    }
}
