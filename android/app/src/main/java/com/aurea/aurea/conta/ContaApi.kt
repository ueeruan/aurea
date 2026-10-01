package com.aurea.aurea.conta

import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.net.HttpURLConnection
import java.net.URL

/**
 * As rotas de conta do Worker (`discovery/contas.js`). Só HTTPS, só o endereço
 * fixo do Worker. A senha atravessa esta classe UMA vez, no corpo do POST —
 * nada aqui a guarda, registra no log ou devolve.
 *
 * Sem biblioteca de rede nova: `HttpURLConnection` + `org.json`, como o resto do app.
 */
object ContaApi {
    const val BASE = "https://aurea-ai-discovery.aureaapp.workers.dev"
    private const val RESPOSTA_MAX = 16 * 1024

    /** Resposta crua: HTTP + JSON. `status == 0` = não houve resposta (rede). */
    data class Resposta(val status: Int, val corpo: JSONObject) {
        val ok: Boolean get() = status in 200..299
        val codigo: String? get() = corpo.optString("error").ifBlank { null }
    }

    fun cadastrar(email: String, senha: String): Resposta =
        chamar("POST", "/api/auth/signup", JSONObject().put("email", email).put("password", senha), null)

    fun entrar(email: String, senha: String): Resposta =
        chamar("POST", "/api/auth/login", JSONObject().put("email", email).put("password", senha), null)

    fun validar(token: String): Resposta = chamar("GET", "/api/auth/session", null, token)

    fun sair(token: String): Resposta = chamar("POST", "/api/auth/logout", JSONObject(), token)

    fun usuarios(): Resposta = chamar("GET", "/api/stats/users", null, null)

    /** "Relatar um problema" (discovery/relato.js). Sessão opcional: dá o e-mail da conta ao relato. */
    fun relatar(corpo: JSONObject, token: String?): Resposta = chamar("POST", "/api/report", corpo, token)

    /** O que a revalidação significa para o estado (só 401 derruba). */
    fun revalidacao(r: Resposta): Revalidacao = when {
        r.ok -> Revalidacao.VALIDA
        r.status == 401 -> Revalidacao.INVALIDA
        else -> Revalidacao.SEM_RESPOSTA
    }

    private fun chamar(metodo: String, caminho: String, corpo: JSONObject?, token: String?): Resposta {
        var conexao: HttpURLConnection? = null
        return try {
            conexao = (URL(BASE + caminho).openConnection() as HttpURLConnection).apply {
                requestMethod = metodo
                connectTimeout = 12_000
                readTimeout = 20_000
                useCaches = false
                instanceFollowRedirects = false
                setRequestProperty("Accept", "application/json")
                if (token != null) setRequestProperty("Authorization", "Bearer $token")
            }
            if (corpo != null) {
                val bytes = corpo.toString().toByteArray(Charsets.UTF_8)
                conexao.doOutput = true
                conexao.setRequestProperty("Content-Type", "application/json; charset=utf-8")
                conexao.setFixedLengthStreamingMode(bytes.size)
                conexao.outputStream.use { it.write(bytes) }
            }
            val status = conexao.responseCode
            val fluxo = if (status in 200..299) conexao.inputStream else conexao.errorStream
            val texto = fluxo?.use { lerLimitado(it) }?.toString(Charsets.UTF_8).orEmpty()
            Resposta(status, runCatching { JSONObject(texto) }.getOrDefault(JSONObject()))
        } catch (_: IOException) {
            Resposta(0, JSONObject())
        } catch (_: SecurityException) {
            Resposta(0, JSONObject())
        } finally {
            conexao?.disconnect()
        }
    }

    private fun lerLimitado(entrada: InputStream): ByteArray {
        val saida = ByteArrayOutputStream()
        val buffer = ByteArray(4096)
        while (saida.size() <= RESPOSTA_MAX) {
            val n = entrada.read(buffer)
            if (n < 0) break
            saida.write(buffer, 0, n)
        }
        return saida.toByteArray()
    }
}
