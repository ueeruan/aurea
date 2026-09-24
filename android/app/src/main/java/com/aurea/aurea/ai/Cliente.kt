package com.aurea.aurea.ai

import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.util.UUID

/**
 * Fala com a Aurea AI API.
 *
 * Sem biblioteca de rede nova: `HttpURLConnection` e `org.json` são o que o
 * resto do app já usa (as legendas fazem igual). Menos uma dependência para o
 * APK carregar e menos uma versão para conflitar.
 *
 * Assíncrono por fora (as chamadas rodam em `Dispatchers.IO`), bloqueante por
 * dentro — que é o que `HttpURLConnection` oferece.
 */
class AureaAiCliente(
    private val base: String,
    private val token: String,
    private val timeoutLeituraMs: Int = 30_000,
) {

    class Falha(val codigo: String, val detalhe: String, val http: Int = 0) :
        IOException("$codigo: $detalhe") {
        /** Vale a pena tentar de novo? 4xx é decisão do servidor, não soluço. */
        val valeTentar: Boolean get() = http == 0 || http >= 500 || http == 429
    }

    // -- o que o app pergunta ---------------------------------------------

    fun saude(): Saude = Saude.ler(pedir("GET", "/api/v1/health", autenticado = false).corpo)

    fun capacidades(): Capacidades = Capacidades.ler(pedir("GET", "/api/v1/capabilities").corpo)

    fun gerar(pedido: Pedido): Job = Job.ler(
        pedir("POST", "/api/v1/generations", corpo = pedido.json().toString()).corpo,
    )

    fun consultar(jobId: String): Job =
        Job.ler(pedir("GET", "/api/v1/generations/${escapar(jobId)}").corpo)

    fun cancelar(jobId: String) {
        pedir("DELETE", "/api/v1/generations/${escapar(jobId)}").fechar()
    }

    fun historico(): List<Job> {
        // O historico e uma LISTA na raiz, nao um objeto: `pedir` precisa saber
        // disso, senao tenta `JSONObject(...)` num array e estoura.
        val bruto = pedir("GET", "/api/v1/generations", lista = true).bytes
        if (bruto.isEmpty()) return emptyList()
        val a = JSONArray(String(bruto, Charsets.UTF_8))
        return List(a.length()) { Job.ler(a.getJSONObject(it)) }
    }

    /** Baixa o vídeo pronto para um arquivo do app. Devolve o arquivo. */
    fun baixarVideo(jobId: String, destino: File): File {
        val bytes = pedir("GET", "/api/v1/generations/${escapar(jobId)}/video",
            binario = true, timeoutMs = 300_000).bytes
        destino.parentFile?.mkdirs()
        destino.writeBytes(bytes)
        return destino
    }

    /**
     * Sobe a imagem de partida. O nome do arquivo é do aparelho e não vai como
     * está: o servidor descarta e devolve um UUID — mas mandar um nome limpo
     * evita até a aparência de caminho no caminho.
     */
    fun subirImagem(bytes: ByteArray, tipo: String): String {
        val fronteira = "----aurea${UUID.randomUUID().toString().replace("-", "")}"
        val extensao = when (tipo) {
            "image/png" -> "png"
            "image/webp" -> "webp"
            else -> "jpg"
        }
        val cabecalho = buildString {
            append("--$fronteira\r\n")
            append("Content-Disposition: form-data; name=\"file\"; filename=\"aurea.$extensao\"\r\n")
            append("Content-Type: $tipo\r\n\r\n")
        }.toByteArray(Charsets.UTF_8)
        val rodape = "\r\n--$fronteira--\r\n".toByteArray(Charsets.UTF_8)

        val corpo = cabecalho + bytes + rodape
        val resposta = pedir(
            "POST", "/api/v1/assets", corpoBytes = corpo,
            tipoConteudo = "multipart/form-data; boundary=$fronteira",
        )
        return resposta.corpo.optString("assetId")
    }

    // -- o transporte ------------------------------------------------------

    private class Resposta(val http: Int, val corpo: JSONObject, val bytes: ByteArray) {
        fun fechar() = Unit
    }

    private fun escapar(s: String) = java.net.URLEncoder.encode(s, "UTF-8")

    private fun pedir(
        metodo: String,
        caminho: String,
        corpo: String? = null,
        corpoBytes: ByteArray? = null,
        tipoConteudo: String = "application/json",
        autenticado: Boolean = true,
        binario: Boolean = false,
        lista: Boolean = false,
        timeoutMs: Int = timeoutLeituraMs,
    ): Resposta {
        val conn = (URL(base + caminho).openConnection() as HttpURLConnection).apply {
            requestMethod = metodo
            connectTimeout = 15_000
            readTimeout = timeoutMs
            // Sem token do usuário: só manda o cabeçalho se o discovery publicou um.
            if (autenticado && token.isNotBlank()) setRequestProperty("Authorization", "Bearer $token")
            setRequestProperty("Accept", if (binario) "*/*" else "application/json")
            setRequestProperty("User-Agent", "Aurea/${android.os.Build.VERSION.SDK_INT}")
            if (corpo != null || corpoBytes != null) {
                doOutput = true
                setRequestProperty("Content-Type", tipoConteudo)
            }
        }

        try {
            val dados = corpoBytes ?: corpo?.toByteArray(Charsets.UTF_8)
            if (dados != null) {
                conn.setFixedLengthStreamingMode(dados.size)
                conn.outputStream.use { it.write(dados) }
            }

            val http = conn.responseCode
            val fluxo = if (http in 200..299) conn.inputStream else conn.errorStream
            val bruto = fluxo?.use { it.readBytes() } ?: ByteArray(0)

            if (http !in 200..299) {
                throw traduzir(http, bruto)
            }
            if (binario || lista) return Resposta(http, JSONObject(), bruto)

            val json = if (bruto.isEmpty()) JSONObject() else JSONObject(String(bruto, Charsets.UTF_8))
            return Resposta(http, json, bruto)
        } catch (e: Falha) {
            throw e
        } catch (e: IOException) {
            throw Falha("sem_conexao", e.message ?: "falha de rede")
        } finally {
            conn.disconnect()
        }
    }

    /** O servidor já manda `{error, detail}`; aqui só vira exceção. */
    private fun traduzir(http: Int, bruto: ByteArray): Falha {
        val texto = String(bruto, Charsets.UTF_8)
        val (codigo, detalhe) = try {
            val o = JSONObject(texto)
            o.optString("error", "erro_$http") to o.optString("detail", texto.take(200))
        } catch (_: Exception) {
            "erro_$http" to texto.take(200)
        }
        return Falha(codigo, detalhe.ifBlank { "HTTP $http" }, http)
    }

    companion object {
        /** Resultado de uma leitura do discovery: o HTTP (0 = sem rede) e o documento. */
        data class LeituraDiscovery(val http: Int, val doc: Discovery?)

        /**
         * `GET DISCOVERY_URL`, sem token (é público) e sem cache — o endpoint da
         * sessão muda quando o servidor reinicia.
         */
        fun lerDiscovery(url: String = DISCOVERY_URL): LeituraDiscovery {
            val conn = try {
                URL(url).openConnection() as HttpURLConnection
            } catch (_: Exception) {
                return LeituraDiscovery(0, null)
            }
            return try {
                conn.connectTimeout = 10_000
                conn.readTimeout = 10_000
                conn.useCaches = false
                conn.setRequestProperty("Accept", "application/json")
                conn.setRequestProperty("Cache-Control", "no-cache")
                // Sem UA proprio a Cloudflare do Worker devolve 403 (1010).
                conn.setRequestProperty("User-Agent", AGENTE)
                val http = conn.responseCode
                val texto = if (http in 200..299) conn.inputStream.use { String(it.readBytes(), Charsets.UTF_8) } else ""
                LeituraDiscovery(http, if (texto.isEmpty()) null else Discovery.ler(texto))
            } catch (_: Exception) {
                LeituraDiscovery(0, null)
            } finally {
                conn.disconnect()
            }
        }

        /** O mesmo User-Agent em tudo que fala com o Worker. */
        const val AGENTE = "Aurea/2.0 (Android)"

        /** `GET {endpoint}/system_stats`: o HTTP da resposta (0 = sem resposta). */
        fun saudeDoEndpoint(endpoint: String): Int {
            val conn = try {
                URL(endpoint + CAMINHO_SAUDE).openConnection() as HttpURLConnection
            } catch (_: Exception) {
                return 0
            }
            return try {
                conn.connectTimeout = 10_000
                conn.readTimeout = 10_000
                conn.useCaches = false
                conn.setRequestProperty("Accept", "application/json")
                conn.setRequestProperty("User-Agent", AGENTE)
                val http = conn.responseCode
                runCatching { (if (http in 200..299) conn.inputStream else conn.errorStream)?.use { it.readBytes() } }
                http
            } catch (_: Exception) {
                0
            } finally {
                conn.disconnect()
            }
        }
    }
}
