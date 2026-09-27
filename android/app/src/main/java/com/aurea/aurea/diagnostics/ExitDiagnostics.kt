package com.aurea.aurea.diagnostics

import android.app.ActivityManager
import android.app.ApplicationExitInfo
import android.content.Context
import android.media.MediaCodecList
import android.os.Build
import android.os.SystemClock
import com.aurea.aurea.BuildConfig
import java.io.InputStream
import java.io.OutputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

/** Local diagnostics; exported only when the user chooses a destination. */
object ExitDiagnostics {
    // No URI, project name or media contents in the OS process-state marker.
    enum class Phase { ENGINE_START, ENGINE_READY, VIDEO_PERMISSION, VIDEO_NATIVE, VIDEO_READY, VIDEO_FAILED, VIDEO_CANCELLED }

    fun mark(context: Context, phase: Phase) {
        if (Build.VERSION.SDK_INT < 30) return
        runCatching {
            context.getSystemService(ActivityManager::class.java)?.setProcessStateSummary(
                marker(phase, BuildConfig.VERSION_CODE, SystemClock.elapsedRealtime())
            )
        } // An OEM refusing/throttling diagnostics must never break import.
    }

    private const val PREFS = "aurea_exit_diagnostics"
    private const val SAFE_VIDEO = "safe_video_planes"

    /**
     * Modo seguro de vídeo, permanente no aparelho: ligado quando o processo
     * anterior morreu por crash nativo (ou do app) numa etapa de vídeo — dentro
     * da importação ou depois dela, quando o decoder e a GPU já trabalham. O
     * custo é só desempenho (planos YUV pela CPU); o ganho é o app não fechar
     * de novo a cada vídeo num aparelho cujo driver/gralloc ainda não conhecemos.
     */
    fun safeVideoMode(context: Context): Boolean {
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        if (prefs.getBoolean(SAFE_VIDEO, false)) return true
        val crashed = runCatching {
            exits(context).firstOrNull()?.let { exit ->
                crashedDuringVideo(exit.reason, exit.processStateSummary?.toString(Charsets.UTF_8))
            } ?: false
        }.getOrDefault(false)
        if (crashed) prefs.edit().putBoolean(SAFE_VIDEO, true).apply()
        return crashed
    }

    internal fun crashedDuringVideo(reason: Int, summary: String?): Boolean {
        if (summary == null) return false
        val crash = reason == ApplicationExitInfo.REASON_CRASH_NATIVE || reason == ApplicationExitInfo.REASON_CRASH
        return crash && (summary.contains("phase=${Phase.VIDEO_NATIVE.name}") || summary.contains("phase=${Phase.VIDEO_READY.name}"))
    }

    internal fun marker(phase: Phase, build: Int, uptimeMs: Long): ByteArray =
        "Aurea build=$build phase=${phase.name} uptimeMs=$uptimeMs".toByteArray(Charsets.UTF_8)

    private fun exits(context: Context): List<ApplicationExitInfo> {
        if (Build.VERSION.SDK_INT < 30) return emptyList()
        return context.getSystemService(ActivityManager::class.java)
            ?.getHistoricalProcessExitReasons(context.packageName, 0, 8).orEmpty()
    }

    /** Call on IO: queries OS history, not just exceptions caught by Kotlin. */
    fun report(context: Context): String = buildString {
        appendLine("AUREA — DIAGNÓSTICO DE FECHAMENTOS")
        appendLine("build atual: ${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE})")
        appendLine("aparelho: ${Build.MANUFACTURER} ${Build.MODEL}")
        appendLine("device=${Build.DEVICE} hardware=${Build.HARDWARE}")
        appendLine("Android ${Build.VERSION.RELEASE}, SDK ${Build.VERSION.SDK_INT}, firmware=${Build.DISPLAY}")
        appendLine("ABI: ${Build.SUPPORTED_ABIS.joinToString()}")
        appendLine("Samsung (todas as versoes): " +
            if (Build.MANUFACTURER.equals("samsung", ignoreCase = true))
                "proteção de planos YUV ativa; confirmação no aparelho pendente" else "não se aplica")
        appendLine("Decodificadores disponíveis (não necessariamente em uso):")
        runCatching {
            MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos.filter { !it.isEncoder }.forEach { codec ->
                val types = codec.supportedTypes.filter { it.startsWith("video/") }
                if (types.isNotEmpty()) appendLine("  ${codec.name}: ${types.joinToString()}")
            }
        }.onFailure { appendLine("  sondagem indisponível: ${it.javaClass.simpleName}") }
        appendLine()
        if (Build.VERSION.SDK_INT < 30) {
            appendLine("O Android oferece o histórico de encerramentos a partir do Android 11.")
            return@buildString
        }
        runCatching {
            val entries = exits(context)
            if (entries.isEmpty()) appendLine("Nenhum encerramento disponível no histórico do Android.")
            entries.forEach { exit ->
                appendLine("timestampMs=${exit.timestamp} process=${exit.processName}")
                appendLine("  reason=${reason(exit.reason)} (${exit.reason}) status/signal=${exit.status}")
                appendLine("  PSS=${exit.pss} KB RSS=${exit.rss} KB (última amostra; 0 pode significar não medido)")
                appendLine("  etapa: ${exit.processStateSummary?.toString(Charsets.UTF_8) ?: "não registrada por esse build"}")
            }
            appendLine("Encerramento pelo usuário ou atualização não significa falha. O histórico pode incluir builds anteriores.")
        }.onFailure { appendLine("Histórico indisponível: ${it.javaClass.simpleName}") }
    }

    /** API 31 supplies native crash traces as protobuf, not UTF-8 text. */
    fun writeArchive(context: Context, output: OutputStream) {
        ZipOutputStream(output).use { zip ->
            fun entry(name: String, bytes: ByteArray) {
                zip.putNextEntry(ZipEntry(name))
                zip.write(bytes)
                zip.closeEntry()
            }
            entry("diagnostico.txt", report(context).toByteArray(Charsets.UTF_8))
            val notes = StringBuilder()
            if (Build.VERSION.SDK_INT >= 30) {
                val history = runCatching { exits(context) }.onFailure {
                    notes.appendLine("Histórico indisponível: ${it.javaClass.simpleName}")
                }.getOrDefault(emptyList())
                history.filter {
                    it.reason == ApplicationExitInfo.REASON_ANR || it.reason == ApplicationExitInfo.REASON_CRASH_NATIVE
                }.take(4).forEach { exit ->
                    val name = "trace-${exit.timestamp}-${exit.pid}"
                    val bytes = runCatching { exit.traceInputStream?.use { readBounded(it, 2 * 1024 * 1024) } }
                    val data = bytes.getOrNull()
                    if (data != null) {
                        val extension = if (exit.reason == ApplicationExitInfo.REASON_CRASH_NATIVE && Build.VERSION.SDK_INT >= 31) "pb" else "txt"
                        entry("$name.$extension", data)
                    } else {
                        notes.appendLine("$name: rastro indisponível, removido pelo Android ou acima do limite de 2 MB.")
                    }
                }
            }
            entry("leia-me.txt", ("Rastros .pb são tombstones nativos do Android em protobuf.\n" +
                "Sem rastro não é possível identificar a instrução que falhou.\n" + notes).toByteArray(Charsets.UTF_8))
        }
    }

    // Never create a truncated protobuf or allocate an unbounded OEM trace.
    internal fun readBounded(input: InputStream, limit: Int): ByteArray? {
        require(limit >= 0)
        val output = java.io.ByteArrayOutputStream()
        val buffer = ByteArray(8192)
        while (true) {
            val count = input.read(buffer, 0, minOf(buffer.size.toLong(), limit.toLong() - output.size() + 1).toInt())
            if (count == -1) return output.toByteArray()
            if (count == 0) {
                val byte = input.read()
                if (byte == -1) return output.toByteArray()
                if (output.size() == limit) return null
                output.write(byte)
            } else {
                if (count > limit - output.size()) return null
                output.write(buffer, 0, count)
            }
        }
    }

    private fun reason(reason: Int): String = when (reason) {
        ApplicationExitInfo.REASON_CRASH -> "CRASH_JAVA"
        ApplicationExitInfo.REASON_CRASH_NATIVE -> "CRASH_NATIVE"
        ApplicationExitInfo.REASON_ANR -> "ANR"
        ApplicationExitInfo.REASON_LOW_MEMORY -> "LOW_MEMORY"
        ApplicationExitInfo.REASON_SIGNALED -> "SIGNALED"
        ApplicationExitInfo.REASON_USER_REQUESTED -> "USER_REQUESTED"
        ApplicationExitInfo.REASON_USER_STOPPED -> "USER_STOPPED"
        ApplicationExitInfo.REASON_EXIT_SELF -> "EXIT_SELF"
        ApplicationExitInfo.REASON_INITIALIZATION_FAILURE -> "INITIALIZATION_FAILURE"
        ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE -> "EXCESSIVE_RESOURCE_USAGE"
        ApplicationExitInfo.REASON_DEPENDENCY_DIED -> "DEPENDENCY_DIED"
        ApplicationExitInfo.REASON_PERMISSION_CHANGE -> "PERMISSION_CHANGE"
        ApplicationExitInfo.REASON_OTHER -> "OTHER"
        else -> "UNKNOWN"
    }
}
