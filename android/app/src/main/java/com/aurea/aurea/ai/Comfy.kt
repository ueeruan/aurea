package com.aurea.aurea.ai

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.SocketTimeoutException
import java.net.URL
import java.net.URLEncoder
import java.util.UUID
import kotlin.math.ceil
import kotlin.math.max
import kotlin.math.roundToInt
import kotlin.math.sqrt

/**
 * A ÚNICA configuração de endereço da Aurea AI.
 *
 * Não há endereço de servidor compilado. Nenhum. O app conhece só o endereço
 * FIXO do discovery ([DISCOVERY_URL]) e de lá recebe o `endpoint` do momento.
 *
 * É isso que faz o túnel do Colab poder mudar de nome sem APK novo e sem IPA
 * novo: quem conta o endereço novo é o discovery, não o binário.
 */
object AureaAiConfig {
    /** Endereço FIXO do discovery. Este nunca muda — é o que dispensa recompilar. */
    const val DISCOVERY_URL = "https://aurea-ai-discovery.aureaapp.workers.dev/server"

    /** Workflow do MiniMax H3 no formato de API do ComfyUI (assets/ai/). */
    const val WORKFLOW_ASSET = "ai/minimax_h3_api.json"
}

/**
 * Erro REAL do ComfyUI, para a tela mostrar o que aconteceu (e não "Erro ao
 * gerar vídeo"): `400 no_prompt`, `405`, `530`, `node_errors`, `timeout`…
 */
class ComfyErro(val http: Int, val codigo: String, val detalhe: String) :
    IOException("$codigo: $detalhe") {

    /** A frase que vai para a tela. */
    fun paraTela(): String = when {
        codigo == "timeout" -> "timeout: o servidor não respondeu"
        codigo == "sem_conexao" -> "sem conexão: $detalhe"
        http == 530 -> "530: Cloudflare Tunnel indisponível"
        http == 405 -> "405: método/endpoint errado ($detalhe)"
        codigo == "node_errors" -> "node_errors: $detalhe"
        http > 0 -> "$http $codigo: $detalhe"
        else -> "$codigo: $detalhe"
    }
}

/** Uma saída de arquivo do ComfyUI (o que o `/view` recebe). */
data class ComfyArquivo(val nome: String, val subpasta: String, val tipo: String)

/**
 * A API do ComfyUI, direto: `/system_stats`, `POST /prompt`, `/history/{id}`,
 * `/queue`, `/upload/image`, `/view`, `/interrupt`. Nada de `/generate` nem `/v1`.
 */
class ComfyCliente(val base: String) {

    val clientId: String = UUID.randomUUID().toString()

    /** `GET /system_stats`: 200 + JSON com "system" = servidor de pé. */
    fun estaOnline(): Pair<Int, Boolean> {
        return try {
            val (http, corpo) = pedir("GET", "/system_stats", timeoutMs = 10_000)
            http to (http == 200 && runCatching { JSONObject(String(corpo, Charsets.UTF_8)).has("system") }.getOrDefault(false))
        } catch (e: ComfyErro) {
            e.http to false
        }
    }

    /** `POST /prompt` com `{"prompt": WORKFLOW, "client_id"}`. Devolve o prompt_id. */
    fun enviarPrompt(workflow: JSONObject): String {
        val corpo = JSONObject().put("prompt", workflow).put("client_id", clientId).toString()
        val (_, bruto) = pedir("POST", "/prompt", corpo.toByteArray(Charsets.UTF_8), "application/json", timeoutMs = 60_000)
        val o = JSONObject(String(bruto, Charsets.UTF_8))
        val erros = o.optJSONObject("node_errors")
        if (erros != null && erros.length() > 0) throw ComfyErro(200, "node_errors", resumirNodeErrors(erros))
        val id = o.optString("prompt_id")
        if (id.isBlank()) throw ComfyErro(200, "sem_prompt_id", String(bruto, Charsets.UTF_8).take(300))
        return id
    }

    /** `GET /history/{id}`: o registro do job, ou nulo enquanto não terminou. */
    fun historico(promptId: String): JSONObject? {
        val (_, bruto) = pedir("GET", "/history/" + URLEncoder.encode(promptId, "UTF-8"))
        val o = JSONObject(String(bruto, Charsets.UTF_8))
        return o.optJSONObject(promptId)
    }

    /** `GET /queue`: (rodando agora?, posição na fila de espera; 0 = não está esperando). */
    fun situacaoNaFila(promptId: String): Pair<Boolean, Int> {
        val (_, bruto) = pedir("GET", "/queue")
        val o = JSONObject(String(bruto, Charsets.UTF_8))
        fun temId(a: JSONArray?, i: Int) = a?.optJSONArray(i)?.optString(1) == promptId
        val rodando = o.optJSONArray("queue_running")
        for (i in 0 until (rodando?.length() ?: 0)) if (temId(rodando, i)) return true to 0
        val espera = o.optJSONArray("queue_pending")
        // A fila de espera vem na ordem de chegada pelo número (índice 0 do item).
        val itens = (0 until (espera?.length() ?: 0)).mapNotNull { espera?.optJSONArray(it) }
            .sortedBy { it.optDouble(0) }
        val pos = itens.indexOfFirst { it.optString(1) == promptId }
        return false to (if (pos >= 0) pos + 1 else 0)
    }

    /** `POST /upload/image` (multipart, campo "image"). Devolve o valor para o LoadImage. */
    fun subirImagem(bytes: ByteArray, tipo: String): String {
        val fronteira = "----aurea${UUID.randomUUID().toString().replace("-", "")}"
        val ext = when (tipo) { "image/png" -> "png"; "image/webp" -> "webp"; else -> "jpg" }
        val nome = "aurea_${UUID.randomUUID().toString().take(8)}.$ext"
        val cab = ("--$fronteira\r\nContent-Disposition: form-data; name=\"image\"; filename=\"$nome\"\r\n" +
            "Content-Type: $tipo\r\n\r\n").toByteArray(Charsets.UTF_8)
        val tipoCampo = "\r\n--$fronteira\r\nContent-Disposition: form-data; name=\"type\"\r\n\r\ninput" +
            "\r\n--$fronteira\r\nContent-Disposition: form-data; name=\"overwrite\"\r\n\r\ntrue\r\n--$fronteira--\r\n"
        val corpo = cab + bytes + tipoCampo.toByteArray(Charsets.UTF_8)
        val (_, bruto) = pedir("POST", "/upload/image", corpo, "multipart/form-data; boundary=$fronteira", timeoutMs = 120_000)
        val o = JSONObject(String(bruto, Charsets.UTF_8))
        val n = o.optString("name")
        if (n.isBlank()) throw ComfyErro(200, "upload", String(bruto, Charsets.UTF_8).take(300))
        val sub = o.optString("subfolder")
        return if (sub.isBlank()) n else "$sub/$n"
    }

    /** `GET /view?filename&subfolder&type` → arquivo local. */
    fun baixar(arquivo: ComfyArquivo, destino: File): File {
        val q = "filename=" + URLEncoder.encode(arquivo.nome, "UTF-8") +
            "&subfolder=" + URLEncoder.encode(arquivo.subpasta, "UTF-8") +
            "&type=" + URLEncoder.encode(arquivo.tipo, "UTF-8")
        val (_, bruto) = pedir("GET", "/view?$q", timeoutMs = 300_000)
        if (bruto.isEmpty()) throw ComfyErro(200, "view_vazio", "o /view devolveu 0 bytes")
        destino.parentFile?.mkdirs()
        destino.writeBytes(bruto)
        return destino
    }

    /** Cancela: tira da fila de espera e, se já estiver rodando, interrompe. */
    fun cancelar(promptId: String) {
        runCatching {
            pedir("POST", "/queue", JSONObject().put("delete", JSONArray().put(promptId)).toString()
                .toByteArray(Charsets.UTF_8), "application/json")
        }
        runCatching { pedir("POST", "/interrupt", "{}".toByteArray(Charsets.UTF_8), "application/json") }
    }

    // -- transporte -----------------------------------------------------------

    private fun pedir(
        metodo: String,
        caminho: String,
        corpo: ByteArray? = null,
        tipoConteudo: String = "application/json",
        timeoutMs: Int = 30_000,
    ): Pair<Int, ByteArray> {
        val conn = try {
            URL(base + caminho).openConnection() as HttpURLConnection
        } catch (e: Exception) {
            throw ComfyErro(0, "sem_conexao", e.message ?: "URL inválida")
        }
        try {
            conn.requestMethod = metodo
            conn.connectTimeout = 15_000
            conn.readTimeout = timeoutMs
            conn.useCaches = false
            conn.setRequestProperty("Accept", "*/*")
            // A Cloudflare do Worker recusa o UA padrao com 403 (1010).
            conn.setRequestProperty("User-Agent", "Aurea/2.0 (Android)")
            if (corpo != null) {
                conn.doOutput = true
                conn.setRequestProperty("Content-Type", tipoConteudo)
                conn.setFixedLengthStreamingMode(corpo.size)
                conn.outputStream.use { it.write(corpo) }
            }
            val http = conn.responseCode
            val fluxo = if (http in 200..299) conn.inputStream else conn.errorStream
            val bruto = fluxo?.use { it.readBytes() } ?: ByteArray(0)
            if (http !in 200..299) throw traduzir(http, bruto)
            return http to bruto
        } catch (e: ComfyErro) {
            throw e
        } catch (e: SocketTimeoutException) {
            throw ComfyErro(0, "timeout", "$metodo $caminho")
        } catch (e: IOException) {
            throw ComfyErro(0, "sem_conexao", e.message ?: "falha de rede")
        } finally {
            conn.disconnect()
        }
    }

    /** O corpo de erro do ComfyUI: `{"error": {"type", "message", "details"}, "node_errors": {...}}`. */
    private fun traduzir(http: Int, bruto: ByteArray): ComfyErro {
        val texto = String(bruto, Charsets.UTF_8)
        return try {
            val o = JSONObject(texto)
            val erros = o.optJSONObject("node_errors")
            if (erros != null && erros.length() > 0) return ComfyErro(http, "node_errors", resumirNodeErrors(erros))
            val e = o.optJSONObject("error")
            if (e != null) {
                val msg = listOf(e.optString("message"), e.optString("details")).filter { it.isNotBlank() }.joinToString(" — ")
                ComfyErro(http, e.optString("type").ifBlank { "erro" }, msg.ifBlank { texto.take(200) })
            } else {
                ComfyErro(http, "erro", texto.take(200))
            }
        } catch (_: Exception) {
            ComfyErro(http, "erro", texto.take(200).ifBlank { "HTTP $http" })
        }
    }

    private fun resumirNodeErrors(erros: JSONObject): String = erros.keys().asSequence().take(3).joinToString("; ") { id ->
        val n = erros.optJSONObject(id)
        val classe = n?.optString("class_type").orEmpty()
        val primeiro = n?.optJSONArray("errors")?.optJSONObject(0)
        val msg = listOf(primeiro?.optString("message"), primeiro?.optString("details"))
            .filter { !it.isNullOrBlank() }.joinToString(" — ")
        "nó $id ($classe): $msg"
    }

    companion object {
        /** Todas as saídas de vídeo do registro do `/history` (SaveVideo põe em "images" com animated). */
        fun videosDoHistorico(registro: JSONObject): List<ComfyArquivo> {
            val saidas = registro.optJSONObject("outputs") ?: return emptyList()
            val achados = ArrayList<ComfyArquivo>()
            for (no in saidas.keys()) {
                val o = saidas.optJSONObject(no) ?: continue
                for (chave in o.keys()) {
                    val lista = o.optJSONArray(chave) ?: continue
                    for (i in 0 until lista.length()) {
                        val it = lista.optJSONObject(i) ?: continue
                        val nome = it.optString("filename")
                        if (nome.lowercase().let { n -> n.endsWith(".mp4") || n.endsWith(".webm") || n.endsWith(".mkv") || n.endsWith(".mov") }) {
                            achados += ComfyArquivo(nome, it.optString("subfolder"), it.optString("type", "output"))
                        }
                    }
                }
            }
            return achados
        }

        /** Mensagens de erro do `/history` (status_str == "error"). */
        fun erroDoHistorico(registro: JSONObject): String? {
            val st = registro.optJSONObject("status") ?: return null
            if (st.optString("status_str") != "error") return null
            val msgs = st.optJSONArray("messages") ?: return "execution_error"
            for (i in 0 until msgs.length()) {
                val m = msgs.optJSONArray(i) ?: continue
                if (m.optString(0) == "execution_error") {
                    val d = m.optJSONObject(1)
                    return "execution_error no nó ${d?.optString("node_id")} (${d?.optString("node_type")}): " +
                        "${d?.optString("exception_type")}: ${d?.optString("exception_message")?.trim()?.take(300)}"
                }
            }
            return "execution_error"
        }
    }
}

/**
 * O workflow do MiniMax H3 (assets/ai/minimax_h3_api.json), montado para UM pedido.
 *
 * A tela só muda VALORES: o mapa `_aurea.inputs` do arquivo diz em que nó e em
 * que entrada cada um entra. Largura/altura e quadros saem da MESMA conta do
 * template (ResolutionSelector e a expressão de duração).
 */
object H3Workflow {

    /** Megapixels por resolução da tela (0,4 = padrão do template; 0,98 = 768p oficial). */
    fun megapixels(resolucao: String): Double = when (resolucao) {
        "preview" -> 0.2
        "high" -> 0.98
        else -> 0.4
    }

    /** ResolutionSelector: área em MP e proporção, arredondado para CIMA ao múltiplo de 32. */
    fun dimensoes(aspecto: String, resolucao: String): Pair<Int, Int> {
        val (a, b) = aspecto.split(":").map { it.trim().toDouble() }.let { it[0] to it[1] }
        val area = megapixels(resolucao) * 1_000_000.0
        val w = ceil(sqrt(area * a / b) / 32.0).toInt() * 32
        val h = ceil(sqrt(area * b / a) / 32.0).toInt() * 32
        return w to h
    }

    /** `max(5, round(a*24)) + (5 - (max(5, round(a*24)) % 17)) % 17` — a grade 17k+5 do H3. */
    fun quadros(segundos: Int): Int {
        val f = max(5, (segundos * 24.0).roundToInt())
        return f + Math.floorMod(5 - f % 17, 17)
    }

    fun montar(context: Context, pedido: Pedido, imagemComfy: String?): JSONObject {
        val texto = context.assets.open(AureaAiConfig.WORKFLOW_ASSET).use { String(it.readBytes(), Charsets.UTF_8) }
        return montar(JSONObject(texto), pedido, imagemComfy)
    }

    fun montar(arquivo: JSONObject, pedido: Pedido, imagemComfy: String?): JSONObject {
        val g = JSONObject(arquivo.toString())
        val meta = g.remove("_aurea") as JSONObject
        val mapa = meta.getJSONObject("inputs")
        fun por(chave: String, valor: Any) {
            val (no, entrada) = mapa.getString(chave).split(".", limit = 2)
            g.getJSONObject(no).getJSONObject("inputs").put(entrada, valor)
        }
        val (w, h) = dimensoes(pedido.aspecto, pedido.resolucao)
        por("prompt", pedido.prompt)
        por("largura", w)
        por("altura", h)
        por("quadros", quadros(pedido.duracao))
        por("semente", if (pedido.semente >= 0) pedido.semente.toLong() else (Math.random() * 1e15).toLong())
        por("turbo", pedido.turbo)
        por("fps", pedido.fps.toDouble())
        por("prefixo", "aurea/h3")
        if (pedido.modo == "image_to_video" && imagemComfy != null) {
            val img = meta.getJSONObject("imagem")
            g.put(img.getString("no"), JSONObject()
                .put("class_type", "LoadImage")
                .put("inputs", JSONObject().put("image", imagemComfy)))
            val (no, entrada) = img.getString("entrada").split(".", limit = 2)
            g.getJSONObject(no).getJSONObject("inputs").put(entrada, JSONArray().put(img.getString("no")).put(0))
        }
        return g
    }
}
