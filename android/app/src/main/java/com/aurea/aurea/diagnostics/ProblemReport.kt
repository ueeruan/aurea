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
    private const val LAST_EXPORT_MAX = 600

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

    /** Envia (IO). A sessão, quando há, dá ao relato o e-mail da conta. */
    fun send(context: Context, sessao: SessaoGuardada?, whatDid: String, whatHappened: String, steps: String): Result {
        val corpo = body(CrashReporter.installationId(context), whatDid, whatHappened, stepsWithExport(steps, lastExport(context)))
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
