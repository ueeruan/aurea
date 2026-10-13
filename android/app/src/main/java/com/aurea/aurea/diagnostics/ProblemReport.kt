package com.aurea.aurea.diagnostics

import android.content.Context
import android.os.Build
import com.aurea.aurea.BuildConfig
import com.aurea.aurea.conta.ContaApi
import com.aurea.aurea.conta.SessaoGuardada
import org.json.JSONObject
import java.util.Locale
import java.util.UUID

/**
 * "Relatar um problema": o que a pessoa escreveu + a ficha do aparelho, para
 * o mesmo Worker dos crashes (POST /api/report, discovery/relato.js). Nada de
 * projeto, mídia ou caminho — só o texto e a ficha.
 */
object ProblemReport {
    const val WHAT_DID_MAX = 2000
    const val WHAT_HAPPENED_MAX = 4000
    const val STEPS_MAX = 4000

    enum class Result { SENT, INVALID, RATE_LIMITED, OFFLINE, FAILED }

    /** A ficha que acompanha o relato (também mostrada na folha antes de enviar). */
    fun deviceSummary(): String =
        "Aurea ${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE}) · Android ${Build.VERSION.RELEASE} · " +
            "${Build.MANUFACTURER} ${Build.MODEL}"

    fun body(installId: String, whatDid: String, whatHappened: String, steps: String): JSONObject = JSONObject()
        .put("reportId", "report-${System.currentTimeMillis()}-${UUID.randomUUID().toString().take(8)}")
        .put("installId", installId)
        .put("platform", "android")
        .put("appVersion", BuildConfig.VERSION_NAME)
        .put("appBuild", BuildConfig.VERSION_CODE.toString())
        .put("os", "Android")
        .put("osVersion", "${Build.VERSION.RELEASE} (SDK ${Build.VERSION.SDK_INT})")
        .put("deviceModel", Build.MODEL.orEmpty())
        .put("manufacturer", Build.MANUFACTURER.orEmpty())
        .put("abi", Build.SUPPORTED_ABIS.joinToString())
        .put("locale", Locale.getDefault().toLanguageTag())
        .put("whatDid", whatDid.trim().take(WHAT_DID_MAX))
        .put("whatHappened", whatHappened.trim().take(WHAT_HAPPENED_MAX))
        .put("steps", steps.trim().take(STEPS_MAX))

    private const val PREFS = "aurea_problem_report"
    private const val KEY_LAST_EXPORT = "last_export"
    const val LAST_EXPORT_MAX = 1200

    /** O chip do aparelho: SoC (API 31+) e a placa (`Build.HARDWARE`, ex.: mt6768, qcom). */
    fun chipset(): String {
        val soc = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
            listOf(Build.SOC_MANUFACTURER, Build.SOC_MODEL).filter { !it.isNullOrBlank() && it != Build.UNKNOWN }.joinToString(" ")
        else ""
        return listOf(soc, Build.HARDWARE.orEmpty()).filter { it.isNotBlank() }.joinToString(" / ").ifBlank { "?" }
    }

    /**
     * O diagnóstico de um export com problema (falha na largada ou no meio —
     * watchdog, verificação, encoder parado —, nova tentativa, HEVC trocado por
     * H.264). Pura (testada na JVM). `engine` = Engine::export_diagnostic
     * (codec e encoder abertos, tamanho/fps, onde travou, verificação).
     */
    fun exportDiagnosticLine(
        platform: String, device: String, chip: String, event: String, percent: Int, failure: Int, code: Int,
        safeMode: Int, softwareDecode: Boolean, retry: String, hevcNote: String?, engine: String, timestamp: String,
    ): String = buildString {
        append(timestamp).append(' ').append(platform).append(" · ").append(device).append(" · chip ").append(chip)
        append(" | ").append(event)
        append(" | parou em ").append(percent.coerceIn(0, 100)).append('%')
        append(" motivo=").append(failure).append(" codigo=").append(code)
        append(" seguranca=").append(safeMode).append(" decod=").append(if (softwareDecode) "software" else "padrao")
        append(" refez=").append(retry.ifBlank { "nao" })
        if (!hevcNote.isNullOrBlank()) append(" | hevc->h264: ").append(hevcNote)
        if (engine.isNotBlank()) append(" | motor: ").append(engine)
    }

    /**
     * A última falha de export (etapa, motivo, código, quadro, modo de
     * segurança, mensagem do motor) vai junto do próximo relato — "não consigo
     * exportar" sem detalhe nenhum era tudo o que chegava. Sem caminhos.
     */
    fun noteExport(context: Context, summary: String) {
        runCatching {
            context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString(KEY_LAST_EXPORT, sanitize(summary).take(LAST_EXPORT_MAX)).apply()
        }
    }

    fun lastExport(context: Context): String? =
        runCatching { context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY_LAST_EXPORT, null) }.getOrNull()

    /** Caminhos de arquivo saem do diagnóstico (nada de mídia ou pasta no relato). */
    fun sanitize(text: String): String = text.replace(Regex("""(/[^\s/]+){2,}/?"""), "<arquivo>")

    /** Os passos que a pessoa escreveu + a última falha de export, dentro do limite. */
    fun stepsWithExport(steps: String, lastExport: String?): String {
        if (lastExport.isNullOrBlank()) return steps
        val own = steps.trim()
        val prefix = if (own.isEmpty()) "" else own + "\n\n"
        return (prefix + "[export] " + lastExport).take(STEPS_MAX)
    }

    /**
     * Envia (IO). A sessão, quando há, dá ao relato o e-mail da conta.
     * `attachExport` = a chave "Anexar diagnóstico do último export" (ligada
     * por padrão); desligada, o relato vai sem ele.
     */
    fun send(context: Context, sessao: SessaoGuardada?, whatDid: String, whatHappened: String, steps: String,
             attachExport: Boolean = true): Result {
        val corpo = body(CrashReporter.installationId(context), whatDid, whatHappened,
            stepsWithExport(steps, if (attachExport) lastExport(context) else null))
        if (sessao != null) corpo.put("email", sessao.email)
        val r = ContaApi.relatar(corpo, sessao?.token)
        return when {
            r.ok -> Result.SENT
            r.status == 0 -> Result.OFFLINE
            r.status == 429 -> Result.RATE_LIMITED
            r.status == 400 || r.status == 413 -> Result.INVALID
            else -> Result.FAILED
        }
    }
}
