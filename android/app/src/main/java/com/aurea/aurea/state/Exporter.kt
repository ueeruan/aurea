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
import android.provider.MediaStore
import androidx.annotation.StringRes
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
    val fraction: Float get() = if (framesTotal > 0) framesDone.toFloat() / framesTotal else 0f
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
    @Volatile private var cancelPending = false

    val busy: Boolean get() = state.phase == ExportPhase.Running || state.phase == ExportPhase.Publishing

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
            if (options.customMbps > 0) options.customMbps.toDouble()
            else 14.0 * (width.toDouble() * height / (1920.0 * 1080.0)) * (fps / 30.0) * (0.6 + 0.45 * options.quality)
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
        val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(Date())
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
        cancelPending = false
        poll?.cancel()
        val guarded = projectPath()
        poll = scope.launch {
            // Galaxy A32 "fecha em 70%": o marcador diz no próximo crash que foi
            // no export, e em que quadro (atualizado a cada 1% abaixo).
            if (guarded != null) withContext(Dispatchers.IO) { ProjectGuard.begin(app, ProjectGuard.Stage.EXPORT_VIDEO, guarded) }
            try {
                runExport(dir, file, options, mbps)
            } finally {
                if (guarded != null) withContext(NonCancellable + Dispatchers.IO) { ProjectGuard.end(app, ProjectGuard.Stage.EXPORT_VIDEO) }
            }
        }
    }

    /** PNG, sequência .zip ou GIF: o motor renderiza e codifica; aqui, só publicar. */
    private fun startImage(dir: File, file: File, options: ExportOptions) {
        state = ExportUiState(ExportPhase.Running, outputLabel = file.nameWithoutExtension, format = options.format)
        cancelPending = false
        poll?.cancel()
        val guarded = projectPath()
        poll = scope.launch {
            if (guarded != null) withContext(Dispatchers.IO) { ProjectGuard.begin(app, ProjectGuard.Stage.EXPORT_VIDEO, guarded) }
            try {
                runEngineJob(dir, file, options) {
                    engine.startImageExport(file.absolutePath, options.format.engineCode, options.imageShortSide, options.gifWidth,
                        imageFps(options), options.trimToContent)
                }
            } finally {
                if (guarded != null) withContext(NonCancellable + Dispatchers.IO) { ProjectGuard.end(app, ProjectGuard.Stage.EXPORT_VIDEO) }
            }
        }
    }

    private suspend fun runExport(dir: File, file: File, options: ExportOptions, mbps: Int) = runEngineJob(dir, file, options) {
        engine.startExport(file.absolutePath, options.shortSide, options.fps, if (options.hevc) 1 else 0, mbps, options.aiUpscale,
            options.trimToContent, options.quality.coerceIn(0, 2), rateModeFor(options.hevc))
    }

    private suspend fun runEngineJob(dir: File, file: File, options: ExportOptions, startJob: () -> Int) {
        run {
            // Abrir o encoder, o MP4 e os alvos de GPU leva segundos em aparelho
            // lento (e no emulador): na main thread isso dava "Aurea não está
            // respondendo" no toque de Exportar.
            val code = withContext(Dispatchers.IO) {
                dir.mkdirs()
                dir.listFiles()?.forEach { it.delete() }   // sobra de export interrompido
                startJob()
            }
            if (code != 0) {
                state = ExportUiState(ExportPhase.Failed, message = startError(code, options))
                return
            }
            if (cancelPending) engine.cancelExport()
            var markedPercent = -1
            while (true) {
                delay(100)
                if (!engine.exportProgress(progressBuffer)) continue
                progress.readFrom(progressBuffer)
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
                when (progress.result) {
                    0 -> publish(file, options.format)
                    CANCELLED -> state = state.copy(phase = ExportPhase.Cancelled, message = text(R.string.app_export_cancelled))
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
                break
            }
        }
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
        val result = withContext(Dispatchers.IO) {
            runCatching { if (format == ExportFormat.Video) copyToGallery(file) else copyImageOut(file, format) }
        }
        file.delete()
        result.fold(
            onSuccess = { (uri, label) ->
                val pronto = state.copy(phase = ExportPhase.Done, outputUri = uri, message = label)
                // Publication succeeded independently of the ad SDK. A late
                // callback must never overwrite a newer export's state.
                state = pronto
                com.aurea.aurea.ads.AureaAdsManager.showExportInterstitialIfAvailable { }
            },
            onFailure = { state = state.copy(phase = ExportPhase.Failed, message = text(R.string.app_export_gallery_failed)) },
        )
    }

    /** Copia para Filmes/Aurea. Devolve a Uri e o texto de onde ficou. */
    private fun copyToGallery(file: File): Pair<Uri, String> {
        val resolver = app.contentResolver
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val values = ContentValues().apply {
                put(MediaStore.Video.Media.DISPLAY_NAME, file.name)
                put(MediaStore.Video.Media.MIME_TYPE, "video/mp4")
                put(MediaStore.Video.Media.RELATIVE_PATH, "${Environment.DIRECTORY_MOVIES}/Aurea")
                put(MediaStore.Video.Media.IS_PENDING, 1)
            }
            val uri = resolver.insert(MediaStore.Video.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY), values)
                ?: error("MediaStore recusou")
            try {
                resolver.openOutputStream(uri)!!.use { out -> file.inputStream().use { it.copyTo(out, 1 shl 20) } }
                resolver.update(uri, ContentValues().apply { put(MediaStore.Video.Media.IS_PENDING, 0) }, null, null)
            } catch (e: Exception) {
                resolver.delete(uri, null, null)
                throw e
            }
            return uri to text(R.string.app_saved_to_gallery)
        }
        // Android 8–9: sem permissão de armazenamento, fica na pasta do app.
        val dir = File(app.getExternalFilesDir(Environment.DIRECTORY_MOVIES), "Aurea").apply { mkdirs() }
        val dst = File(dir, file.name)
        file.copyTo(dst, overwrite = true)
        return Uri.fromFile(dst) to text(R.string.app_saved_to_path, dst.absolutePath)
    }

    /**
     * PNG e GIF em Imagens/Aurea (aparecem na galeria); a sequência .zip em
     * Downloads/Aurea (o app Arquivos abre e descompacta).
     */
    private fun copyImageOut(file: File, format: ExportFormat): Pair<Uri, String> {
        val resolver = app.contentResolver
        val zip = format == ExportFormat.Sequence
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val values = ContentValues().apply {
                put(MediaStore.MediaColumns.DISPLAY_NAME, file.name)
                put(MediaStore.MediaColumns.MIME_TYPE, format.mime)
                put(MediaStore.MediaColumns.RELATIVE_PATH,
                    "${if (zip) Environment.DIRECTORY_DOWNLOADS else Environment.DIRECTORY_PICTURES}/Aurea")
                put(MediaStore.MediaColumns.IS_PENDING, 1)
            }
            val collection = if (zip) MediaStore.Downloads.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
                else MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
            val uri = resolver.insert(collection, values) ?: error("MediaStore recusou")
            try {
                resolver.openOutputStream(uri)!!.use { out -> file.inputStream().use { it.copyTo(out, 1 shl 20) } }
                resolver.update(uri, ContentValues().apply { put(MediaStore.MediaColumns.IS_PENDING, 0) }, null, null)
            } catch (e: Exception) {
                resolver.delete(uri, null, null)
                throw e
            }
            return uri to text(if (zip) R.string.exp2_saved_downloads else R.string.exp2_saved_pictures)
        }
        // Android 8–9: sem permissão de armazenamento, fica na pasta do app.
        val dir = File(app.getExternalFilesDir(if (zip) Environment.DIRECTORY_DOWNLOADS else Environment.DIRECTORY_PICTURES), "Aurea")
            .apply { mkdirs() }
        val dst = File(dir, file.name)
        file.copyTo(dst, overwrite = true)
        return Uri.fromFile(dst) to text(R.string.app_saved_to_path, dst.absolutePath)
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
        val lines = ArrayList<String>(2)
        if (p.softwareEncoder) {
            val codec = if (options.hevc) "HEVC" else "H.264"
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
                enc.isBitrateModeSupported(MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR) -> 1
                enc.isBitrateModeSupported(MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_CBR) -> 0
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
