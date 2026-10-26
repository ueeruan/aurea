package com.aurea.aurea.hyper

import android.app.Application
import android.graphics.Bitmap
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.SystemClock
import android.provider.OpenableColumns
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.state.ExportPhase
import com.aurea.aurea.state.Exporter
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.nio.ByteBuffer
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

// =============================================================================
//  AureaHyper na Home (0.3.2) — o fluxo de 4 telas (hyper/HyperScreen.kt):
//    1. Escolher: galeria ou gravar agora;
//    2. O que fazer: receitas prontas (Ajustar = os campos da Recipe);
//    3. Prévia: um quadro antes/depois (hyper_preview_frame), Turbo/Lite/Max
//       com o tempo estimado NESTE aparelho (hyper_estimate) e o medidor do
//       aparelho na primeira vez (hyper_device_profile, ~10 s);
//    4. Trabalhando: os passos (hyper_job_steps), quadros, tempo, Cancelar; o
//       job roda pelo caminho do export (Exporter.startHyper: serviço em
//       primeiro plano, vigia, cancelamento) e o resultado vira PROJETO.
//  O par do iOS: HyperModel.swift / HyperView.swift.
// =============================================================================

class HyperFlow internal constructor(
    private val app: Application,
    private val engine: AureaEngine,
    val exporter: Exporter,
    private val scope: CoroutineScope,
    private val host: Host,
) {
    /** O que o fluxo pede ao EditorStore (projetos, navegação, avisos). */
    interface Host {
        /** Cria um projeto com o vídeo pronto; devolve o caminho do .aurea (null = falhou). */
        suspend fun createProjectFromVideo(video: File, title: String, width: Int, height: Int, fps: Float): String?
        fun openProject(path: String)
        fun close()
        fun toast(message: String)
        fun text(id: Int, vararg args: Any): String
    }

    enum class Step { Choose, Recipe, Preview, Working }

    data class Source(val path: String, val name: String, val width: Int, val height: Int, val fps: Float, val durationSeconds: Double)

    class Preview(val before: Bitmap, val after: Bitmap, val path: HyperPath)

    var step by mutableStateOf(Step.Choose)
        private set
    var source by mutableStateOf<Source?>(null)
        private set
    /** Copiando o vídeo escolhido para o app (o motor lê um arquivo). */
    var importing by mutableStateOf(false)
        private set
    var preset by mutableStateOf<HyperPreset?>(null)
        private set
    var variant by mutableStateOf(0)
        private set
    var recipe by mutableStateOf(HyperRecipe())
        private set
    /** "Ajustar" aberto (os campos da receita à mostra). */
    var advanced by mutableStateOf(false)

    var profile by mutableStateOf<HyperDeviceProfile?>(null)
        private set
    /** O medidor rodando; [measureStartedAt] dá a barra de ~10 s. */
    var measuring by mutableStateOf(false)
        private set
    var measureStartedAt by mutableLongStateOf(0L)
        private set
    var estimate by mutableStateOf<HyperEstimate?>(null)
        private set
    var estimating by mutableStateOf(false)
        private set
    var path by mutableStateOf(HyperPath.Turbo)
        private set
    var preview by mutableStateOf<Preview?>(null)
        private set
    var previewLoading by mutableStateOf(false)
        private set
    var previewFailed by mutableStateOf(false)
        private set
    /** Criando o projeto com o resultado. */
    var finishing by mutableStateOf(false)
        private set
    var error by mutableStateOf<String?>(null)
    /** O job foi cancelado por quem usa (a tela 4 mostra "Cancelando…" na hora). */
    var cancelRequested by mutableStateOf(false)
        private set

    /** O modelo de rostos (GFPGAN, sob demanda) está no aparelho; sem ele a etapa Face não muda o quadro. */
    var faceReady by mutableStateOf(true)
        private set

    /** Um job que o sistema interrompeu (o processo morreu): dá para continuar de onde parou. */
    data class Pending(val source: Source, val recipe: HyperRecipe, val path: HyperPath, val output: String)
    var pending by mutableStateOf<Pending?>(null)
        private set

    private val prefs = app.getSharedPreferences("aurea_hyper", android.content.Context.MODE_PRIVATE)

    private var previewJob: Job? = null
    private var previewGeneration = 0
    private var jobOutput: File? = null

    val busy: Boolean get() = importing || finishing || exporter.busy

    // ---- 1. Escolher ---------------------------------------------------------

    fun reset() {
        if (exporter.busy || finishing) return
        exporter.reset()
        step = Step.Choose
        source?.let { old -> scope.launch(Dispatchers.IO) { runCatching { File(old.path).delete() } } }
        source = null; preset = null; variant = 0; recipe = HyperRecipe(); advanced = false
        estimate = null; preview = null; previewFailed = false; error = null; cancelRequested = false
        pending = loadPending()
        refreshFaceStatus()
    }

    private fun refreshFaceStatus() {
        faceReady = runCatching { AureaEngine.nativeHyperFaceStatus()?.getOrNull(1) }.getOrNull() ?: false
    }

    // ---- job interrompido (o motor continua do checkpoint com a MESMA fonte, receita e saída) ----

    private fun savePending(p: Pending) {
        val o = org.json.JSONObject()
            .put("path", p.source.path).put("name", p.source.name).put("width", p.source.width).put("height", p.source.height)
            .put("fps", p.source.fps.toDouble()).put("duration", p.source.durationSeconds)
            .put("recipe", org.json.JSONArray(p.recipe.pack().toList())).put("route", p.path.code).put("output", p.output)
        prefs.edit().putString(KEY_PENDING, o.toString()).apply()
    }

    private fun clearPending() {
        pending = null
        prefs.edit().remove(KEY_PENDING).apply()
    }

    private fun loadPending(): Pending? = runCatching {
        val o = org.json.JSONObject(prefs.getString(KEY_PENDING, null) ?: return null)
        val source = Source(o.getString("path"), o.getString("name"), o.getInt("width"), o.getInt("height"),
            o.getDouble("fps").toFloat(), o.getDouble("duration"))
        if (!File(source.path).isFile) { clearPending(); return null }
        val a = o.getJSONArray("recipe")
        val v = DoubleArray(a.length()) { a.getDouble(it) }
        Pending(source, unpack(v) ?: return null, HyperPath.of(o.getInt("route")), o.getString("output"))
    }.getOrNull()

    private fun unpack(v: DoubleArray): HyperRecipe? {
        if (v.size < HyperRecipe.PACKED_SIZE) return null
        return HyperRecipe(upscale = v[0].toInt(), upscaleMode = if (v[1] >= 0.5) HyperUpscaleMode.Anime else HyperUpscaleMode.General,
            denoise = v[2].toFloat(), deblock = v[3] >= 0.5, deinterlace = v[4].toInt(), face = v[5] >= 0.5, faceStrength = v[6].toFloat(),
            targetFps = v[7], slowMotion = v[8].toInt(), stabilize = v[9] >= 0.5, stabilizeStrength = v[10].toFloat(),
            stabilizeToKeyframes = v[11] >= 0.5, path = HyperPath.of(v[12].toInt()))
    }

    /** Continua o job interrompido: a mesma fonte, receita, caminho e saída. */
    fun resumePending() {
        val p = pending ?: return
        if (exporter.busy) return
        source = p.source
        recipe = p.recipe
        preset = null
        path = p.path
        start(File(p.output))
    }

    fun discardPending() {
        val p = pending ?: return
        clearPending()
        scope.launch(Dispatchers.IO) {
            runCatching { File(p.output).delete() }
            runCatching { File(p.source.path).delete() }
        }
    }

    /** Vídeo da galeria ou da câmera: copia para o app e sonda. */
    fun pick(uri: Uri) {
        if (busy) return
        importing = true
        error = null
        scope.launch {
            try {
                val copied = withContext(Dispatchers.IO) { copyIn(uri) }
                val probed = copied?.let { withContext(Dispatchers.IO) { probe(it, displayName(uri)) } }
                if (probed == null) {
                    copied?.delete()
                    error = host.text(com.aurea.aurea.R.string.hyper_error_video)
                    return@launch
                }
                source?.let { old -> if (old.path != probed.path) withContext(Dispatchers.IO) { runCatching { File(old.path).delete() } } }
                source = probed
                estimate = null; preview = null
                refreshFaceStatus()
                step = Step.Recipe
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (failure: Exception) {
                Log.w(TAG, "copia do video falhou: ${failure.javaClass.simpleName}")
                error = host.text(com.aurea.aurea.R.string.hyper_error_video)
            } finally {
                importing = false
            }
        }
    }

    // ---- 2. O que fazer ------------------------------------------------------

    fun choose(p: HyperPreset, v: Int = p.variants.firstOrNull() ?: 0) {
        preset = p
        variant = v
        recipe = p.recipe(v).copy(path = recipe.path)
    }

    /** "Ajustar": qualquer campo; a receita deixa de ser a do cartão. */
    fun edit(r: HyperRecipe) {
        recipe = r
        if (preset?.recipe(variant)?.copy(path = r.path) != r) preset = null
    }

    fun toPreview() {
        val s = source ?: return
        if (recipe.identity) return
        step = Step.Preview
        error = null
        scope.launch {
            ensureProfile(remeasure = false)
            refreshEstimate(s)
            refreshPreview()
        }
    }

    fun back() {
        if (busy) return
        when (step) {
            Step.Choose -> host.close()
            Step.Recipe -> step = Step.Choose
            Step.Preview -> { previewJob?.cancel(); step = Step.Recipe }
            Step.Working -> { exporter.reset(); step = Step.Preview }
        }
    }

    // ---- 3. Prévia e estimativa ---------------------------------------------

    /** O medidor (na primeira vez o motor mede, ~10 s; `remeasure` refaz). */
    fun measure() {
        if (measuring) return
        scope.launch {
            ensureProfile(remeasure = true)
            source?.let { refreshEstimate(it) }
        }
    }

    private suspend fun ensureProfile(remeasure: Boolean) {
        if (measuring) return
        if (!remeasure && profile?.measured == true) return
        measuring = true
        measureStartedAt = SystemClock.elapsedRealtime()
        try {
            profile = withContext(Dispatchers.Default) { runCatching { engine.hyperDeviceProfile(remeasure) }.getOrNull() } ?: profile
        } finally {
            measuring = false
        }
    }

    private suspend fun refreshEstimate(s: Source) {
        estimating = true
        try {
            val r = recipe
            estimate = withContext(Dispatchers.Default) { runCatching { engine.hyperEstimate(r, s.path) }.getOrNull() }
            path = HyperPathRules.initial(estimate, profile)
        } finally {
            estimating = false
        }
    }

    fun selectPath(p: HyperPath) {
        if (estimate?.available(p) == false) return
        if (path == p) return
        path = p
        refreshPreview()
    }

    fun refreshPreview() {
        val s = source ?: return
        val r = recipe.copy(path = path)
        val chosen = path
        previewJob?.cancel()
        val generation = ++previewGeneration
        previewLoading = true
        previewFailed = false
        previewJob = scope.launch {
            val seconds = (s.durationSeconds * 0.33).coerceAtLeast(0.0)
            val result = withContext(Dispatchers.Default) {
                runCatching {
                    val size = IntArray(3)
                    val frames = engine.hyperPreviewFrame(r, s.path, seconds, chosen.code, size)
                    val w = size[1]; val h = size[2]
                    if (frames == null || frames.size < 2 || size[0] != 0 || w <= 0 || h <= 0) null
                    else Preview(rgbaBitmap(frames[0], w, h) ?: return@runCatching null, rgbaBitmap(frames[1], w, h) ?: return@runCatching null, chosen)
                }.getOrNull()
            }
            if (generation != previewGeneration) return@launch
            preview = result ?: preview
            previewFailed = result == null
            previewLoading = false
        }
    }

    // ---- 4. Trabalhando -----------------------------------------------------

    /**
     * Antes de cada job a config remota é consultada de novo (até ~10 s): o
     * modo do AureaHyper pode ter mudado ("off", "entitled") ou a versão pode
     * ter ficado abaixo da mínima.
     */
    fun start(resumeOutput: File? = null) {
        if (exporter.busy || finishing || recipe.identity || checkingConfig) return
        checkingConfig = true
        scope.launch {
            try {
                com.aurea.aurea.config.AppConfigStore.refreshNow()
                HyperEntitlements.recheck()
            } finally {
                checkingConfig = false
            }
            if (com.aurea.aurea.config.AppConfigStore.updateRequired) return@launch
            if (!HyperEntitlements.unlocked) {
                error = host.text(when (HyperEntitlements.access) {
                    HyperAccess.Off -> com.aurea.aurea.R.string.hyper_off_default
                    HyperAccess.NeedsConfig -> com.aurea.aurea.R.string.hyper_needs_internet
                    else -> com.aurea.aurea.R.string.hyper_effect_locked
                })
                return@launch
            }
            startNow(resumeOutput)
        }
    }

    /** Consultando a config antes de começar (o botão fica ocupado). */
    var checkingConfig by mutableStateOf(false)
        private set

    private fun startNow(resumeOutput: File?) {
        val s = source ?: return
        if (exporter.busy || finishing || recipe.identity) return
        if (resumeOutput == null && estimate?.available(path) == false) return
        val dir = File(app.filesDir, "projetos/hyper").apply { mkdirs() }
        val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(Date())
        val base = s.name.replace(Regex("[^\\p{L}\\p{N} _-]"), "").trim().take(60).ifBlank { "Aurea" }
        // "Tentar de novo" e "Continuar" usam a MESMA saída: o motor retoma o que já estava pronto.
        val out = resumeOutput ?: File(dir, "$base Hyper $stamp.mp4")
        jobOutput = out
        savePending(Pending(s, recipe, path, out.absolutePath))
        pending = null
        cancelRequested = false
        error = null
        exporter.reset()
        exporter.startHyper(s.path, recipe.copy(path = path), out)
        step = Step.Working
    }

    /** "Tentar de novo" depois de falha/cancelamento: a mesma saída (o motor retoma o que já validou). */
    fun retry() = start(jobOutput)

    fun cancel() {
        if (exporter.state.phase != ExportPhase.Running) return
        cancelRequested = true
        exporter.cancel()
        clearPending()   // cancelado por quem usa: não volta como "interrompido"
    }

    /**
     * O job terminou com o arquivo pronto: vira projeto e abre no editor. A
     * tela chama ao ver a fase Done (só com o app à vista: o motor pode ter
     * sido suspenso em segundo plano e volta antes disto).
     */
    fun finishIfDone() {
        if (finishing || exporter.state.phase != ExportPhase.Done) return
        val out = exporter.hyperOutput ?: return
        val s = source ?: return
        finishing = true
        scope.launch {
            try {
                val media = withContext(Dispatchers.IO) { probe(out, s.name) }
                if (media == null) {
                    error = host.text(com.aurea.aurea.R.string.hyper_error_result)
                    return@launch
                }
                val title = "${s.name} · ${recipe.summary()}"
                val w = media.width and 1.inv(); val h = media.height and 1.inv()
                val project = host.createProjectFromVideo(out, title, w, h, media.fps)
                if (project == null) {
                    error = host.text(com.aurea.aurea.R.string.hyper_error_project)
                    return@launch
                }
                // A cópia do original já não serve: o projeto usa o resultado.
                clearPending()
                withContext(Dispatchers.IO) { runCatching { File(s.path).delete() } }
                source = null
                exporter.reset()
                step = Step.Choose
                preset = null; recipe = HyperRecipe(); preview = null; estimate = null
                host.openProject(project)
            } finally {
                finishing = false
            }
        }
    }

    // ---- arquivos -----------------------------------------------------------

    private fun displayName(uri: Uri): String? = runCatching {
        app.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { c -> if (c.moveToFirst()) c.getString(0) else null }
    }.getOrNull()

    /** O motor lê caminho de arquivo: a mídia vai para files/hyper/fontes (sobrevive ao processo). */
    private fun copyIn(uri: Uri): File? {
        val dir = File(app.filesDir, "hyper/fontes").apply { mkdirs() }
        val ext = displayName(uri)?.substringAfterLast('.', "")?.lowercase(Locale.ROOT)
            ?.takeIf { it.length in 1..5 && it.all(Char::isLetterOrDigit) } ?: "mp4"
        val target = File(dir, "fonte-${System.currentTimeMillis()}.$ext")
        val pending = File(dir, target.name + ".parcial")
        return try {
            val input = app.contentResolver.openInputStream(uri) ?: return null
            input.use { src -> FileOutputStream(pending).use { out -> src.copyTo(out, 1 shl 16); out.fd.sync() } }
            if (pending.length() <= 0L || !pending.renameTo(target)) { pending.delete(); null } else target
        } catch (e: Exception) {
            pending.delete()
            Log.w(TAG, "copia falhou: ${e.javaClass.simpleName}")
            null
        }
    }

    private fun probe(file: File, name: String?): Source? {
        val r = MediaMetadataRetriever()
        return try {
            r.setDataSource(file.absolutePath)
            val w = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)?.toIntOrNull() ?: 0
            val h = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)?.toIntOrNull() ?: 0
            val rot = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_ROTATION)?.toIntOrNull() ?: 0
            val ms = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull() ?: 0L
            val frames = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_FRAME_COUNT)?.toLongOrNull() ?: 0L
            val fps = r.extractMetadata(MediaMetadataRetriever.METADATA_KEY_CAPTURE_FRAMERATE)?.toFloatOrNull()?.takeIf { it in 1f..240f }
                ?: if (frames > 0 && ms > 0) (frames * 1000f / ms).takeIf { it in 1f..240f } ?: 30f else 30f
            if (w <= 0 || h <= 0) return null
            val label = name?.substringBeforeLast('.')?.takeIf { it.isNotBlank() } ?: host.text(com.aurea.aurea.R.string.app_imported_media)
            if (rot == 90 || rot == 270) Source(file.absolutePath, label, h, w, fps, ms / 1000.0)
            else Source(file.absolutePath, label, w, h, fps, ms / 1000.0)
        } catch (_: Exception) {
            null
        } finally {
            runCatching { r.release() }
        }
    }

    private fun rgbaBitmap(rgba: ByteArray, w: Int, h: Int): Bitmap? {
        if (rgba.size < w.toLong() * h * 4) return null
        return Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888).apply { copyPixelsFromBuffer(ByteBuffer.wrap(rgba, 0, w * h * 4)) }
    }

    private companion object {
        const val TAG = "AureaHyper"
        const val KEY_PENDING = "pendente"
    }
}
