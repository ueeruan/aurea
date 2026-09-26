package com.aurea.aurea.captions

import android.app.Application
import android.net.Uri
import android.provider.OpenableColumns
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.aurea.aurea.engine.AureaEngine
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

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
)

/**
 * Estado das legendas da camada escolhida: a transcrição (do cache, da Groq
 * ou de um SRT), a edição palavra a palavra e a geração das camadas.
 *
 * O áudio só sai do aparelho em [transcribe], que a UI chama no toque de
 * "Gerar legendas" — e só com a chave configurada.
 */
class CaptionsState(
    private val app: Application,
    private val engine: AureaEngine,
    private val scope: CoroutineScope,
    private val onModelChanged: () -> Unit,
) {
    private val vault = KeyVault(app)
    private val cache = TranscriptCache(app)
    var track by mutableStateOf<CaptionTrack?>(null)
        private set
    fun capturePreset(name: String): String = track?.let { engine.saveCaptionBundle(it.layer, name) }.orEmpty()
    fun applyPreset(data: String) {
        val current = track ?: return
        error = if (engine.applyCaptionBundle(current.layer, data)) null else "Preset incompatível com esta versão do Aurea."
        onModelChanged()
    }
    fun editBlocks(command: org.json.JSONObject) {
        val current = track ?: return
        if (!engine.editCaptionTrack(current.layer, command.toString())) { error = "A edição sobrepõe outro bloco ou possui tempos inválidos."; return }
        error = null
        track = parseCaptionTracks(engine.captionTracks()).firstOrNull { it.layer == current.layer }
        onModelChanged()
    }

    var hasGroqKey by mutableStateOf(vault.has(KeyVault.GROQ))
        private set
    var layer by mutableStateOf<Long?>(null)
        private set
    var words by mutableStateOf<List<Word>>(emptyList())
        private set
    /** Palavras marcadas como vício de linguagem (mesmo critério do motor). */
    var fillers by mutableStateOf<Set<Int>>(emptySet())
        private set
    var busy by mutableStateOf<String?>(null)
        private set
    var error by mutableStateOf<String?>(null)
    var settings by mutableStateOf(CaptionSettings())
    var captionCount by mutableStateOf(0)
        private set
    /** De onde veio a transcrição mostrada ("Groq", "SRT", "cache"). */
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
        track = tracks.firstOrNull { it.layer == layerId || it.source == layerId }
        val sourceId = tracks.firstOrNull { it.layer == layerId }?.source ?: layerId
        openSource(sourceId)
    }
    private fun openSource(layerId: Long) {
        if (layer != layerId) {
            layer = layerId
            error = null
            setWords(emptyList(), null)
            val path = engine.layerMediaPath(layerId)
            if (path != null) {
                scope.launch {
                    val cached = withContext(Dispatchers.IO) { mediaKey(path)?.let { cache.load(it) } }
                    if (layer == layerId && cached != null && words.isEmpty()) setWords(cached, "cache")
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
        scope.launch(Dispatchers.IO) { mediaKey(path)?.let { cache.save(it, snapshot) } }
    }

    /** Whisper local: nenhum áudio é enviado à rede. */
    fun cancelTranscription() { engine.captionProgress(true) }
    fun transcribe(language: String?, thenGenerate: Boolean = true) {
        if (busy != null) return
        val id = layer ?: return
        if (engine.layerMediaPath(id) == null) { error = "Esta camada não tem mídia com som."; return }
        busy = "Preparando Whisper local…"; error = null
        scope.launch {
            val result = withContext(Dispatchers.IO) {
                runCatching {
                    val model = WhisperModels.prepare(app) { stage -> scope.launch(Dispatchers.Main) { busy = stage } }
                    withContext(Dispatchers.Main) { busy = "Transcrevendo no aparelho…" }
                    val ticker = scope.launch { while (true) { kotlinx.coroutines.delay(400); busy = "Whisper local: ${engine.captionProgress()}%" } }
                    try {
                        engine.transcribeLocal(id, model.absolutePath, language.orEmpty()).lineSequence().mapNotNull { line ->
                            val fields = line.split('\t', limit = 3)
                            if (fields.size != 3) null else Word(fields[2], fields[0].toDouble(), fields[1].toDouble())
                        }.toList()
                    } finally { ticker.cancel() }
                }
            }
            busy = null
            if (layer != id) return@launch
            result.onSuccess { list ->
                if (list.isEmpty()) error = "Nenhuma fala encontrada."
                else { setWords(list, "Whisper local"); persist(); if (thenGenerate) generate() }
            }.onFailure { error = it.message ?: "Falha na transcrição local." }
        }
    }

    /** Legendas de um arquivo SRT (sem internet, sem chave). */
    fun importSrt(uri: Uri) {
        scope.launch {
            val text = withContext(Dispatchers.IO) {
                runCatching { app.contentResolver.openInputStream(uri)?.use { it.readBytes().toString(Charsets.UTF_8) } }.getOrNull()
            }
            val parsed = text?.let { engine.parseSrt(it) }
                ?.lineSequence()
                ?.mapNotNull { line ->
                    val p = line.split('\t')
                    if (p.size < 3) null else Word(p[2], p[0].toDoubleOrNull() ?: return@mapNotNull null, p[1].toDoubleOrNull() ?: return@mapNotNull null)
                }
                ?.toList()
                .orEmpty()
            if (parsed.isEmpty()) {
                error = "Esse arquivo não tem legendas SRT legíveis."
                return@launch
            }
            error = null
            setWords(parsed, "SRT")
            persist()
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
    fun generate() {
        val id = layer ?: return
        if (words.isEmpty()) return
        val s = settings
        val texts = words.map { it.text }.toTypedArray()
        val times = DoubleArray(words.size * 2) { i -> if (i % 2 == 0) words[i / 2].start else words[i / 2].end }
        val ints = intArrayOf(
            s.mode, s.maxWords, s.maxChars, s.maxLines, s.style,
            if (s.highlight) 1 else 0, if (s.uppercase) 1 else 0, if (s.breakOnPause) 1 else 0, if (s.removeFillers) 1 else 0,
        )
        val floats = floatArrayOf(s.pauseSec, s.posY, s.sizeFrac, s.highlightColor[0], s.highlightColor[1], s.highlightColor[2])
        // Fase 8D: milhares de palavras viram milhares de camadas (texto medido
        // uma a uma): fora da thread da UI, com aviso enquanto faz.
        busy = "Criando legendas…"
        scope.launch {
            val n = withContext(Dispatchers.Default) { engine.createCaptions(id, texts, times, ints, floats) }
            busy = null
            if (n < 0) error = "Não foi possível criar as legendas (erro ${-n})." else error = null
            captionCount = engine.captionCount(id)
            onModelChanged()
        }
    }

    fun removeAll() {
        val id = layer ?: return
        engine.removeCaptions(id)
        captionCount = engine.captionCount(id)
        onModelChanged()
    }
}
