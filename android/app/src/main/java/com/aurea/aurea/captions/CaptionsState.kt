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

    /** Abre a camada: carrega a transcrição guardada (se houver). Nada é enviado. */
    fun open(layerId: Long) {
        if (layer != layerId) {
            layer = layerId
            error = null
            val path = engine.layerMediaPath(layerId)
            val cached = path?.let { p -> mediaKey(p)?.let { cache.load(it) } }
            setWords(cached ?: emptyList(), if (cached != null) "cache" else null)
        }
        captionCount = engine.captionCount(layerId)
    }

    private fun setWords(list: List<Word>, from: String?) {
        words = list
        source = from
        fillers = list.indices.filter { engine.isFillerWord(list[it].text) }.toSet()
    }

    private fun persist() {
        val id = layer ?: return
        val path = engine.layerMediaPath(id) ?: return
        mediaKey(path)?.let { cache.save(it, words) }
    }

    /** Envia o áudio da camada à Groq (só aqui) e guarda a transcrição. */
    fun transcribe(language: String?, thenGenerate: Boolean = true) {
        val id = layer ?: return
        val key = vault.get(KeyVault.GROQ)
        if (key == null) {
            hasGroqKey = false
            error = "Configure a chave da Groq nos Ajustes para transcrever."
            return
        }
        val path = engine.layerMediaPath(id) ?: run { error = "Esta camada não tem mídia com som."; return }
        busy = "Separando o áudio…"
        error = null
        scope.launch {
            val result = withContext(Dispatchers.IO) {
                val tmp = File(app.cacheDir, "legendas/audio_${System.nanoTime()}.m4a")
                try {
                    AudioExtractor.extract(app, path, tmp)
                    withContext(Dispatchers.Main) { busy = "Transcrevendo na Groq…" }
                    Result.success(GroqWhisperProvider(key).transcribe(tmp, language))
                } catch (e: Exception) {
                    Result.failure(e)
                } finally {
                    tmp.delete()   // arquivo temporário do próprio app
                }
            }
            busy = null
            result.onSuccess { list ->
                if (list.isEmpty()) {
                    error = "Nenhuma fala encontrada."
                } else {
                    setWords(list, "Groq")
                    persist()
                    if (thenGenerate) generate()
                }
            }.onFailure { error = it.message ?: "Falha na transcrição." }
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
        if (t.isEmpty()) list.removeAt(index) else list[index] = list[index].copy(text = t)
        setWords(list, source)
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
        val n = engine.createCaptions(id, texts, times, ints, floats)
        if (n < 0) error = "Não foi possível criar as legendas (erro ${-n})." else error = null
        captionCount = engine.captionCount(id)
        onModelChanged()
    }

    fun removeAll() {
        val id = layer ?: return
        engine.removeCaptions(id)
        captionCount = engine.captionCount(id)
        onModelChanged()
    }
}
