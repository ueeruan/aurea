package com.aurea.aurea.state

import android.app.Application
import android.content.ContentValues
import android.content.Intent
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.os.SystemClock
import android.provider.MediaStore
import androidx.annotation.StringRes
import androidx.core.content.FileProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.aurea.aurea.R
import com.aurea.aurea.diagnostics.ExitDiagnostics
import com.aurea.aurea.diagnostics.ProjectGuard
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.engine.directBuffer
import com.aurea.aurea.ui.i18n.AppText
import com.aurea.aurea.ui.i18n.EngineText
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.ceil
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToLong

/**
 * Formatos da tela Exportar. `engineCode` = ImageExportFormat do motor
 * (export/ImageEncode.hpp); −1 = o vídeo de sempre (encoder da plataforma).
 */
enum class ExportFormat(val engineCode: Int, val extension: String, val mime: String) {
    Video(-1, "mp4", "video/mp4"),
    Frame(0, "png", "image/png"),
    Sequence(1, "zip", "application/zip"),
    Gif(2, "gif", "image/gif"),
}

/** O plano do export como imagem: dimensões, quadros, alfa, bytes estimados, fps. */
data class ImagePlan(val width: Int, val height: Int, val frames: Int, val alpha: Boolean, val bytes: Long, val fps: Double)

/**
 * As regras do export como imagem — ESPELHO de plan_image_export e
 * estimate_image_export_bytes (ImageEncode.cpp). A tela usa o motor; isto vale
 * sem a biblioteca nativa (testes JVM) e é conferido contra os mesmos números.
 */
object ImageExportRules {
    val GifWidths = listOf(320, 480, 720)
    val GifFrameRates = listOf(10.0, 15.0, 24.0, 30.0)
    const val GIF_DEFAULT_WIDTH = 480
    const val GIF_DEFAULT_FPS = 15.0
    const val GIF_MAX_FRAMES = 1800
    const val SEQUENCE_MAX_FRAMES = 18000

    fun plan(format: ExportFormat, compW: Int, compH: Int, compFps: Double, durationFrames: Long, transparent: Boolean,
             shortSide: Int, gifWidth: Int, fps: Double): ImagePlan {
        if (compW <= 0 || compH <= 0 || format == ExportFormat.Video) return ImagePlan(0, 0, 0, false, 0, 0.0)
        val k = when {
            format == ExportFormat.Gif -> min(1.0, (if (gifWidth > 0) gifWidth else GIF_DEFAULT_WIDTH).coerceIn(16, 1080).toDouble() / compW)
            shortSide > 0 -> shortSide.toDouble() / min(compW, compH)
            else -> 1.0
        }
        val w = max(1L, (compW * k).roundToLong()).toInt()
        val h = max(1L, (compH * k).roundToLong()).toInt()
        val cfps = if (compFps > 0) compFps else 30.0
        if (format == ExportFormat.Frame) return ImagePlan(w, h, 1, transparent, estimateBytes(format, w, h, 1, transparent), cfps)
        val outFps = if (format == ExportFormat.Gif) {
            min((if (fps > 0) fps else GIF_DEFAULT_FPS).coerceIn(5.0, 50.0), max(5.0, cfps))
        } else if (fps > 0) fps else cfps
        val seconds = max(1L, durationFrames).toDouble() / cfps
        val frames = max(1, ceil(seconds * outFps - 1e-6).toInt())
        return ImagePlan(w, h, frames, transparent, estimateBytes(format, w, h, frames, transparent), outFps)
    }

    fun estimateBytes(format: ExportFormat, w: Int, h: Int, frames: Int, alpha: Boolean): Long {
        val px = w.toDouble() * h
        val n = max(1, frames)
        return when (format) {
            ExportFormat.Frame -> (px * (if (alpha) 2.0 else 1.6)).toLong() + 1024
            ExportFormat.Sequence -> (px * (if (alpha) 2.0 else 1.6) * n).toLong() + n * 120L + 1024
            ExportFormat.Gif -> (px * 0.55 * n).toLong() + n * 800L + 1024
            ExportFormat.Video -> 0L
        }
    }

    /** O motor recusa trechos acima disto (o arquivo não serviria a ninguém). */
    fun tooLong(format: ExportFormat, frames: Int): Boolean = when (format) {
        ExportFormat.Gif -> frames > GIF_MAX_FRAMES
        ExportFormat.Sequence -> frames > SEQUENCE_MAX_FRAMES
        else -> false
    }

    /** Onde o arquivo vai parar: Imagens/Aurea (PNG, GIF), Downloads/Aurea (.zip), Filmes/Aurea (vídeo). */
    fun galleryFolder(format: ExportFormat): String = when (format) {
        ExportFormat.Video -> "Movies/Aurea"
        ExportFormat.Frame, ExportFormat.Gif -> "Pictures/Aurea"
        ExportFormat.Sequence -> "Download/Aurea"
    }
}

/** Opções escolhidas na tela Exportar. */
data class ExportOptions(
    /** Lado menor: 720, 1080, 1440 ou 2160. */
    val shortSide: Int = 1080,
    /** 0 = o fps da composição. */
    val fps: Double = 0.0,
    val hevc: Boolean = false,
    /** 0 Baixa, 1 Normal, 2 Alta — multiplica a taxa automática do motor (BitratePolicy). */
    val quality: Int = 1,
    /** Mbps escolhido à mão (Avançado). 0 = automático pela qualidade. */
    val customMbps: Int = 0,
    val aiUpscale: Int = 0,
    val trimToContent: Boolean = true,
    val format: ExportFormat = ExportFormat.Video,
    /** PNG e sequência: lado menor; 0 = a resolução da composição. */
    val imageShortSide: Int = 0,
    /** GIF: largura máxima (px) e quadros por segundo. A sequência usa `fps`. */
    val gifWidth: Int = ImageExportRules.GIF_DEFAULT_WIDTH,
    val gifFps: Double = ImageExportRules.GIF_DEFAULT_FPS,
)

enum class ExportPhase { Idle, Running, Publishing, Done, Failed, Cancelled }

data class ExportUiState(
    val phase: ExportPhase = ExportPhase.Idle,
    val framesDone: Int = 0,
    val framesTotal: Int = 0,
    val fps: Float = 0f,
    val etaSeconds: Int = 0,
    val message: String = "",
    /** Resultado publicado (galeria), para abrir e compartilhar. */
    val outputUri: Uri? = null,
    val outputLabel: String = "",
    /** Aviso durante o export (encoder de software, aparelho quente). Vazio = nada. */
    val notice: String = "",
    val format: ExportFormat = ExportFormat.Video,
) {
    val fraction: Float get() = if (framesTotal > 0) (framesDone.toFloat() / framesTotal).coerceIn(0f, 1f) else 0f
}

/**
 * O export do lado da UI: pede ao motor, acompanha o progresso e, no fim,
 * publica o MP4 na galeria (Filmes/Aurea). O motor escreve num arquivo
 * temporário do app; a cópia para o MediaStore é o único trabalho daqui.
 */
class Exporter internal constructor(
    private val app: Application,
    private val engine: AureaEngine,
    private val scope: CoroutineScope,
    /** O projeto aberto (marcador do ProjectGuard durante o export). */
    private val projectPath: () -> String? = { null },
) {
    var state by mutableStateOf(ExportUiState())
        private set

    private val progressBuffer = directBuffer(128)
    private val progress = ExportProgress()
    private var poll: Job? = null
    private var sessionActive by mutableStateOf(false)
    @Volatile private var cancelPending = false

    val busy: Boolean get() = sessionActive || state.phase == ExportPhase.Running || state.phase == ExportPhase.Publishing

    init {
        // Temporário de um export que o sistema matou no meio (o app não teve
        // como apagar): some na abertura, fora da main thread. Só o que é mais
        // velho que esta sessão — um export novo nunca é tocado.
        val sessionStart = System.currentTimeMillis()
        scope.launch(Dispatchers.IO) {
            File(app.cacheDir, "export").listFiles()?.forEach { if (it.lastModified() < sessionStart) it.delete() }
        }
    }

    /**
     * A taxa de vídeo (Mbps) que o motor vai usar — a MESMA regra do encoder
     * (BitratePolicy.hpp), nunca uma conta paralela: o tamanho estimado na
     * tela é o que sai. Sem a biblioteca nativa (testes JVM), uma aproximação.
     */
    fun estimatedMbps(width: Int, height: Int, fps: Double, options: ExportOptions): Double =
        runCatching {
            AureaEngine.nativeExportBitrateBps(width, height, fps, if (options.hevc) 1 else 0, options.quality, options.customMbps) / 1e6
        }.getOrElse {
            if (options.customMbps > 0) options.customMbps.toDouble().coerceAtMost(100.0)
            else (8.0 * (width.toDouble() * height / (1920.0 * 1080.0)) *
                Math.pow((if (fps > 0.0) fps else 30.0).coerceIn(1.0, 240.0) / 30.0, 0.6) *
                when (options.quality) { 0 -> 0.6; 2 -> 1.5; else -> 1.0 } *
                if (options.hevc) 0.65 else 1.0).coerceIn(0.5, 60.0)
        }

    /** Tamanho estimado (bytes): vídeo + AAC 192 kbps + contêiner. */
    fun estimatedBytes(width: Int, height: Int, fps: Double, seconds: Double, options: ExportOptions): Long =
        if (seconds <= 0) 0L else ((estimatedMbps(width, height, fps, options) * 1e6 + AUDIO_BPS) * seconds / 8.0 * 1.015).toLong()

    /**
     * O plano do export como imagem pela regra do MOTOR (o que a tela mostra é
     * o que sai). Sem a biblioteca nativa, o espelho em Kotlin.
     */
    fun imagePlan(options: ExportOptions, compW: Int, compH: Int, compFps: Double, durationFrames: Long): ImagePlan =
        runCatching {
            engine.imageExportPlan(options.format.engineCode, options.imageShortSide, options.gifWidth, imageFps(options),
                options.trimToContent)?.let { ImagePlan(it[0].toInt(), it[1].toInt(), it[2].toInt(), it[3] != 0L, it[4], it[5] / 1000.0) }
        }.getOrNull() ?: ImageExportRules.plan(options.format, compW, compH, compFps, durationFrames, false,
            options.imageShortSide, options.gifWidth, imageFps(options))

    private fun imageFps(options: ExportOptions): Double = when (options.format) {
        ExportFormat.Gif -> options.gifFps
        ExportFormat.Sequence -> options.fps
        else -> 0.0
    }

    fun start(title: String, compWidth: Int, compHeight: Int, compFps: Double, options: ExportOptions) {
        if (busy) return
        val dir = File(app.cacheDir, "export")
        val stamp = SimpleDateFormat("yyyyMMdd-HHmmss-SSS", Locale.ROOT).format(Date())
        val base = title.ifBlank { "Aurea" }.replace(Regex("[^\\p{L}\\p{N} _-]"), "").trim().ifBlank { "Aurea" }
        val file = File(dir, "$base $stamp.${options.format.extension}")
        if (options.format != ExportFormat.Video) {
            startImage(dir, file, options)
            return
        }

        // Taxa: o motor calcula pela qualidade (0 = automático); só o Mbps
        // manual do Avançado é passado como número.
        val mbps = options.customMbps.coerceAtLeast(0)

        state = ExportUiState(ExportPhase.Running, outputLabel = file.nameWithoutExtension)
        sessionActive = true
        cancelPending = false
        poll?.cancel()
        val guarded = projectPath()
        poll = scope.launch {
            // Galaxy A32 "fecha em 70%": o marcador diz no próximo crash que foi
            // no export, e em que quadro (atualizado a cada 1% abaixo).
            guardedExport(guarded) {
                runExport(dir, file, options, mbps)
            }
        }
    }

    /** PNG, sequência .zip ou GIF: o motor renderiza e codifica; aqui, só publicar. */
    private fun startImage(dir: File, file: File, options: ExportOptions) {
        state = ExportUiState(ExportPhase.Running, outputLabel = file.nameWithoutExtension, format = options.format)
        sessionActive = true
        cancelPending = false
        poll?.cancel()
        val guarded = projectPath()
        poll = scope.launch {
            guardedExport(guarded) {
                runEngineJob(dir, file, options) {
                    engine.startImageExport(file.absolutePath, options.format.engineCode, options.imageShortSide, options.gifWidth,
                        imageFps(options), options.trimToContent)
                }
            }
        }
    }

    /** A failed IO/JNI call is a recoverable export error, never an uncaught UI coroutine. */
    private suspend fun guardedExport(guarded: String?, job: suspend () -> Unit) {
        try {
            if (guarded != null) withContext(Dispatchers.IO) { ProjectGuard.begin(app, ProjectGuard.Stage.EXPORT_VIDEO, guarded) }
            job()
        } catch (cancelled: CancellationException) {
            withContext(NonCancellable + Dispatchers.IO) { runCatching { engine.cancelExport() } }
            state = state.copy(phase = ExportPhase.Cancelled, message = text(R.string.app_export_cancelled))
            throw cancelled
        } catch (error: Exception) {
            withContext(NonCancellable + Dispatchers.IO) { runCatching { engine.cancelExport() } }
            android.util.Log.e("AureaExport", "Export operation failed", error)
            state = state.copy(phase = ExportPhase.Failed, message = text(R.string.app_export_interrupted))
        } finally {
            try {
                if (guarded != null) withContext(NonCancellable + Dispatchers.IO) {
                    runCatching { ProjectGuard.end(app, ProjectGuard.Stage.EXPORT_VIDEO) }
                        .onFailure { android.util.Log.w("AureaExport", "Export marker cleanup failed", it) }
                }
            } finally {
                // A new job cannot cancel this coroutine while its old guard cleanup is still pending.
                sessionActive = false
            }
        }
    }

    /**
     * O vídeo. O encoder do aparelho travou ou recusou no meio: o MOTOR sugere
     * refazer no modo de segurança (export/ExportWatchdog.hpp — H.264 Baseline
     * em múltiplos de 16 com taxa menor, depois o encoder de software). Refaz do
     * começo (um MP4 não continua de onde outro encoder parou), com o aviso na
     * tela, em vez de a exportação morrer no percentual em que travou.
     */
    private suspend fun runExport(dir: File, file: File, options: ExportOptions, mbps: Int) {
        var safeMode = 0
        while (true) {
            val level = safeMode
            val suggested = runEngineJob(dir, file, options, level) {
                engine.startExport(file.absolutePath, options.shortSide, options.fps, if (options.hevc) 1 else 0, mbps, options.aiUpscale,
                    options.trimToContent, options.quality.coerceIn(0, 2), rateModeFor(options.hevc && level == 0), level)
            }
            val next = ExportStallWatchdog.nextSafeMode(level, suggested)
            if (next == 0) return
            android.util.Log.w("AureaExport", "Video encoder stalled/refused; exporting again in safe mode $next")
            safeMode = next
            state = state.copy(phase = ExportPhase.Running, framesDone = 0, fps = 0f, etaSeconds = 0, message = "",
                notice = text(R.string.app_export_safe_mode))
        }
    }

    /**
     * Roda um export do motor até o fim e publica. Devolve o modo de segurança
     * em que o motor manda refazer (só vídeo, só se o usuário não cancelou);
     * 0 = terminou (pronto, falhou ou cancelado — o estado já diz qual).
     */
    private suspend fun runEngineJob(dir: File, file: File, options: ExportOptions, safeMode: Int = 0,
                                     startJob: () -> Int): Int {
        run {
            // Abrir o encoder, o MP4 e os alvos de GPU leva segundos em aparelho
            // lento (e no emulador): na main thread isso dava "Aurea não está
            // respondendo" no toque de Exportar.
            val code = withContext(Dispatchers.IO) {
                check(dir.isDirectory || dir.mkdirs()) { "Export directory unavailable" }
                // Startup removes orphaned files. A previous failed job can still be
                // unwinding its native writer here; never unlink that writer's output.
                startJob()
            }
            if (code != 0) {
                if (options.format == ExportFormat.Video && !cancelPending && engine.exportProgress(progressBuffer)) {
                    progress.readFrom(progressBuffer)
                    val retry = if (progress.finished && progress.result == code)
                        ExportStallWatchdog.nextSafeMode(safeMode, progress.retrySafeMode) else 0
                    if (retry > 0) return retry
                }
                val nativeCause = if (options.format == ExportFormat.Video && progress.finished &&
                    progress.result == code && progress.message.isNotBlank())
                    EngineText.reason(app, progress.message, code) else startError(code, options)
                state = state.copy(phase = ExportPhase.Failed, message = nativeCause)
                android.util.Log.e("AureaExport", "Export did not start: code=$code safeMode=$safeMode format=${options.format}")
                com.aurea.aurea.diagnostics.ProblemReport.noteExport(app,
                    "Android ${Build.VERSION.SDK_INT}: nao iniciou codigo=$code seguranca=$safeMode formato=${options.format} " +
                        "lado=${options.shortSide} hevc=${options.hevc}")
                return 0
            }
            if (cancelPending) engine.cancelExport()
            var markedPercent = -1
            val watchdog = ExportStallWatchdog()
            while (true) {
                delay(100)
                check(engine.exportProgress(progressBuffer)) { "Export engine unavailable" }
                progress.readFrom(progressBuffer)
                if (!progress.finished) {
                    // A última rede (ExportStallWatchdog): nada muda há minutos
                    // → cancela; nem assim conclui → a tela desiste e se libera.
                    when (watchdog.observe(SystemClock.elapsedRealtime(), progress.framesDone, progress.message)) {
                        ExportStallWatchdog.Verdict.Cancel -> {
                            android.util.Log.w("AureaExport", "No export progress for ${ExportStallWatchdog.STALL_MS / 1000} s " +
                                "at frame ${progress.framesDone}/${progress.framesTotal}; cancelling")
                            withContext(Dispatchers.IO) { runCatching { engine.cancelExport() } }
                        }
                        ExportStallWatchdog.Verdict.GiveUp -> {
                            android.util.Log.e("AureaExport", "Export engine did not conclude after the stall cancel; releasing the screen")
                            state = state.copy(phase = ExportPhase.Failed,
                                message = text(R.string.app_export_failed, text(R.string.expfail_stuck)))
                            return 0
                        }
                        ExportStallWatchdog.Verdict.Running -> Unit
                    }
                }
                val percent = if (progress.framesTotal > 0) (progress.framesDone.toLong() * 100 / progress.framesTotal).toInt() else 0
                if (percent != markedPercent) {
                    markedPercent = percent
                    ExitDiagnostics.mark(app, ExitDiagnostics.Phase.PROJECT_EXPORT_VIDEO, "f=${progress.framesDone}/${progress.framesTotal}")
                }
                state = state.copy(
                    framesDone = progress.framesDone,
                    framesTotal = progress.framesTotal,
                    fps = progress.fps,
                    etaSeconds = progress.etaSeconds,
                    notice = if (options.aiUpscale > 0 && progress.message.startsWith("IA:")) EngineText.aiProgress(app, progress.message) else noticeFor(progress, options),
                )
                if (!progress.finished) continue
                // O encoder travou/recusou e o motor sugere o modo de segurança:
                // nada de "falhou" na tela — o vídeo é refeito (runExport).
                val retry = if (options.format == ExportFormat.Video && progress.result != 0 && !cancelPending &&
                    !watchdog.cancelled) ExportStallWatchdog.nextSafeMode(safeMode, progress.retrySafeMode) else 0
                if (retry > 0) {
                    android.util.Log.w("AureaExport", "Export failed (reason ${progress.failure}, ${progress.message}); " +
                        "engine suggests safe mode $retry")
                    withContext(Dispatchers.IO) { file.delete() }
                    return retry
                }
                when (progress.result) {
                    0 -> publish(file, options.format)
                    // Cancelado pela própria tela (sem progresso por minutos), não
                    // pelo usuário: é falha, com o motivo.
                    CANCELLED -> state = if (watchdog.cancelled && !cancelPending) {
                        state.copy(phase = ExportPhase.Failed, message = text(R.string.app_export_failed, text(R.string.expfail_stuck)))
                    } else {
                        state.copy(phase = ExportPhase.Cancelled, message = text(R.string.app_export_cancelled))
                    }
                    STORAGE_FULL -> state = state.copy(phase = ExportPhase.Failed, message = text(R.string.msg_sem_espaco_no_aparelho_libere_espaco))
                    else -> state = state.copy(
                        phase = ExportPhase.Failed,
                        // O MOTIVO (código estável do motor) manda; a frase crua
                        // do motor só é consultada quando o motor não deu motivo.
                        message = text(R.string.app_export_failed,
                            failureReason(progress.failure) ?: EngineText.reason(app, progress.message, progress.result)),
                    )
                }
                // Temporário com dono (Fase 8B §52): falhou ou cancelou, o
                // arquivo parcial sai agora — não espera o próximo export.
                // Sucesso: `publish` já apagou depois de copiar para a galeria.
                if (progress.result != 0) withContext(Dispatchers.IO) { file.delete() }
                if (progress.result != 0 && (progress.result != CANCELLED || watchdog.cancelled) && !cancelPending) {
                    // A causa vai para o log e para o próximo "Relatar um problema".
                    val cause = "falhou: motivo=${progress.failure} codigo=${progress.result} " +
                        "quadro=${progress.framesDone}/${progress.framesTotal} seguranca=$safeMode " +
                        "travou=${watchdog.cancelled} formato=${options.format} motor=\"${progress.message}\""
                    android.util.Log.e("AureaExport", "Export $cause")
                    com.aurea.aurea.diagnostics.ProblemReport.noteExport(app, "Android ${Build.VERSION.SDK_INT}: $cause")
                }
                break
            }
        }
        return 0
    }

    fun cancel() {
        if (state.phase != ExportPhase.Running) return
        cancelPending = true   // ainda abrindo: cancela assim que o motor começar
        engine.cancelExport()
    }

    /** Volta ao estado inicial (tela reaberta ou fechada). */
    fun reset() {
        if (!busy) state = ExportUiState()
        // Momento ocioso (fora do render): prepara o anúncio da próxima exportação.
        com.aurea.aurea.ads.AureaAdsManager.preloadExportInterstitial()
    }

    fun shareIntent(): Intent? {
        val uri = state.outputUri ?: return null
        return Intent(Intent.ACTION_SEND).apply {
            type = state.format.mime
            putExtra(Intent.EXTRA_STREAM, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }.let {
            val title = if (state.format == ExportFormat.Video) text(R.string.app_share_video) else text(R.string.exp2_share_file)
            Intent.createChooser(it, title).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
    }

    fun viewIntent(): Intent? {
        val uri = state.outputUri ?: return null
        return Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(uri, state.format.mime)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
        }
    }

    private suspend fun publish(file: File, format: ExportFormat) {
        state = state.copy(phase = ExportPhase.Publishing)
        // Complete or roll back the MediaStore transaction even if the view model is cleared.
        val result = withContext(NonCancellable + Dispatchers.IO) {
            runCatching { if (format == ExportFormat.Video) copyToGallery(file) else copyImageOut(file, format) }
        }
        withContext(NonCancellable + Dispatchers.IO) { file.delete() }
        result.fold(
            onSuccess = { (uri, label) ->
                val pronto = state.copy(phase = ExportPhase.Done, outputUri = uri, message = label)
                // Publication succeeded independently of the ad SDK. A late
                // callback must never overwrite a newer export's state.
                state = pronto
                runCatching { com.aurea.aurea.ads.AureaAdsManager.showExportInterstitialIfAvailable { } }
            },
            onFailure = {
                android.util.Log.e("AureaExport", "Gallery publish failed", it)
                com.aurea.aurea.diagnostics.ProblemReport.noteExport(app,
                    "Android ${Build.VERSION.SDK_INT}: galeria falhou formato=$format ${it.javaClass.simpleName}: ${it.message}")
                state = state.copy(phase = ExportPhase.Failed, message = text(R.string.app_export_gallery_failed))
            },
        )
    }

    /** Copia para Filmes/Aurea. Devolve a Uri e o texto de onde ficou. */
    private fun copyToGallery(file: File): Pair<Uri, String> {
        check(file.isFile && file.length() > 0L) { "Export produced no file" }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val uri = insertIntoMediaStore(file, ExportFormat.Video,
                MediaStore.Video.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY))
            return uri to text(R.string.app_saved_to_gallery)
        }
        return copyLegacy(file, ExportFormat.Video, Environment.DIRECTORY_MOVIES, R.string.app_saved_to_gallery)
    }

    /**
     * Android 10+: item PENDENTE (com as datas de agora, GalleryPublish), cópia,
     * publicado. Qualquer falha desfaz o item — nunca fica um pendente órfão
     * que a galeria esconde.
     */
    private fun insertIntoMediaStore(file: File, format: ExportFormat, collection: Uri): Uri {
        val resolver = app.contentResolver
        val now = System.currentTimeMillis()
        val values = ContentValues().apply {
            GalleryPublish.pendingColumns(file.name, format.mime, GalleryPublish.relativeFolder(format), now,
                dateTaken = format != ExportFormat.Sequence).forEach { (k, v) ->
                when (v) { is Int -> put(k, v); is Long -> put(k, v); else -> put(k, v.toString()) }
            }
        }
        val uri = resolver.insert(collection, values) ?: error("MediaStore refused the insert")
        try {
            checkNotNull(resolver.openOutputStream(uri)) { "MediaStore output unavailable" }
                .use { out -> file.inputStream().use { it.copyTo(out, 1 shl 20) } }
            val publish = ContentValues().apply {
                GalleryPublish.publishColumns(System.currentTimeMillis()).forEach { (k, v) ->
                    when (v) { is Int -> put(k, v); is Long -> put(k, v); else -> put(k, v.toString()) }
                }
            }
            check(resolver.update(uri, publish, null, null) > 0) { "MediaStore did not publish the item" }
        } catch (e: Exception) {
            runCatching { resolver.delete(uri, null, null) }
            throw e
        }
        return uri
    }

    /**
     * Android 8–9: com WRITE_EXTERNAL_STORAGE, a pasta PÚBLICA + MediaScanner
     * (a galeria vê); sem ela, a pasta do app, também passada ao scanner.
     */
    private fun copyLegacy(file: File, format: ExportFormat, publicDir: String, @StringRes savedPublic: Int): Pair<Uri, String> {
        if (GalleryPublish.canWritePublic(app)) {
            val ok = runCatching {
                @Suppress("DEPRECATION")
                val dir = File(Environment.getExternalStoragePublicDirectory(publicDir), "Aurea").apply { mkdirs() }
                val dst = File(dir, file.name)
                file.copyTo(dst, overwrite = true)
                val shared = GalleryPublish.scan(app, dst, format.mime)
                    ?: FileProvider.getUriForFile(app, "${app.packageName}.exports", dst)
                shared to text(savedPublic)
            }.onFailure { android.util.Log.w("AureaExport", "Public folder copy failed; keeping the app folder", it) }
            ok.getOrNull()?.let { return it }
        }
        val dir = File(app.getExternalFilesDir(publicDir), "Aurea").apply { mkdirs() }
        val dst = File(dir, file.name)
        file.copyTo(dst, overwrite = true)
        runCatching { GalleryPublish.scan(app, dst, format.mime, 3_000) }
        return FileProvider.getUriForFile(app, "${app.packageName}.exports", dst) to text(R.string.app_saved_to_path, dst.absolutePath)
    }

    /**
     * PNG e GIF em Imagens/Aurea (aparecem na galeria); a sequência .zip em
     * Downloads/Aurea (o app Arquivos abre e descompacta).
     */
    private fun copyImageOut(file: File, format: ExportFormat): Pair<Uri, String> {
        check(file.isFile && file.length() > 0L) { "Export produced no file" }
        val zip = format == ExportFormat.Sequence
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val collection = if (zip) MediaStore.Downloads.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
                else MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
            val uri = insertIntoMediaStore(file, format, collection)
            return uri to text(if (zip) R.string.exp2_saved_downloads else R.string.exp2_saved_pictures)
        }
        return copyLegacy(file, format, if (zip) Environment.DIRECTORY_DOWNLOADS else Environment.DIRECTORY_PICTURES,
            if (zip) R.string.exp2_saved_downloads else R.string.exp2_saved_pictures)
    }

    /**
     * O que o motor avisou sobre ESTE export. Nenhum dos dois muda o vídeo:
     * resolução, fps e qualidade continuam os pedidos — muda só o tempo.
     */
    /**
     * Texto do motivo da falha (aurea::ExportFailure, ExportRules.hpp) no idioma
     * do app; null = sem motivo (cai na frase do motor). Encaixa em
     * app_export_failed.
     */
    private fun failureReason(failure: Int): String? = when (failure) {
        ExportProgress.FAILURE_ENCODER -> text(R.string.expfail_encoder)
        ExportProgress.FAILURE_ENCODER_STALLED -> text(R.string.expfail_encoder_stalled)
        ExportProgress.FAILURE_RENDER -> text(R.string.expfail_render)
        ExportProgress.FAILURE_GPU_MEMORY -> text(R.string.expfail_memory)
        ExportProgress.FAILURE_MEDIA -> text(R.string.expfail_media)
        ExportProgress.FAILURE_FILE -> text(R.string.expfail_file)
        ExportProgress.FAILURE_STORAGE -> text(R.string.msg_sem_espaco_no_aparelho_libere_espaco).trimEnd('.', ' ')
        ExportProgress.FAILURE_UNSUPPORTED -> text(R.string.expfail_unsupported)
        else -> null
    }

    private fun noticeFor(p: ExportProgress, options: ExportOptions): String {
        val lines = ArrayList<String>(4)
        when ((p.flags ushr 8) and 15) {
            4 -> lines += text(R.string.export_v2_finalizing)
            5 -> lines += text(R.string.export_v2_validating)
        }
        if (p.safeMode) {
            lines += text(R.string.app_export_safe_mode)
        }
        if (p.softwareEncoder) {
            // O modo de segurança é sempre H.264 (ExportWatchdog.hpp).
            val codec = if (options.hevc && (!p.safeMode || ((p.flags ushr 8) and 15) != 0)) "HEVC" else "H.264"
            lines += text(R.string.app_export_software_encoder, codec)
        }
        if (p.thermalReduced) {
            lines += text(R.string.app_export_thermal)
        }
        if (p.frameFallback) {
            lines += text(R.string.app_export_frame_fallback)
        }
        return lines.joinToString("\n")
    }

    /**
     * VBR quando o encoder PREFERIDO do aparelho (o mesmo que o motor abre com
     * createEncoderByType) anuncia VBR; senão CBR. Nunca CQ: qualidade
     * constante ignora a taxa e um minuto vira 1 GB. 1 = VBR, 0 = CBR.
     */
    private fun rateModeFor(hevc: Boolean): Int = runCatching {
        val mime = if (hevc) MediaFormat.MIMETYPE_VIDEO_HEVC else MediaFormat.MIMETYPE_VIDEO_AVC
        val codec = MediaCodec.createEncoderByType(mime)
        try {
            val enc = codec.codecInfo.getCapabilitiesForType(mime).encoderCapabilities
            when {
                enc?.isBitrateModeSupported(MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR) == true -> 1
                enc?.isBitrateModeSupported(MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CBR) == true -> 0
                else -> 1
            }
        } finally {
            codec.release()
        }
    }.getOrDefault(1)

    private fun startError(code: Int, options: ExportOptions): String = when (code) {
        NOT_SUPPORTED -> text(when {
            options.format == ExportFormat.Gif || options.format == ExportFormat.Sequence -> R.string.exp2_too_long
            options.format == ExportFormat.Frame -> R.string.app_export_unsupported_res
            options.hevc -> R.string.app_export_no_hevc
            else -> R.string.app_export_unsupported_res
        })
        OUT_OF_MEMORY -> text(R.string.app_export_no_vram)
        INVALID_STATE -> text(R.string.app_export_busy)
        STORAGE_FULL -> text(R.string.msg_sem_espaco_no_aparelho_libere_espaco)
        else -> text(R.string.app_export_start_failed, code)
    }

    /** Texto do catálogo no idioma do app (o `Application` sozinho resolveria no do sistema). */
    private fun text(@StringRes id: Int, vararg args: Any): String = AppText.get(app, id, *args)

    private companion object {
        // aurea::Errc (Result.hpp).
        const val INVALID_STATE = 5
        const val NOT_SUPPORTED = 6
        const val OUT_OF_MEMORY = 8
        const val CANCELLED = 25
        const val STORAGE_FULL = 28
        /** AAC do export (kExportAudioKbps no motor). */
        const val AUDIO_BPS = 192_000.0
    }
}
