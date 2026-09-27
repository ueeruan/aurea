package com.aurea.aurea.diagnostics

import android.app.ActivityManager
import android.app.ApplicationExitInfo
import android.content.Context
import android.os.Build
import android.os.Process
import androidx.annotation.RequiresApi
import com.aurea.aurea.BuildConfig
import com.aurea.aurea.conta.ContaApi
import com.aurea.aurea.conta.SessaoGuardada
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL
import java.util.UUID
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.system.exitProcess

/**
 * Crash e fechamento anormal → relatório para o e-mail do desenvolvedor.
 *
 * De onde vem cada relatório:
 *  - Android 11+: o histórico do próprio sistema (`ApplicationExitInfo`, o
 *    mesmo do [ExitDiagnostics]) — CRASH, CRASH_NATIVE (tombstone), ANR,
 *    LOW_MEMORY com o app na frente e EXCESSIVE_RESOURCE_USAGE;
 *  - qualquer versão: a pilha Java/Kotlin, gravada pelo handler de exceção
 *    não tratada ANTES de o processo morrer (o sistema não guarda essa pilha);
 *  - Android 8–10: o marcador de "estava na frente" que sobrou da sessão
 *    anterior (o app morreu sem passar pelo onStop) vira ABNORMAL_EXIT.
 *
 * Cada relatório é coletado UMA vez (id no [CrashLedger]), vai para a caixa de
 * saída (`files/crash/outbox`) e sai no POST /api/crash quando há conta — a tela
 * de cadastro avisa desse envio antes. Enviado, sai da caixa; o servidor ainda
 * deduplica pelo id.
 */
object CrashReporter {
    private const val PREFS = "aurea_crash"
    private const val KEY_INSTALACAO = "install_id"
    private const val KEY_VISTOS = "vistos"
    private const val URL_CRASH = ContaApi.BASE + "/api/crash"
    private const val IDADE_MAX_MS = 7L * 24 * 3600_000
    private const val CAIXA_MAX = 20
    private const val ENVIOS_POR_VEZ = 5
    private const val TOMBSTONE_MAX = 4 * 1024 * 1024

    private val instalado = AtomicBoolean(false)
    private val lock = Any()

    private fun raiz(context: Context) = File(context.filesDir, "crash")
    private fun pastaJava(context: Context) = File(raiz(context), "java").apply { mkdirs() }
    private fun caixa(context: Context) = File(raiz(context), "outbox").apply { mkdirs() }
    private fun marcador(context: Context) = File(raiz(context), "aberto")
    private fun marcadorAnterior(context: Context) = File(raiz(context), "aberto.anterior")

    /**
     * A primeira linha do onCreate. Uma vez por processo: instala o handler e
     * guarda o marcador da sessão anterior (antes que o onStart escreva o novo).
     */
    fun instalar(context: Context) {
        if (!instalado.compareAndSet(false, true)) return
        val app = context.applicationContext
        runCatching {
            val anterior = marcador(app)
            if (anterior.exists()) anterior.renameTo(marcadorAnterior(app))
        }
        val dir = pastaJava(app)
        val proximo = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, erro ->
            // Rápido e sem lançar: o processo está morrendo.
            try {
                val agora = System.currentTimeMillis()
                val o = JSONObject()
                    .put("pid", Process.myPid()).put("ts", agora).put("thread", thread.name)
                    .put("phase", ExitDiagnostics.ultimaEtapa ?: "")
                    .put("stack", erro.stackTraceToString().take(CrashReport.PILHA_MAX_BYTES))
                File(dir, "java-$agora-${Process.myPid()}.json").writeText(o.toString())
            } catch (_: Throwable) {
            }
            if (proximo != null) proximo.uncaughtException(thread, erro)
            else { Process.killProcess(Process.myPid()); exitProcess(10) }
        }
    }

    /** Android 8–10: marca "na frente" no onStart e apaga no onStop. */
    fun primeiroPlano(context: Context, naFrente: Boolean) {
        if (Build.VERSION.SDK_INT >= 30) return
        runCatching {
            val f = marcador(context.applicationContext)
            if (naFrente) { f.parentFile?.mkdirs(); f.writeText(ExitDiagnostics.ultimaEtapa ?: "") } else f.delete()
        }
    }

    private fun prefs(context: Context) = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    private fun instalacao(context: Context): String {
        val p = prefs(context)
        p.getString(KEY_INSTALACAO, null)?.let { return it }
        val novo = UUID.randomUUID().toString()
        p.edit().putString(KEY_INSTALACAO, novo).apply()
        return novo
    }

    private fun relatorio(id: String, motivo: String, etapa: String, quando: Long, pilha: String): CrashReport {
        val (texto, cortou) = CrashTexto.truncarUtf8(CrashTexto.sanitizar(pilha), CrashReport.PILHA_MAX_BYTES)
        return CrashReport(
            reportId = id, appVersion = BuildConfig.VERSION_NAME, appBuild = BuildConfig.VERSION_CODE.toString(),
            os = "Android", osVersion = "${Build.VERSION.RELEASE} (SDK ${Build.VERSION.SDK_INT})",
            deviceModel = Build.MODEL.orEmpty(), manufacturer = Build.MANUFACTURER.orEmpty(),
            abi = Build.SUPPORTED_ABIS.joinToString(), reason = motivo, phase = CrashTexto.sanitizar(etapa),
            timestamp = quando, stack = texto, stackTruncated = cortou,
        )
    }

    private data class PilhaJava(val arquivo: File, val pid: Int, val ts: Long, val thread: String, val etapa: String, val pilha: String)

    /** Na abertura, em IO: junta os relatórios novos na caixa de saída. */
    fun coletar(context: Context) = synchronized(lock) {
        val app = context.applicationContext
        val vistos = CrashLedger.ler(prefs(app).getString(KEY_VISTOS, null))
        val javas = pastaJava(app).listFiles { f -> f.name.endsWith(".json") }.orEmpty().mapNotNull { f ->
            runCatching {
                val o = JSONObject(f.readText())
                PilhaJava(f, o.getInt("pid"), o.getLong("ts"), o.optString("thread"), o.optString("phase"), o.optString("stack"))
            }.getOrElse { f.delete(); null }
        }
        val usadas = mutableSetOf<File>()
        val agora = System.currentTimeMillis()

        if (Build.VERSION.SDK_INT >= 30) {
            val saidas = runCatching {
                app.getSystemService(ActivityManager::class.java)
                    ?.getHistoricalProcessExitReasons(app.packageName, 0, 16).orEmpty()
            }.getOrDefault(emptyList())
            for (exit in saidas) {
                val motivo = CrashReport.motivoReportavel(exit.reason, exit.importance) ?: continue
                if (agora - exit.timestamp > IDADE_MAX_MS) continue
                val id = "exit-${exit.timestamp}-${exit.pid}"
                if (vistos.contem(id)) continue
                val java = if (exit.reason == ApplicationExitInfo.REASON_CRASH) javas.firstOrNull { it.pid == exit.pid } else null
                if (java != null) usadas += java.arquivo
                val etapa = exit.processStateSummary?.toString(Charsets.UTF_8) ?: java?.etapa.orEmpty()
                gravar(app, relatorio(id, motivo, etapa, exit.timestamp, pilhaDaSaida(exit, motivo, java)))
                vistos.marcar(id)
            }
        }
        for (j in javas) {
            if (j.arquivo in usadas) continue
            val id = "java-${j.ts}-${j.pid}"
            if (!vistos.contem(id)) {
                gravar(app, relatorio(id, "CRASH", j.etapa, j.ts, "thread: ${j.thread}\n\n${j.pilha}"))
                vistos.marcar(id)
            }
        }
        javas.forEach { it.arquivo.delete() }

        // Android 8–10: morreu na frente sem crash Java registrado.
        val anterior = marcadorAnterior(app)
        if (anterior.exists()) {
            if (Build.VERSION.SDK_INT < 30 && javas.isEmpty()) {
                val quando = anterior.lastModified().takeIf { it > 0 } ?: agora
                val id = "abnormal-$quando"
                if (!vistos.contem(id)) {
                    val etapa = runCatching { anterior.readText() }.getOrDefault("")
                    gravar(app, relatorio(id, "ABNORMAL_EXIT", etapa, quando,
                        "O app fechou com a tela aberta sem passar pelo onStop (crash nativo, falta de memória ou o sistema o encerrou). " +
                            "O Android ${Build.VERSION.RELEASE} não guarda o motivo."))
                    vistos.marcar(id)
                }
            }
            anterior.delete()
        }
        prefs(app).edit().putString(KEY_VISTOS, vistos.serializar()).apply()

        // A caixa não cresce sem fim (aparelho sem conta ou sem rede por meses).
        caixa(app).listFiles().orEmpty().sortedByDescending { it.lastModified() }.drop(CAIXA_MAX).forEach { it.delete() }
    }

    private fun gravar(context: Context, r: CrashReport) {
        runCatching {
            val destino = File(caixa(context), "${r.reportId}.json")
            val temp = File(caixa(context), "${r.reportId}.part")
            temp.writeText(r.toJson().toString())
            if (!temp.renameTo(destino)) temp.delete()
        }
    }

    @RequiresApi(30)
    private fun pilhaDaSaida(exit: ApplicationExitInfo, motivo: String, java: PilhaJava?): String = buildString {
        appendLine("motivo=$motivo (${exit.reason}) status/sinal=${exit.status} importância=${exit.importance}")
        appendLine("PSS=${exit.pss} KB RSS=${exit.rss} KB")
        exit.description?.let { appendLine("descrição: $it") }
        java?.let { appendLine("thread: ${it.thread}") }
        appendLine()
        when {
            java != null -> append(java.pilha)
            exit.reason == ApplicationExitInfo.REASON_CRASH -> append("(a pilha Java não foi gravada: o crash aconteceu antes do handler ou em outro processo)")
            exit.reason == ApplicationExitInfo.REASON_CRASH_NATIVE -> append(tombstone(exit))
            exit.reason == ApplicationExitInfo.REASON_ANR -> append(textoDoRastro(exit) ?: "(rastro do ANR indisponível)")
            else -> append("(o Android não guarda pilha para este motivo)")
        }
    }

    @RequiresApi(30)
    private fun tombstone(exit: ApplicationExitInfo): String {
        if (Build.VERSION.SDK_INT >= 31) {
            val bytes = runCatching { exit.traceInputStream?.use { ExitDiagnostics.readBounded(it, TOMBSTONE_MAX) } }.getOrNull()
                ?: return "(tombstone indisponível ou acima de 4 MB)"
            return TombstoneTexto.decodificar(bytes) ?: "(tombstone protobuf ilegível: ${bytes.size} bytes)"
        }
        return textoDoRastro(exit)?.let { CrashTexto.semLogDoTombstone(it) } ?: "(tombstone indisponível)"
    }

    /** Rastro em texto (ANR; tombstone do Android 11): os primeiros 200 KiB bastam. */
    @RequiresApi(30)
    private fun textoDoRastro(exit: ApplicationExitInfo): String? = runCatching {
        exit.traceInputStream?.use { lerInicio(it, CrashReport.PILHA_MAX_BYTES + 1) }?.toString(Charsets.UTF_8)
    }.getOrNull()

    private fun lerInicio(entrada: InputStream, limite: Int): ByteArray {
        val saida = ByteArrayOutputStream()
        val buffer = ByteArray(8192)
        while (saida.size() < limite) {
            val n = entrada.read(buffer, 0, minOf(buffer.size, limite - saida.size()))
            if (n < 0) break
            saida.write(buffer, 0, n)
        }
        return saida.toByteArray()
    }

    /**
     * Envia a caixa de saída (IO). Só com sessão: é o cadastro que avisa do envio.
     * 2xx → sai da caixa; 400/413 → descartado (repetir não adianta); 429 ou
     * sem rede → fica para a próxima abertura.
     */
    fun enviar(context: Context, sessao: SessaoGuardada?) = synchronized(lock) {
        if (sessao == null) return@synchronized
        val app = context.applicationContext
        val instalacao = instalacao(app)
        val pendentes = caixa(app).listFiles { f -> f.name.endsWith(".json") }.orEmpty().sortedBy { it.name }.take(ENVIOS_POR_VEZ)
        for (arquivo in pendentes) {
            val r = runCatching { CrashReport.fromJson(JSONObject(arquivo.readText())) }.getOrNull()
            if (r == null) { arquivo.delete(); continue }
            when (val status = postar(r.paraEnvio(instalacao, sessao.email), sessao.token)) {
                in 200..299, 400, 413 -> arquivo.delete()
                429 -> return@synchronized
                else -> if (status == 0) return@synchronized
            }
        }
    }

    private fun postar(corpo: JSONObject, token: String): Int {
        var conexao: HttpURLConnection? = null
        return try {
            val bytes = corpo.toString().toByteArray(Charsets.UTF_8)
            conexao = (URL(URL_CRASH).openConnection() as HttpURLConnection).apply {
                requestMethod = "POST"
                connectTimeout = 12_000
                readTimeout = 30_000
                doOutput = true
                setRequestProperty("Content-Type", "application/json; charset=utf-8")
                setRequestProperty("Authorization", "Bearer $token")
                setFixedLengthStreamingMode(bytes.size)
            }
            conexao.outputStream.use { it.write(bytes) }
            val status = conexao.responseCode
            runCatching { (if (status in 200..299) conexao.inputStream else conexao.errorStream)?.use { it.readBytes() } }
            status
        } catch (_: IOException) {
            0
        } catch (_: SecurityException) {
            0
        } finally {
            conexao?.disconnect()
        }
    }
}
