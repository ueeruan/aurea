package com.aurea.aurea.diagnostics

import org.json.JSONArray
import org.json.JSONObject
import java.util.Locale

/**
 * A parte PURA dos relatórios de crash (testável na JVM): o relatório, o
 * registro do que já foi coletado, a limpeza de caminhos e o tombstone nativo
 * em texto. Quem fala com o Android é o [CrashReporter].
 *
 * O relatório leva só diagnóstico: versão, aparelho, motivo, etapa e pilha.
 * Caminho de mídia, URI e pasta de projeto são apagados da pilha antes de sair.
 */
data class CrashReport(
    val reportId: String,
    val appVersion: String,
    val appBuild: String,
    val os: String,
    val osVersion: String,
    val deviceModel: String,
    val manufacturer: String,
    val abi: String,
    val reason: String,
    val phase: String,
    val timestamp: Long,
    val stack: String,
    val stackTruncated: Boolean,
    val platform: String = "android",
) {
    /** O que fica na caixa de saída (sem instalação nem e-mail: esses entram no envio). */
    fun toJson(): JSONObject = JSONObject()
        .put("reportId", reportId).put("platform", platform)
        .put("appVersion", appVersion).put("appBuild", appBuild)
        .put("os", os).put("osVersion", osVersion)
        .put("deviceModel", deviceModel).put("manufacturer", manufacturer).put("abi", abi)
        .put("reason", reason).put("phase", phase).put("timestamp", timestamp)
        .put("stack", stack).put("stackTruncated", stackTruncated)

    /** O corpo do POST /api/crash. */
    fun paraEnvio(installId: String, email: String?): JSONObject = toJson().apply {
        put("installId", installId)
        if (!email.isNullOrBlank()) put("email", email)
    }

    companion object {
        const val PILHA_MAX_BYTES = 200 * 1024
        private val ID = Regex("^[A-Za-z0-9._:-]{8,100}$")

        fun fromJson(o: JSONObject): CrashReport? = runCatching {
            CrashReport(
                reportId = o.getString("reportId").also { require(ID.matches(it)) },
                appVersion = o.optString("appVersion"), appBuild = o.optString("appBuild"),
                os = o.optString("os"), osVersion = o.optString("osVersion"),
                deviceModel = o.optString("deviceModel"), manufacturer = o.optString("manufacturer"),
                abi = o.optString("abi"), reason = o.getString("reason"), phase = o.optString("phase"),
                timestamp = o.getLong("timestamp"), stack = o.optString("stack"),
                stackTruncated = o.optBoolean("stackTruncated"), platform = o.optString("platform", "android"),
            )
        }.getOrNull()

        /**
         * Os motivos do ApplicationExitInfo que viram relatório. LOW_MEMORY só
         * quando o app estava na frente (importância ≤ VISIBLE): o Android matar
         * um processo em cache no fundo é rotina, não "o app fechou sozinho".
         */
        fun motivoReportavel(reason: Int, importance: Int): String? = when (reason) {
            4 -> "CRASH"                        // REASON_CRASH (Java/Kotlin)
            5 -> "CRASH_NATIVE"                 // REASON_CRASH_NATIVE (C++ do motor, driver)
            6 -> "ANR"                          // REASON_ANR
            3 -> if (importance in 1..200) "LOW_MEMORY" else null   // REASON_LOW_MEMORY; 200 = IMPORTANCE_VISIBLE
            9 -> "EXCESSIVE_RESOURCE_USAGE"     // REASON_EXCESSIVE_RESOURCE_USAGE
            else -> null
        }
    }
}

/** Os ids já coletados (os mais novos no fim), para cada crash virar UM relatório. */
class CrashLedger private constructor(private val ids: ArrayDeque<String>) {
    fun contem(id: String): Boolean = id in ids

    fun marcar(id: String) {
        if (id in ids) return
        ids.addLast(id)
        while (ids.size > MAX) ids.removeFirst()
    }

    fun serializar(): String = JSONArray(ids.toList()).toString()

    val tamanho: Int get() = ids.size

    companion object {
        const val MAX = 200

        fun ler(json: String?): CrashLedger {
            val lista = runCatching {
                val a = JSONArray(json ?: "[]")
                List(a.length()) { a.getString(it) }
            }.getOrDefault(emptyList())
            return CrashLedger(ArrayDeque(lista.takeLast(MAX)))
        }
    }
}

object CrashTexto {
    private val URI = Regex("""\b(content|file)://[^\s'"<>)\]]+""")
    private val CAMINHO = Regex(
        """(/storage/|/sdcard/|/mnt/|/data/user/\d+/|/data/data/|/data/media/|/private/var/mobile/|/var/mobile/)[^\n'"]*?(?=:\s|['"]|\s\(|$)""",
        RegexOption.MULTILINE,
    )

    /** Apaga URIs e caminhos de dados do usuário; mantém o das bibliotecas (/data/app/...). */
    fun sanitizar(texto: String): String {
        val semUri = URI.replace(texto) { "${it.groupValues[1]}://<removido>" }
        return CAMINHO.replace(semUri) { "${it.groupValues[1]}<removido>" }
    }

    /** Corta em [max] bytes de UTF-8 sem partir caractere. (texto, cortou?) */
    fun truncarUtf8(texto: String, max: Int): Pair<String, Boolean> {
        val bytes = texto.toByteArray(Charsets.UTF_8)
        if (bytes.size <= max) return texto to false
        var fim = max
        while (fim > 0 && (bytes[fim].toInt() and 0xC0) == 0x80) fim--
        return String(bytes, 0, fim, Charsets.UTF_8) to true
    }

    /** Tombstone em TEXTO (Android 11): sem a seção de log (pode ter dados do app). */
    fun semLogDoTombstone(texto: String): String {
        val corte = Regex("""^--------- (?:tail end of )?log """, RegexOption.MULTILINE).find(texto)?.range?.first
        return if (corte != null) texto.substring(0, corte) + "[log do sistema omitido]\n" else texto
    }
}

/**
 * O tombstone nativo do Android 12+ (protobuf `tombstone.proto` do debuggerd)
 * em texto no formato do logcat: sinal, abort message, causa e a pilha da
 * thread que caiu, com rel_pc e BuildId — o suficiente para simbolizar com o
 * `libaurea.so` do build. Log, memória e mapas NÃO entram.
 */
object TombstoneTexto {
    private class Leitor(private val b: ByteArray, private var pos: Int, private val fim: Int) {
        val temMais: Boolean get() = pos < fim

        fun varint(): Long {
            var r = 0L
            var s = 0
            while (true) {
                require(pos < fim && s <= 63)
                val x = b[pos++].toInt() and 0xFF
                r = r or ((x and 0x7F).toLong() shl s)
                if ((x and 0x80) == 0) return r
                s += 7
            }
        }

        fun chave(): Pair<Int, Int> { val k = varint(); return (k ushr 3).toInt() to (k and 7).toInt() }

        fun sub(): Leitor {
            val n = varint()
            require(n >= 0 && n <= (fim - pos).toLong())
            val l = Leitor(b, pos, pos + n.toInt())
            pos += n.toInt()
            return l
        }

        fun texto(): String {
            val n = varint()
            require(n >= 0 && n <= (fim - pos).toLong())
            val s = String(b, pos, n.toInt(), Charsets.UTF_8)
            pos += n.toInt()
            return s
        }

        fun pular(tipo: Int) {
            when (tipo) {
                0 -> varint()
                1 -> pos += 8
                2 -> { val n = varint(); require(n >= 0 && n <= (fim - pos).toLong()); pos += n.toInt() }
                5 -> pos += 4
                else -> throw IllegalArgumentException("tipo $tipo")
            }
            require(pos <= fim)
        }
    }

    private data class Quadro(val relPc: Long, val funcao: String, val deslocamento: Long, val arquivo: String, val buildId: String)
    private data class Linha(val id: Int, val nome: String, val quadros: List<Quadro>, val notas: List<String>)

    fun decodificar(bytes: ByteArray, maxQuadros: Int = 64): String? = runCatching {
        val l = Leitor(bytes, 0, bytes.size)
        var impressao = ""
        var pid = 0L
        var tid = 0L
        val comando = mutableListOf<String>()
        var sinal = ""
        var abort = ""
        val causas = mutableListOf<String>()
        val linhas = mutableMapOf<Int, Linha>()
        while (l.temMais) {
            val (campo, tipo) = l.chave()
            when {
                campo == 2 && tipo == 2 -> impressao = l.texto()
                campo == 5 && tipo == 0 -> pid = l.varint()
                campo == 6 && tipo == 0 -> tid = l.varint()
                campo == 9 && tipo == 2 -> comando += l.texto()
                campo == 10 && tipo == 2 -> sinal = sinal(l.sub())
                campo == 14 && tipo == 2 -> abort = l.texto()
                campo == 15 && tipo == 2 -> causa(l.sub())?.let { causas += it }
                campo == 16 && tipo == 2 -> linha(l.sub(), maxQuadros)?.let { linhas[it.id] = it }
                else -> l.pular(tipo)
            }
        }
        buildString {
            if (impressao.isNotEmpty()) appendLine("Build fingerprint: '$impressao'")
            val caiu = linhas[tid.toInt()]
            appendLine("pid: $pid, tid: $tid, name: ${caiu?.nome ?: "?"}  >>> ${comando.joinToString(" ")} <<<")
            if (sinal.isNotEmpty()) appendLine(sinal)
            if (abort.isNotEmpty()) appendLine("Abort message: '$abort'")
            causas.forEach { appendLine("Cause: $it") }
            if (caiu != null) {
                caiu.notas.forEach { appendLine("  NOTE: $it") }
                appendLine()
                appendLine("backtrace:")
                caiu.quadros.forEachIndexed { i, q ->
                    append(String.format(Locale.ROOT, "      #%02d pc %016x  %s", i, q.relPc, q.arquivo))
                    if (q.funcao.isNotEmpty()) append(" (${q.funcao}+${q.deslocamento})")
                    if (q.buildId.isNotEmpty()) append(" (BuildId: ${q.buildId})")
                    appendLine()
                }
            } else {
                appendLine("(a thread que caiu não veio no tombstone)")
            }
        }
    }.getOrNull()

    private fun sinal(l: Leitor): String {
        var numero = 0
        var nome = ""
        var codigo = 0
        var nomeCodigo = ""
        var temEndereco = false
        var endereco = 0L
        while (l.temMais) {
            val (campo, tipo) = l.chave()
            when {
                campo == 1 && tipo == 0 -> numero = l.varint().toInt()
                campo == 2 && tipo == 2 -> nome = l.texto()
                campo == 3 && tipo == 0 -> codigo = l.varint().toInt()
                campo == 4 && tipo == 2 -> nomeCodigo = l.texto()
                campo == 8 && tipo == 0 -> temEndereco = l.varint() != 0L
                campo == 9 && tipo == 0 -> endereco = l.varint()
                else -> l.pular(tipo)
            }
        }
        val fim = if (temEndereco) String.format(Locale.ROOT, ", fault addr 0x%016x", endereco) else ", fault addr --------"
        return "signal $numero ($nome), code $codigo ($nomeCodigo)$fim"
    }

    private fun causa(l: Leitor): String? {
        while (l.temMais) {
            val (campo, tipo) = l.chave()
            if (campo == 1 && tipo == 2) return l.texto() else l.pular(tipo)
        }
        return null
    }

    /** Uma entrada do `map<uint32, Thread>`: chave 1, valor 2. */
    private fun linha(entrada: Leitor, maxQuadros: Int): Linha? {
        var valor: Leitor? = null
        while (entrada.temMais) {
            val (campo, tipo) = entrada.chave()
            if (campo == 2 && tipo == 2) valor = entrada.sub() else entrada.pular(tipo)
        }
        val l = valor ?: return null
        var id = 0
        var nome = ""
        val quadros = mutableListOf<Quadro>()
        val notas = mutableListOf<String>()
        while (l.temMais) {
            val (campo, tipo) = l.chave()
            when {
                campo == 1 && tipo == 0 -> id = l.varint().toInt()
                campo == 2 && tipo == 2 -> nome = l.texto()
                campo == 4 && tipo == 2 -> if (quadros.size < maxQuadros) quadros += quadro(l.sub()) else l.pular(tipo)
                campo == 7 && tipo == 2 -> notas += l.texto()
                else -> l.pular(tipo)
            }
        }
        return Linha(id, nome, quadros, notas)
    }

    private fun quadro(l: Leitor): Quadro {
        var relPc = 0L
        var funcao = ""
        var deslocamento = 0L
        var arquivo = ""
        var buildId = ""
        while (l.temMais) {
            val (campo, tipo) = l.chave()
            when {
                campo == 1 && tipo == 0 -> relPc = l.varint()
                campo == 4 && tipo == 2 -> funcao = l.texto()
                campo == 5 && tipo == 0 -> deslocamento = l.varint()
                campo == 6 && tipo == 2 -> arquivo = l.texto()
                campo == 8 && tipo == 2 -> buildId = l.texto()
                else -> l.pular(tipo)
            }
        }
        return Quadro(relPc, funcao, deslocamento, arquivo, buildId)
    }
}
