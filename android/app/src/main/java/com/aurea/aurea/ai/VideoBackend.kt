package com.aurea.aurea.ai

import android.content.Context
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.net.HttpURLConnection
import java.net.SocketTimeoutException
import java.net.URL
import java.net.UnknownHostException
import java.util.UUID

/**
 * A geração de vídeo pelo BACKEND do Aurea (Cloudflare Worker), que fala com a
 * 8Scale. O app nunca fala com a 8Scale e nunca vê a chave dela: só o Worker a
 * tem, como secret.
 *
 * O endereço é o do Worker que o app já usa para o discovery — fixo, sem túnel.
 */
object AureaVideoBackend {
    /** `https://aurea-ai-discovery.aureaapp.workers.dev/api/ai/video` */
    val BASE: String = AureaAiConfig.DISCOVERY_URL.substringBeforeLast("/server") + "/api/ai/video"
}

/** O que o backend oferece hoje (monta a tela: nada de opção que ele recusa). */
data class ConfigDeVideo(
    val ligado: Boolean,
    val modelo: String,
    val modos: List<String>,
    val duracoes: List<Int>,
    val aspectos: List<String>,
    val resolucoes: List<String>,
    val promptMax: Int,
)

/** Estado de um job no backend. `status` ∈ queued | generating | completed | failed | cancelled. */
data class JobDeVideo(
    val id: String,
    val status: String,
    val etapa: String,
    val segundos: Double,
    val erro: String?,
    /** A falha foi do provedor: o mesmo ticket gera de novo SEM outro anúncio. */
    val repetirSemAnuncio: Boolean,
    val prontoParaBaixar: Boolean,
) {
    val terminado: Boolean get() = status == "completed" || status == "failed" || status == "cancelled"
}

/** Falha com código curto do backend (`recompensa_pendente`, `saldo_insuficiente`...). */
class FalhaDeVideo(val codigo: String, val http: Int = 0, detalhe: String = "") :
    IOException("$codigo${if (detalhe.isNotBlank()) ": $detalhe" else ""}") {
    /** Erro técnico que passa sozinho (rede, 5xx, provedor ocupado). */
    val transitorio: Boolean
        get() = http == 0 || http >= 500 || codigo in setOf("provedor_ocupado", "provedor_indisponivel", "provedor_timeout", "sem_conexao", "tempo_esgotado")
}

/**
 * O provedor de geração do ponto de vista do APP. A tela e o estado só
 * conhecem esta interface; trocar o provedor do backend (8Scale hoje) não muda
 * nada aqui.
 */
interface VideoGenerationProvider {
    fun config(): ConfigDeVideo
    fun enviarImagem(bytes: ByteArray, tipo: String): String
    /** Ticket de UMA geração: valida o pedido no servidor ANTES do anúncio. */
    fun ticket(pedido: Pedido): String
    /** Gera com o ticket (idempotente: o mesmo ticket devolve o mesmo job). */
    fun gerar(ticket: String): String
    fun status(jobId: String): JobDeVideo
    fun cancelar(jobId: String)
    fun baixar(jobId: String, destino: File): File
}

/** Identidade do aparelho para os limites do servidor: aleatória, sem dado pessoal. */
object IdDoAparelho {
    fun de(context: Context): String {
        val prefs = context.getSharedPreferences("aurea_ai_video", Context.MODE_PRIVATE)
        prefs.getString("aparelho", null)?.let { return it }
        val novo = UUID.randomUUID().toString()
        prefs.edit().putString("aparelho", novo).apply()
        return novo
    }
}

class AureaBackendVideoProvider(
    private val aparelho: String,
    private val base: String = AureaVideoBackend.BASE,
) : VideoGenerationProvider {

    override fun config(): ConfigDeVideo {
        val o = pedir("GET", "/config").json()
        fun lista(k: String) = o.optJSONArray(k)?.let { a -> List(a.length()) { a.optString(it) } }.orEmpty()
        return ConfigDeVideo(
            ligado = o.optBoolean("enabled", false),
            modelo = o.optString("model"),
            modos = lista("modes"),
            duracoes = lista("durations").mapNotNull { it.toIntOrNull() },
            aspectos = lista("aspectRatios"),
            resolucoes = lista("resolutions"),
            promptMax = o.optInt("promptMaxChars", 800),
        )
    }

    override fun enviarImagem(bytes: ByteArray, tipo: String): String =
        pedir("POST", "/images", corpo = bytes, tipoConteudo = tipo).json().optString("imageId")
            .ifBlank { throw FalhaDeVideo("resposta_invalida") }

    override fun ticket(pedido: Pedido): String {
        val corpo = JSONObject().apply {
            put("mode", pedido.modo)
            put("prompt", pedido.prompt)
            put("negativePrompt", pedido.promptNegativo)
            put("duration", pedido.duracao)
            put("aspectRatio", pedido.aspecto)
            put("resolution", pedido.resolucao)
            pedido.assetId?.let { put("imageId", it) }
        }
        return pedir("POST", "/tickets", corpo = corpo.toString().toByteArray()).json().optString("ticket")
            .ifBlank { throw FalhaDeVideo("resposta_invalida") }
    }

    override fun gerar(ticket: String): String {
        val corpo = JSONObject().put("ticket", ticket).toString().toByteArray()
        return pedir("POST", "/generate", corpo = corpo).json().optString("jobId")
            .ifBlank { throw FalhaDeVideo("resposta_invalida") }
    }

    override fun status(jobId: String): JobDeVideo {
        val o = pedir("GET", "/jobs/${enc(jobId)}").json()
        return JobDeVideo(
            id = o.optString("jobId", jobId),
            status = o.optString("status", "queued"),
            etapa = o.optString("stage"),
            segundos = o.optDouble("elapsedSeconds", 0.0),
            erro = if (o.isNull("error")) null else o.optString("error"),
            repetirSemAnuncio = o.optBoolean("retryWithoutAd", false),
            prontoParaBaixar = o.optJSONObject("result") != null,
        )
    }

    override fun cancelar(jobId: String) {
        pedir("POST", "/jobs/${enc(jobId)}/cancel", corpo = ByteArray(0))
    }

    /**
     * Baixa para um arquivo TEMPORÁRIO e só troca de nome se for vídeo MP4 de
     * verdade (caixa `ftyp` no começo): download interrompido ou página de erro
     * nunca vira "o vídeo".
     */
    override fun baixar(jobId: String, destino: File): File {
        destino.parentFile?.mkdirs()
        val parcial = File(destino.parentFile, destino.name + ".parte")
        val conn = abrir("GET", "/jobs/${enc(jobId)}/video", 300_000)
        try {
            val http = conn.responseCode
            if (http !in 200..299) throw falhaDe(http, conn.errorStream?.use { it.readBytes() } ?: ByteArray(0))
            val esperado = conn.contentLengthLong
            conn.inputStream.use { entrada -> parcial.outputStream().use { entrada.copyTo(it, 1 shl 16) } }
            if (esperado > 0 && parcial.length() != esperado) {
                parcial.delete()
                throw FalhaDeVideo("download_interrompido", 0, "${parcial.length()}/$esperado bytes")
            }
            if (!pareceMp4(parcial)) {
                parcial.delete()
                throw FalhaDeVideo("resultado_nao_e_video", 0)
            }
            if (destino.exists()) destino.delete()
            if (!parcial.renameTo(destino)) {
                parcial.copyTo(destino, overwrite = true)
                parcial.delete()
            }
            return destino
        } catch (e: FalhaDeVideo) {
            parcial.delete()
            throw e
        } catch (e: IOException) {
            parcial.delete()
            throw FalhaDeVideo(if (e is SocketTimeoutException) "tempo_esgotado" else "download_interrompido", 0, e.message ?: "")
        } finally {
            conn.disconnect()
        }
    }

    // -- transporte --------------------------------------------------------

    private class Resposta(val bytes: ByteArray) {
        fun json(): JSONObject = if (bytes.isEmpty()) JSONObject() else JSONObject(String(bytes, Charsets.UTF_8))
    }

    private fun enc(s: String) = java.net.URLEncoder.encode(s, "UTF-8")

    private fun abrir(metodo: String, caminho: String, timeoutMs: Int): HttpURLConnection =
        (URL(base + caminho).openConnection() as HttpURLConnection).apply {
            requestMethod = metodo
            connectTimeout = 15_000
            readTimeout = timeoutMs
            useCaches = false
            setRequestProperty("Accept", "application/json, video/mp4")
            // Sem UA próprio a Cloudflare do Worker devolve 403 (1010).
            setRequestProperty("User-Agent", AureaAiCliente.AGENTE)
            setRequestProperty("x-aurea-device", aparelho)
        }

    private fun pedir(
        metodo: String,
        caminho: String,
        corpo: ByteArray? = null,
        tipoConteudo: String = "application/json",
        timeoutMs: Int = 30_000,
    ): Resposta {
        val conn = try {
            abrir(metodo, caminho, timeoutMs)
        } catch (e: IOException) {
            throw FalhaDeVideo("sem_conexao", 0, e.message ?: "")
        }
        try {
            if (corpo != null) {
                conn.doOutput = true
                conn.setRequestProperty("Content-Type", tipoConteudo)
                conn.setFixedLengthStreamingMode(corpo.size)
                conn.outputStream.use { it.write(corpo) }
            }
            val http = conn.responseCode
            val bruto = (if (http in 200..299) conn.inputStream else conn.errorStream)?.use { it.readBytes() } ?: ByteArray(0)
            if (http !in 200..299) throw falhaDe(http, bruto)
            return Resposta(bruto)
        } catch (e: FalhaDeVideo) {
            throw e
        } catch (e: UnknownHostException) {
            throw FalhaDeVideo("sem_conexao", 0, e.message ?: "")
        } catch (e: SocketTimeoutException) {
            throw FalhaDeVideo("tempo_esgotado", 0, e.message ?: "")
        } catch (e: IOException) {
            throw FalhaDeVideo("sem_conexao", 0, e.message ?: "")
        } catch (e: org.json.JSONException) {
            throw FalhaDeVideo("resposta_invalida", 0, e.message ?: "")
        } finally {
            conn.disconnect()
        }
    }

    private fun falhaDe(http: Int, bruto: ByteArray): FalhaDeVideo {
        val codigo = runCatching { JSONObject(String(bruto, Charsets.UTF_8)).optString("error") }
            .getOrNull()?.ifBlank { null } ?: "erro_$http"
        return FalhaDeVideo(codigo, http)
    }

    companion object {
        /** MP4/MOV começam com uma caixa cujo tipo, nos bytes 4..7, é `ftyp`. */
        fun pareceMp4(f: File): Boolean {
            if (!f.isFile || f.length() < 12) return false
            val cab = ByteArray(8)
            f.inputStream().use { if (it.read(cab) != 8) return false }
            return cab[4] == 'f'.code.toByte() && cab[5] == 't'.code.toByte() &&
                cab[6] == 'y'.code.toByte() && cab[7] == 'p'.code.toByte()
        }
    }
}

/** Código do backend → frase para quem usa. O prompt nunca se perde por causa disso. */
fun explicarFalhaDeVideo(codigo: String?): String = when (codigo) {
    null -> "Não foi possível gerar o vídeo."
    "sem_conexao" -> "Sem internet. Confira a conexão e tente de novo."
    "tempo_esgotado", "provedor_timeout" -> "O servidor demorou para responder. Tente de novo."
    "provedor_indisponivel", "provedor_erro", "resposta_invalida" -> "O serviço de geração está indisponível agora. Tente de novo em instantes."
    "provedor_ocupado", "servidor_ocupado" -> "Muita gente gerando agora. Tente de novo em alguns minutos."
    "provedor_auth", "provedor_nao_configurado", "saldo_insuficiente", "modelo_indisponivel",
    "ia_nao_configurada", "recompensa_nao_configurada" -> "A geração por IA está em manutenção. Tente mais tarde."
    "ia_desligada" -> "A geração por IA está pausada no momento."
    "orcamento_diario", "limite_global_diario" -> "O limite de gerações de hoje foi atingido. Volte amanhã."
    "limite_diario" -> "Você atingiu o limite de gerações de hoje. Volte amanhã."
    "muitos_pedidos" -> "Muitos pedidos seguidos. Espere um pouco e tente de novo."
    "job_em_andamento", "em_andamento" -> "Já existe uma geração sua em andamento."
    "conteudo_bloqueado" -> "Esse pedido foi bloqueado pela política de conteúdo. Mude o texto e tente de novo."
    "pedido_recusado" -> "O pedido foi recusado pelo modelo. Mude o texto e tente de novo."
    "prompt_vazio" -> "Escreva o que você quer ver no vídeo."
    "prompt_longo" -> "O texto está longo demais."
    "geracao_falhou" -> "A geração falhou do lado do servidor."
    "resultado_invalido", "resultado_nao_e_video" -> "O servidor devolveu um arquivo que não é vídeo."
    "resultado_expirado" -> "O vídeo expirou no servidor (fica guardado 24 h)."
    "download_interrompido", "download_falhou" -> "O download foi interrompido. Toque para baixar de novo."
    "recompensa_pendente" -> "O anúncio ainda não foi confirmado. Tente de novo em instantes."
    "ticket_expirado", "ticket_invalido", "ticket_usado" -> "Este pedido expirou. Toque em gerar de novo."
    "imagem_expirada", "imagem_invalida", "imagem_grande" -> "Escolha a imagem de novo (PNG, JPEG ou WebP, até 8 MB)."
    "cancelado" -> "Geração cancelada."
    else -> "Não foi possível gerar o vídeo ($codigo)."
}
