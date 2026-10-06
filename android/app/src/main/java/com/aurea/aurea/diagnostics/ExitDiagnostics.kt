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
    // PROJECT_*: etapas em que o motor lê/renderiza um projeto inteiro (abrir,
    // importar o .aureaproj, exportar o vídeo, gravar o arquivo do projeto) —
    // o crash que chega por e-mail diz em qual delas o app morreu.
    enum class Phase {
        ENGINE_START, ENGINE_READY, VIDEO_PERMISSION, VIDEO_NATIVE, VIDEO_READY, VIDEO_FAILED, VIDEO_CANCELLED,
        PROJECT_OPEN, PROJECT_IMPORT, PROJECT_EXPORT_VIDEO, PROJECT_EXPORT_FILE,
    }

    /** A última etapa marcada, em memória: o handler de crash Java a grava junto da pilha. */
    @Volatile var ultimaEtapa: String? = null
        private set

    /**
     * `detail`: curto e sem dado do usuário (ex.: "f=1234/2000", o quadro do
     * export). Vai no marcador do SO (≤ 128 bytes) e na pilha Java.
     */
    fun mark(context: Context, phase: Phase, detail: String = "") {
        val extra = cleanDetail(detail)
        ultimaEtapa = "Aurea build=${BuildConfig.VERSION_CODE} phase=${phase.name}" + (if (extra.isEmpty()) "" else " $extra")
        if (Build.VERSION.SDK_INT < 30) return
        runCatching {
            context.getSystemService(ActivityManager::class.java)?.setProcessStateSummary(
                marker(phase, BuildConfig.VERSION_CODE, SystemClock.elapsedRealtime(), extra)
            )
        } // An OEM refusing/throttling diagnostics must never break import.
    }

    /** Só [A-Za-z0-9=/._-], até 24 caracteres: nunca nome, caminho ou URI. */
    internal fun cleanDetail(detail: String): String =
        detail.filter { it.isLetterOrDigit() && it.code < 128 || it in "=/._-" }.take(24)

    private const val PREFS = "aurea_exit_diagnostics"
    private const val SAFE_VIDEO = "safe_video_planes"          // antigo (permanente): só é apagado
    private const val SAFE_VIDEO_BUILD = "safe_video_planes_build"

    /** Janela depois de importar em que um crash ainda conta como "do vídeo". */
    internal const val VIDEO_READY_WINDOW_MS = 20_000L

    /**
     * Modo seguro de vídeo (planos YUV pela CPU) NESTE build: ligado quando o
     * processo anterior, do mesmo build, morreu por crash durante a importação
     * de um vídeo ou logo depois dela (até [VIDEO_READY_WINDOW_MS]). Antes era
     * permanente e contava qualquer crash depois de importar — um crash da
     * superfície ou do 3D prendia o aparelho nos planos da CPU para sempre, e
     * lá um decoder de hardware que não entrega planos deixava o vídeo preto.
     * Um build novo reavalia do zero.
     */
    fun safeVideoMode(context: Context): Boolean {
        if (Build.VERSION.SDK_INT < 30) return false
        val prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        if (prefs.contains(SAFE_VIDEO)) prefs.edit().remove(SAFE_VIDEO).apply()
        val build = BuildConfig.VERSION_CODE
        if (prefs.getInt(SAFE_VIDEO_BUILD, 0) == build) return true
        val crashed = runCatching {
            exits(context).firstOrNull()?.let { exit ->
                val bootWallMs = System.currentTimeMillis() - SystemClock.elapsedRealtime()
                val crashElapsedMs = (exit.timestamp - bootWallMs).takeIf { it > 0 }
                crashedDuringVideo(exit.reason, exit.processStateSummary?.toString(Charsets.UTF_8), build, crashElapsedMs)
            } ?: false
        }.getOrDefault(false)
        if (crashed) prefs.edit().putInt(SAFE_VIDEO_BUILD, build).apply()
        return crashed
    }

    /**
     * O encerramento foi um crash do vídeo neste build? `crashElapsedMs` é o
     * relógio desde o boot na hora do crash (null = desconhecido): com ele, um
     * crash em VIDEO_READY só conta se veio logo depois de importar.
     */
    internal fun crashedDuringVideo(reason: Int, summary: String?, build: Int, crashElapsedMs: Long?): Boolean {
        if (summary == null) return false
        val crash = reason == ApplicationExitInfo.REASON_CRASH_NATIVE || reason == ApplicationExitInfo.REASON_CRASH
        if (!crash || !summary.contains("build=$build ")) return false
        if (summary.contains("phase=${Phase.VIDEO_NATIVE.name} ")) return true
        if (!summary.contains("phase=${Phase.VIDEO_READY.name} ")) return false
        val markedAt = summary.substringAfter("uptimeMs=", "").trim().toLongOrNull() ?: return false
        val after = (crashElapsedMs ?: return false) - markedAt
        return after in 0..VIDEO_READY_WINDOW_MS
    }

    // O detalhe vai ANTES de uptimeMs: crashedDuringVideo lê o número do fim.
    internal fun marker(phase: Phase, build: Int, uptimeMs: Long, detail: String = ""): ByteArray {
        val extra = cleanDetail(detail)
        return ("Aurea build=$build phase=${phase.name} " + (if (extra.isEmpty()) "" else "$extra ") + "uptimeMs=$uptimeMs")
            .toByteArray(Charsets.UTF_8)
    }

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
        appendLine(ProjectGuard.summary(context))
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
