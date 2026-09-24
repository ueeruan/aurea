package com.aurea.aurea.ai

import org.json.JSONArray
import org.json.JSONObject

/**
 * O contrato com a Aurea AI API, do lado do app. Espelha `docs/ai/CONTRATO.md`.
 *
 * Regra que vale para tudo neste arquivo: o app não conhece endereço de
 * servidor. Ele conhece o lugar de um documento de discovery, e é do documento
 * que sai o endereço — que muda sozinho quando o Colab reinicia.
 */

/** O que a interface mostra no lugar de "● Aurea AI / Conectado". */
enum class AureaAiEstado {
    Checking,       // procurando o servidor
    Connected,      // achou, ocioso
    Generating,     // achou, gerando
    Reconnecting,   // tinha achado e perdeu: tentando de novo
    Disconnected,   // procurou, não achou
    Error,          // achou, mas o servidor recusou
}

/**
 * O que a interface escreve ao lado do nome do painel.
 *
 * "Online"/"Offline" e não "Conectado": o app não tem botão de conectar, então
 * a palavra não pode sugerir que exista um. Ele está online quando achou, e
 * offline quando não achou — sem ação do usuário no meio.
 */
fun AureaAiEstado.rotulo(): String = when (this) {
    AureaAiEstado.Checking -> "Procurando"
    AureaAiEstado.Connected -> "Online"
    AureaAiEstado.Generating -> "Online"
    AureaAiEstado.Reconnecting -> "Reconectando"
    AureaAiEstado.Disconnected -> "Offline"
    AureaAiEstado.Error -> "Offline"
}

/** `true` só nos estados em que dá para pedir uma geração agora. */
fun AureaAiEstado.podeGerar(): Boolean =
    this == AureaAiEstado.Connected || this == AureaAiEstado.Generating

/**
 * Espera antes da próxima tentativa, em segundos, enquanto não está online: a
 * 1ª tentativa é ao abrir; a 2ª, 2 s depois; daí em diante, a cada 5 s.
 */
fun recuo(tentativa: Int): Long = if (tentativa == 0) 2L else 5L

/**
 * O discovery: endereço FIXO, público, sem token. Ele devolve o endpoint da
 * sessão (o túnel trycloudflare, que muda a cada sessão) — por isso o endpoint
 * nunca é guardado nem escrito aqui.
 */
const val DISCOVERY_URL = AureaAiConfig.DISCOVERY_URL

/** Caminho que prova que o endpoint da sessão está de pé (ComfyUI). */
const val CAMINHO_SAUDE = "/system_stats"

/**
 * De quanto em quanto tempo o app relê o discovery depois de já estar online.
 *
 * É o que faz o app voltar sozinho: o Colab publica a batida a cada 20 s e o
 * app olha na mesma cadência. Não é mais curto que isso porque reler de segundo
 * em segundo gastaria bateria sem adiantar nada — o Colab não fica online mais
 * rápido por isso.
 */
const val VIGIA_SEGUNDOS = 20L

// ---------------------------------------------------------------------------
// Documento de discovery
// ---------------------------------------------------------------------------

/**
 * A resposta do discovery (`GET DISCOVERY_URL`):
 * `{"online": true, "endpoint": "https://….trycloudflare.com", "model": "MiniMax H3",
 *   "gpu": "A100", "capabilities": ["text_to_video", "image_to_video"]}`.
 *
 * Não pede token nem batida (`updatedAt`): quem diz se o servidor está no ar é
 * o `online` do discovery e, depois, o próprio endpoint respondendo
 * `system_stats`. Um `appToken`, se um dia vier, é usado — nunca exigido.
 */
data class Discovery(
    val endpoint: String,
    val online: Boolean,
    val modelo: String,
    val gpu: String,
    val capacidades: List<String>,
    val appToken: String = "",
) {
    /** Endereço utilizável: online e HTTPS (nada de http solto nem vazio). */
    fun valido(): Boolean = online && endpoint.startsWith("https://")

    companion object {
        /** Nulo quando o texto não é JSON. */
        fun ler(texto: String): Discovery? = try {
            val o = JSONObject(texto)
            Discovery(
                endpoint = o.optString("endpoint").trim().trimEnd('/'),
                online = o.optBoolean("online", false),
                modelo = o.optString("model"),
                gpu = o.optString("gpu"),
                capacidades = o.optJSONArray("capabilities").strings(),
                appToken = o.optString("appToken").trim(),
            )
        } catch (_: Exception) {
            null
        }
    }
}

// ---------------------------------------------------------------------------
// Capacidades — é o que monta a tela
// ---------------------------------------------------------------------------

data class Capacidades(
    val modos: List<String>,
    val duracoes: List<Int>,
    val aspectos: List<String>,
    val resolucoes: List<String>,
    val fps: List<Int>,
    val audio: Boolean,
    val jobsSimultaneos: Int,
    val fila: Int,
) {
    fun temImagemParaVideo() = modos.contains("image_to_video")

    companion object {
        val VAZIO = Capacidades(
            modos = emptyList(), duracoes = emptyList(), aspectos = emptyList(),
            resolucoes = emptyList(), fps = listOf(24), audio = false,
            jobsSimultaneos = 1, fila = 0,
        )

        /**
         * Quando o endpoint da sessão não serve `/api/v1/capabilities`: os modos
         * vêm do discovery e o resto é o valor do contrato (docs/ai/CONTRATO.md).
         */
        fun doDiscovery(modos: List<String>) = Capacidades(
            modos = modos.filter { it == "text_to_video" || it == "image_to_video" },
            duracoes = listOf(5, 10, 15),
            aspectos = listOf("9:16", "16:9", "1:1", "4:5"),
            resolucoes = listOf("preview", "standard", "high"),
            fps = listOf(24), audio = true, jobsSimultaneos = 1, fila = 0,
        )

        fun ler(o: JSONObject) = Capacidades(
            modos = o.optJSONArray("modes").strings(),
            duracoes = o.optJSONArray("durations").ints(),
            aspectos = o.optJSONArray("aspectRatios").strings(),
            resolucoes = o.optJSONArray("resolutions").strings(),
            fps = o.optJSONArray("fps").ints().ifEmpty { listOf(24) },
            audio = o.optBoolean("audio", false),
            jobsSimultaneos = o.optInt("maxConcurrentJobs", 1),
            fila = o.optInt("queueLength", 0),
        )
    }
}

private fun JSONArray?.strings(): List<String> =
    if (this == null) emptyList() else List(length()) { optString(it) }.filter { it.isNotEmpty() }

private fun JSONArray?.ints(): List<Int> =
    if (this == null) emptyList() else List(length()) { optInt(it) }

// ---------------------------------------------------------------------------
// Job
// ---------------------------------------------------------------------------

data class Resultado(
    val videoUrl: String,
    /** Vazio quando o servidor não conseguiu extrair um quadro. */
    val miniaturaUrl: String,
    val duracaoSegundos: Double,
    val largura: Int,
    val altura: Int,
    val fps: Int,
    val comAudio: Boolean,
)

data class Job(
    val id: String,
    val status: String,
    val progresso: Double,
    val etapa: String,
    val posicaoNaFila: Int,
    val segundos: Double,
    val erro: String?,
    val resultado: Resultado?,
) {
    val terminado: Boolean
        get() = status == "completed" || status == "failed" || status == "cancelled"

    val rodando: Boolean
        get() = !terminado

    companion object {
        fun ler(o: JSONObject): Job = Job(
            id = o.optString("jobId"),
            status = o.optString("status", "queued"),
            progresso = o.optDouble("progress", 0.0),
            etapa = o.optString("stage"),
            posicaoNaFila = o.optInt("queuePosition", 0),
            segundos = o.optDouble("elapsedSeconds", 0.0),
            erro = if (o.isNull("error")) null else o.optString("error"),
            resultado = o.optJSONObject("result")?.let(::lerResultado),
        )

        private fun lerResultado(r: JSONObject) = Resultado(
            videoUrl = r.optString("videoUrl"),
            miniaturaUrl = r.optString("thumbnailUrl"),
            duracaoSegundos = r.optDouble("duration", 0.0),
            largura = r.optInt("width"),
            altura = r.optInt("height"),
            fps = r.optInt("fps", 24),
            comAudio = r.optBoolean("hasAudio", false),
        )
    }
}

/** Frase para humano a partir do código de erro do servidor. */
fun explicarErro(codigo: String?): String = when (codigo) {
    null -> "Falhou"
    "comfy_rejected" -> "O motor recusou este pedido"
    "comfy_offline" -> "O motor de geração não respondeu"
    "generation_failed" -> "A geração falhou no meio"
    "result_missing" -> "A geração terminou sem produzir o arquivo"
    "timeout" -> "Passou do tempo máximo e foi interrompida"
    "workflow_invalid" -> "O workflow do servidor está inválido"
    "prepare_failed" -> "O servidor não conseguiu preparar o pedido"
    "internal_error" -> "Erro interno no servidor"
    else -> "Falhou ($codigo)"
}

// ---------------------------------------------------------------------------
// Anúncio
// ---------------------------------------------------------------------------

/**
 * Quantos anúncios a duração pede.
 *
 * É regra de produto, não do servidor: 5 s custa uma A100 por menos tempo que
 * 15 s, então a duração maior pede mais anúncio. Fica aqui, num lugar só, para
 * a tela e a futura cobrança não divergirem.
 */
fun anunciosPara(duracaoSegundos: Int): Int = when {
    duracaoSegundos <= 5 -> 1
    duracaoSegundos <= 10 -> 2
    else -> 3
}

// ---------------------------------------------------------------------------
// O que o app manda
// ---------------------------------------------------------------------------

/**
 * Só o que está na tabela do contrato. Nada de caminho, nome de workflow, id de
 * node ou comando: o servidor recusa e, se não recusasse, seria uma porta.
 */
data class Pedido(
    val modo: String,
    val prompt: String,
    val promptNegativo: String = "",
    val duracao: Int,
    val aspecto: String,
    val resolucao: String,
    val fps: Int = 24,
    val audio: Boolean = true,
    val semente: Int = -1,
    val turbo: Boolean = true,
    val assetId: String? = null,
) {
    fun json(): JSONObject = JSONObject().apply {
        put("mode", modo)
        put("prompt", prompt)
        put("negativePrompt", promptNegativo)
        put("duration", duracao)
        put("aspectRatio", aspecto)
        put("resolution", resolucao)
        put("fps", fps)
        put("audio", audio)
        put("seed", semente)
        put("turbo", turbo)
        if (assetId != null) put("imageAssetId", assetId)
    }
}

/** O que o servidor de saúde diz. O app exige `service == "aurea-ai"`. */
data class Saude(
    val servico: String,
    val pronto: Boolean,
    val gpu: String,
    val vramMb: Int,
    val fila: Int,
    val faltandoModelos: List<String>,
) {
    val eAureaAi: Boolean get() = servico == "aurea-ai"

    companion object {
        const val SERVICO = "aurea-ai"

        fun ler(o: JSONObject) = Saude(
            servico = o.optString("service"),
            pronto = o.optBoolean("ready", false),
            gpu = o.optString("gpu"),
            vramMb = o.optInt("vram_total_mb"),
            fila = o.optInt("queue"),
            faltandoModelos = o.optJSONArray("missingModels").strings(),
        )
    }
}
