package com.aurea.aurea.state

import android.app.Application
import android.content.ContentValues
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.engine.directBuffer
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** Opções escolhidas na tela Exportar. */
data class ExportOptions(
    /** Lado menor: 720, 1080, 1440 ou 2160. */
    val shortSide: Int = 1080,
    /** 0 = o fps da composição. */
    val fps: Double = 0.0,
    val hevc: Boolean = false,
    /** Alta = 1,6× a taxa automática do motor. */
    val highQuality: Boolean = false,
    val aiUpscale: Int = 0,
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
) {
    var state by mutableStateOf(ExportUiState())
        private set

    private val progressBuffer = directBuffer(128)
    private val progress = ExportProgress()
    private var poll: Job? = null

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

    /** Bitrate automático do motor (0,2 bit/pixel·s) × fator de qualidade, em Mbps. */
    fun estimatedMbps(width: Int, height: Int, fps: Double, options: ExportOptions): Double {
        val auto = (width.toDouble() * height * fps * 0.2).coerceIn(2e6, 120e6) / 1e6
        return if (options.highQuality) auto * 1.6 else auto
    }

    fun start(title: String, compWidth: Int, compHeight: Int, compFps: Double, options: ExportOptions) {
        if (busy) return
        val dir = File(app.cacheDir, "export").apply { mkdirs() }
        dir.listFiles()?.forEach { it.delete() }   // sobra de export interrompido
        val stamp = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(Date())
        val base = title.ifBlank { "Aurea" }.replace(Regex("[^\\p{L}\\p{N} _-]"), "").trim().ifBlank { "Aurea" }
        val file = File(dir, "$base $stamp.mp4")

        val short = minOf(compWidth, compHeight).coerceAtLeast(1)
        val w = (compWidth.toDouble() * options.shortSide / short).toInt()
        val h = (compHeight.toDouble() * options.shortSide / short).toInt()
        val fps = if (options.fps > 0) options.fps else compFps
        val mbps = if (options.highQuality) estimatedMbps(w, h, fps, options).toInt().coerceAtLeast(1) else 0

        val code = engine.startExport(file.absolutePath, options.shortSide, options.fps, if (options.hevc) 1 else 0, mbps, options.aiUpscale)
        if (code != 0) {
            state = ExportUiState(ExportPhase.Failed, message = startError(code, options))
            return
        }
        state = ExportUiState(ExportPhase.Running, outputLabel = file.nameWithoutExtension)
        poll?.cancel()
        poll = scope.launch {
            while (true) {
                delay(100)
                if (!engine.exportProgress(progressBuffer)) continue
                progress.readFrom(progressBuffer)
                state = state.copy(
                    framesDone = progress.framesDone,
                    framesTotal = progress.framesTotal,
                    fps = progress.fps,
                    etaSeconds = progress.etaSeconds,
                    notice = if (options.aiUpscale > 0 && progress.message.startsWith("IA:")) progress.message else noticeFor(progress, options),
                )
                if (!progress.finished) continue
                when (progress.result) {
                    0 -> publish(file)
                    CANCELLED -> state = state.copy(phase = ExportPhase.Cancelled, message = "Exportação cancelada.")
                    else -> state = state.copy(
                        phase = ExportPhase.Failed,
                        message = "Não deu para exportar: ${progress.message.ifBlank { "erro ${progress.result}" }}.",
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
        if (state.phase == ExportPhase.Running) engine.cancelExport()
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
            type = "video/mp4"
            putExtra(Intent.EXTRA_STREAM, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }.let { Intent.createChooser(it, "Compartilhar vídeo").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK) }
    }

    fun viewIntent(): Intent? {
        val uri = state.outputUri ?: return null
        return Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(uri, "video/mp4")
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
        }
    }

    private suspend fun publish(file: File) {
        state = state.copy(phase = ExportPhase.Publishing)
        val result = withContext(Dispatchers.IO) { runCatching { copyToGallery(file) } }
        file.delete()
        result.fold(
            onSuccess = { (uri, label) ->
                val pronto = state.copy(phase = ExportPhase.Done, outputUri = uri, message = label)
                // Ponto seguro: o render ACABOU e o vídeo já está na galeria. Se houver
                // anúncio (e a frequência deixar), ele aparece antes do resultado; sem
                // anúncio, offline ou com erro, o resultado aparece na hora.
                com.aurea.aurea.ads.AureaAdsManager.showExportInterstitialIfAvailable { state = pronto }
            },
            onFailure = { state = state.copy(phase = ExportPhase.Failed, message = "O vídeo foi gerado, mas não consegui salvar na galeria.") },
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
            return uri to "Salvo na galeria, em Filmes › Aurea."
        }
        // Android 8–9: sem permissão de armazenamento, fica na pasta do app.
        val dir = File(app.getExternalFilesDir(Environment.DIRECTORY_MOVIES), "Aurea").apply { mkdirs() }
        val dst = File(dir, file.name)
        file.copyTo(dst, overwrite = true)
        return Uri.fromFile(dst) to "Salvo em ${dst.absolutePath}."
    }

    /**
     * O que o motor avisou sobre ESTE export. Nenhum dos dois muda o vídeo:
     * resolução, fps e qualidade continuam os pedidos — muda só o tempo.
     */
    private fun noticeFor(p: ExportProgress, options: ExportOptions): String {
        val lines = ArrayList<String>(2)
        if (p.softwareEncoder) {
            val codec = if (options.hevc) "HEVC" else "H.264"
            lines += "Este aparelho não tem encoder de hardware $codec para esta resolução: " +
                "exportando por software (mais lento, mesma qualidade)."
        }
        if (p.thermalReduced) {
            lines += "Aparelho quente: a exportação desacelerou para esfriar. A qualidade não muda."
        }
        return lines.joinToString("\n")
    }

    private fun startError(code: Int, options: ExportOptions): String = when (code) {
        NOT_SUPPORTED -> if (options.hevc) "Este aparelho não exporta HEVC nesta resolução. Tente H.264."
                         else "Este aparelho não exporta nesta resolução."
        OUT_OF_MEMORY -> "Sem memória de vídeo para exportar nesta resolução."
        INVALID_STATE -> "Já existe uma exportação em andamento."
        else -> "Não deu para começar a exportação (erro $code)."
    }

    private companion object {
        // aurea::Errc (Result.hpp).
        const val INVALID_STATE = 5
        const val NOT_SUPPORTED = 6
        const val OUT_OF_MEMORY = 8
        const val CANCELLED = 25
    }
}
