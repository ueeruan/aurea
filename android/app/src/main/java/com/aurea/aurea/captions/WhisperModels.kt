package com.aurea.aurea.captions

import android.app.ActivityManager
import android.content.Context
import android.os.Process
import androidx.annotation.StringRes
import com.aurea.aurea.R
import com.aurea.aurea.ui.i18n.AppText
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

/**
 * Qualidade da transcrição = tamanho do modelo Whisper (pesos oficiais q5_1
 * do ggerganov/whisper.cpp; tamanho e SHA-256 do ponteiro LFS publicado).
 * O mesmo trio existe no iOS (`WhisperQuality` em MediaPanels.swift).
 */
enum class WhisperQuality(
    val model: String, val bytes: Long, val sha256: String, val megabytes: Int,
    @StringRes val label: Int, @StringRes val hint: Int,
) {
    FAST("tiny", 32152673L, "818710568da3ca15689e31a743197b520007872ff9576237bda97bd1b469c3d7", 32,
        R.string.cap025_quality_fast, R.string.cap025_quality_fast_hint),
    BALANCED("base", 59707625L, "422f1ae452ade6f30a004d7e5c6a43195e4433bc370bf23fac9cc591f01a8898", 60,
        R.string.cap025_quality_balanced, R.string.cap025_quality_balanced_hint),
    ACCURATE("small", 190085487L, "ae85e4a935d7a567bd102fe55afc16bb595bdb618e11b2fc7591bc08120411bb", 190,
        R.string.cap025_quality_accurate, R.string.cap025_quality_accurate_hint);

    val fileName: String get() = "ggml-$model-q5_1.bin"
}

/** Only model weights are downloaded. This class never receives user audio. */
object WhisperModels {
    private const val GIB = 1024L * 1024 * 1024

    /** Falha já traduzida para mostrar como está (sem rede, sem espaço, arquivo corrompido). */
    class Failure(message: String) : IOException(message)

    /**
     * Cabe agora neste aparelho? O modelo pequeno (tiny) sempre; os outros
     * pedem 64 bits e memória livre com folga (o editor continua aberto ao lado:
     * base usa ~350 MB e small ~650 MB durante a transcrição).
     */
    fun fits(context: Context, quality: WhisperQuality): Boolean {
        if (quality == WhisperQuality.FAST) return true
        val manager = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
        val memory = ActivityManager.MemoryInfo().also { manager.getMemoryInfo(it) }
        if (!Process.is64Bit() || memory.lowMemory || manager.isLowRamDevice) return false
        return when (quality) {
            WhisperQuality.BALANCED -> memory.availMem >= GIB * 5 / 4
            WhisperQuality.ACCURATE -> memory.totalMem >= GIB * 11 / 2 && memory.availMem >= GIB * 2
            WhisperQuality.FAST -> true
        }
    }

    /** A melhor qualidade que o aparelho comporta agora. */
    fun best(context: Context): WhisperQuality = WhisperQuality.entries.last { fits(context, it) }

    /** A pedida, ou a melhor abaixo dela que ainda cabe (a memória livre muda). */
    fun usable(context: Context, wanted: WhisperQuality): WhisperQuality =
        WhisperQuality.entries.filter { it.ordinal <= wanted.ordinal }.last { fits(context, it) }

    private fun dir(context: Context) = File(context.filesDir, "whisper")

    /** Já baixado (só o tamanho; o hash é conferido em [prepare]). */
    fun downloaded(context: Context, quality: WhisperQuality): Boolean = File(dir(context), quality.fileName).length() == quality.bytes

    @Synchronized
    fun prepare(context: Context, quality: WhisperQuality, progress: (String) -> Unit): File {
        val name = quality.model
        val size = quality.bytes
        val hash = quality.sha256
        val dir = dir(context).apply { mkdirs() }
        val target = File(dir, quality.fileName)
        fun digest(file: File): String {
            val md = MessageDigest.getInstance("SHA-256")
            file.inputStream().use { input -> val buffer = ByteArray(65536); while (true) { val n = input.read(buffer); if (n < 0) break; md.update(buffer, 0, n) } }
            return md.digest().joinToString("") { "%02x".format(it) }
        }
        if (target.length() == size && digest(target) == hash) return target
        if (dir.usableSpace < size * 2) throw Failure(AppText.get(context, R.string.app_whisper_no_space, name))
        val partial = File(dir, target.name + ".part")
        val connection = URL("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/${target.name}").openConnection() as HttpURLConnection
        connection.connectTimeout = 20000; connection.readTimeout = 30000
        try {
            if (connection.responseCode != 200) throw Failure(AppText.get(context, R.string.app_whisper_download_failed))
            connection.inputStream.use { input -> partial.outputStream().use { output ->
                val buffer = ByteArray(65536); var received = 0L; var shown = -1L
                while (true) {
                    if (Thread.currentThread().isInterrupted) throw Failure(AppText.get(context, R.string.app_download_cancelled))
                    val n = input.read(buffer); if (n < 0) break
                    received += n; if (received > size) throw Failure(AppText.get(context, R.string.app_whisper_invalid_model))
                    output.write(buffer, 0, n)
                    // Um aviso por ponto percentual (o modelo preciso tem 190 MB).
                    val percent = received * 100 / size
                    if (percent != shown) { shown = percent; progress(AppText.get(context, R.string.app_whisper_downloading, name, percent)) }
                }
            } }
            if (partial.length() != size || digest(partial) != hash) throw Failure(AppText.get(context, R.string.app_whisper_verify_failed))
            if (!partial.renameTo(target)) throw Failure(AppText.get(context, R.string.app_whisper_save_failed))
            return target
        } catch (failure: Failure) { throw failure }
        // Sem rede, DNS, tempo esgotado: o usuário vê "não deu para baixar", não "falha na transcrição".
        catch (network: IOException) { throw Failure(AppText.get(context, R.string.app_whisper_download_failed)) }
        finally { connection.disconnect(); partial.delete() }
    }
}
