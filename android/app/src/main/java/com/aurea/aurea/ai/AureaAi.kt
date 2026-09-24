package com.aurea.aurea.ai

import android.app.Application
import android.content.ContentValues
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import android.util.Log
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.aurea.aurea.ads.AureaAdsManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job as CoroutineJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

private const val TAG = "AureaAI"

/**
 * Estado da Aurea AI no aparelho: acha o ComfyUI, gera, acompanha, baixa.
 *
 * Fala DIRETO com a API do ComfyUI (ver [ComfyCliente]): `/system_stats` para
 * saber se está no ar, `POST /prompt` com o workflow do MiniMax H3, `/history`
 * para acompanhar, `/view` para baixar, `/upload/image` para o I2V. O endereço
 * sai de [AureaAiConfig.BASE_URL] (e, se ela não responder, do discovery).
 *
 * Mesma forma do `CaptionsState`: Compose state por fora, corrotina por dentro,
 * e nada de bloquear a thread da interface.
 */
class AureaAiState(
    private val app: Application,
    private val escopo: CoroutineScope,
    /** Chamado quando o vídeo baixado deve entrar na timeline. */
    private val aoAdicionarNaTimeline: (arquivo: File, titulo: String) -> Unit,
    private val aoMudar: () -> Unit = {},
) {
    // -- o que a tela observa ---------------------------------------------
    var estado by mutableStateOf(AureaAiEstado.Checking)
        private set
    var capacidades by mutableStateOf(Capacidades.VAZIO)
        private set
    var gpu by mutableStateOf("")
        private set
    /** Nome do modelo (ex.: "MiniMax H3"). */
    var modelo by mutableStateOf("")
        private set
    var mensagem by mutableStateOf("")
        private set

    var job by mutableStateOf<Job?>(null)
        private set
    /** Posição na fila do ComfyUI quando o job ainda não começou. */
    var posicaoNaFila by mutableStateOf(0)
        private set

    /** O ComfyUI não guarda "meus vídeos" por usuário: o histórico é desta sessão. */
    var historico by mutableStateOf<List<Job>>(emptyList())
        private set

    var baixando by mutableStateOf(false)
        private set
    var erro by mutableStateOf("")
        private set

    /** `true` enquanto o Rewarded está na tela. */
    var anunciando by mutableStateOf(false)
        private set

    /**
     * A geração DESTA tela (a última pedida): anúncio, H3 e liberação juntos.
     * Vive no estado (ViewModel), não na tela: sair e voltar, girar ou ir para
     * o fundo não perde nada.
     */
    var sessao by mutableStateOf<AiGenerationSession?>(null)
        private set
    private var sessaoAtualId: String? = null

    /** Uma geração em andamento (preparando anúncio, anúncio na tela ou H3 gerando). */
    val sessaoOcupada: Boolean
        get() = sessao?.status.let {
            it == SessaoStatus.Preparando || it == SessaoStatus.AnuncioNaTela || it == SessaoStatus.Gerando
        }

    /** O Rewarded da IA pelo AureaAdsManager (a tela nunca fala com o SDK). */
    private val anuncios = object : RewardedAds {
        override fun pronto(): Boolean = AureaAdsManager.rewardedReady()
        override fun carregar(aoCarregar: () -> Unit, aoFalhar: (String) -> Unit) =
            AureaAdsManager.preloadRewarded(aoCarregar, aoFalhar)
        override fun mostrar(aoAbrir: () -> Unit, aoRecompensa: () -> Unit, aoFechar: () -> Unit,
                             aoFalhar: (String) -> Unit): Boolean {
            // O contrato do RewardedAds: `false` = nenhum callback. O manager avisa a
            // falha antes de devolver `false`; essa primeira não é repassada.
            var devolveu = false
            val mostrou = AureaAdsManager.showRewarded(
                aoAbrir = { anunciando = true; aoAbrir() },
                aoRecompensa = aoRecompensa,
                aoFechar = { _ ->
                    anunciando = false
                    aoFechar()
                    // O próximo já fica a caminho (para "Assistir e liberar" ou a próxima geração).
                    AureaAdsManager.preloadRewarded()
                },
                aoFalhar = { e -> anunciando = false; if (devolveu) aoFalhar(e) },
            )
            devolveu = true
            return mostrou
        }
    }

    private var fimDaGeracao: ((File?, String?) -> Unit)? = null

    private val recompensa = AiRewardFlow(
        ads = anuncios,
        iniciarGeracao = { s, aoPromptId, aoTerminar ->
            fimDaGeracao = aoTerminar
            gerar(s.pedido, aoPromptId) { arquivo, e -> fimDaGeracao = null; aoTerminar(arquivo, e) }
        },
        aoMudar = { s ->
            if (s.generationId == sessaoAtualId) {
                sessao = s
                if (s.status == SessaoStatus.Liberado) liberar(s)
            }
        },
    )

    /** Último arquivo baixado, pronto para reproduzir, salvar e ir para a timeline. */
    var ultimoArquivo by mutableStateOf<File?>(null)
        private set
    var ultimoTitulo by mutableStateOf("")
        private set

    private var comfy: ComfyCliente? = null
    private var laco: CoroutineJob? = null
    private var acompanhando: CoroutineJob? = null
    private var tentativa = 0

    val conectado: Boolean
        get() = estado.podeGerar()

    // -- conexão -------------------------------------------------------------

    /**
     * Procura o servidor na hora e continua de olho enquanto a tela existir.
     * "Tentar de novo" chama de novo: começa do zero, sem reaproveitar endereço.
     */
    fun conectar() {
        laco?.cancel()
        tentativa = 0
        comfy = null
        laco = escopo.launch { procurar() }
    }

    fun desconectar() {
        laco?.cancel(); laco = null
        acompanhando?.cancel(); acompanhando = null
        comfy = null
        estado = AureaAiEstado.Disconnected
        capacidades = Capacidades.VAZIO
        gpu = ""
        modelo = ""
        job = null
    }

    private suspend fun procurar() {
        while (escopo.isActive) {
            if (tentativa == 0 && comfy == null) {
                estado = AureaAiEstado.Checking
                mensagem = "Procurando o servidor"
            }
            val final = withContext(Dispatchers.IO) { tentarConectar() }
            // Durante a geração o selo fica "Online"; o laço só não pode derrubar o job.
            estado = if (final == AureaAiEstado.Connected && job?.rodando == true) AureaAiEstado.Generating else final
            if (final == AureaAiEstado.Connected) {
                tentativa = 0
                delay(VIGIA_SEGUNDOS * 1000)
            } else {
                // Na hora (ao abrir), depois 2 s, depois a cada 5 s.
                delay(recuo(tentativa) * 1000)
                tentativa++
            }
        }
    }

    /**
     * Online só com `GET {base}/system_stats` → 200 + JSON válido. Tenta a
     * [AureaAiConfig.BASE_URL]; se ela não responder, o endpoint do discovery.
     */
    private fun tentarConectar(): AureaAiEstado {
        val candidatos = ArrayList<String>()
        candidatos += AureaAiConfig.BASE_URL.trimEnd('/')

        var doc: Discovery? = null
        Log.i(TAG, "[AureaAI] Discovery request")
        val leitura = AureaAiCliente.lerDiscovery()
        Log.i(TAG, "[AureaAI] Discovery HTTP: ${leitura.http}")
        leitura.doc?.let { d ->
            doc = d
            Log.i(TAG, "[AureaAI] online: ${d.online}")
            Log.i(TAG, "[AureaAI] endpoint: ${d.endpoint}")
            if (d.valido() && d.endpoint !in candidatos) candidatos += d.endpoint
        }

        for (base in candidatos) {
            val c = ComfyCliente(base)
            val (http, jsonOk) = c.estaOnline()
            Log.i(TAG, "[AureaAI] health HTTP: $http ($base/system_stats, json ${if (jsonOk) "ok" else "inválido"})")
            if (jsonOk) {
                if (comfy?.base != base) comfy = c
                modelo = doc?.modelo?.ifBlank { null } ?: "MiniMax H3"
                gpu = doc?.gpu.orEmpty()
                capacidades = Capacidades.doDiscovery(
                    doc?.capacidades?.ifEmpty { null } ?: listOf("text_to_video", "image_to_video"),
                )
                mensagem = ""
                Log.i(TAG, "[AureaAI] final state: ONLINE ($base)")
                return AureaAiEstado.Connected
            }
        }
        comfy = null
        // Discovery dizendo offline e nenhum endereço de pé: OFFLINE. Senão, ainda subindo.
        val final = if (doc?.online == false) AureaAiEstado.Disconnected else AureaAiEstado.Reconnecting
        Log.i(TAG, "[AureaAI] final state: ${if (final == AureaAiEstado.Disconnected) "OFFLINE" else "RECONNECTING"}")
        return final
    }

    // -- gerar -------------------------------------------------------------

    /** Ao entrar na tela AI Video: o Rewarded já começa a carregar. */
    fun prepararAnuncio() = AureaAdsManager.preloadRewarded()

    /**
     * O botão "Gerar": o H3 é enviado na hora (um job só) e o Rewarded aparece em
     * paralelo; o vídeo só é entregue com a recompensa (AiRewardFlow).
     */
    fun gerarComRecompensa(pedido: Pedido) {
        if (comfy == null) { erro = "Sem conexão com o servidor"; return }
        if (job?.rodando == true || sessaoOcupada) return
        erro = ""
        mensagem = ""
        ultimoArquivo = null
        val id = java.util.UUID.randomUUID().toString()
        sessaoAtualId = id
        recompensa.gerar(pedido, id)
    }

    /** "Assistir e liberar vídeo": outro anúncio para a MESMA geração — não gera de novo. */
    fun liberarComAnuncio() {
        val id = sessaoAtualId ?: return
        recompensa.liberarComAnuncio(id)
    }

    /** "Tentar de novo" quando não houve anúncio (a geração não tinha começado). */
    fun tentarGerarDeNovo() {
        val id = sessaoAtualId ?: return
        recompensa.tentarDeNovo(id)
    }

    /** Recompensa + vídeo: agora sim o resultado aparece para o usuário. */
    private fun liberar(s: AiGenerationSession) {
        val arquivo = s.result ?: return
        job?.let { j -> if (j.status == "completed") historico = listOf(j) + historico.filter { it.id != j.id } }
        ultimoArquivo = arquivo
        val (w, h) = H3Workflow.dimensoes(s.pedido.aspecto, s.pedido.resolucao)
        ultimoTitulo = "AI ${w}×$h"
        aoMudar()
    }

    /**
     * A geração REAL no H3 (não mexer): Enviando → Na fila → Gerando →
     * Finalizando → Concluído, cada estado como o ComfyUI disse. O vídeo baixado
     * NÃO é entregue aqui: vai para `aoTerminar`, e quem decide a entrega é a
     * recompensa. Roda no escopo do estado: o anúncio na frente não pausa nada.
     */
    private fun gerar(pedido: Pedido, aoPromptId: (String) -> Unit, aoTerminar: (File?, String?) -> Unit) {
        val c = comfy ?: run { erro = "Sem conexão com o servidor"; aoTerminar(null, erro); return }

        erro = ""
        mensagem = ""
        ultimoArquivo = null
        estado = AureaAiEstado.Generating
        val (w, h) = H3Workflow.dimensoes(pedido.aspecto, pedido.resolucao)
        val inicio = System.currentTimeMillis()
        fun etapa(status: String, texto: String, fila: Int = 0, res: Resultado? = null) = Job(
            id = job?.id.orEmpty(), status = status, progresso = 0.0, etapa = texto,
            posicaoNaFila = fila, segundos = (System.currentTimeMillis() - inicio) / 1000.0,
            erro = null, resultado = res,
        )
        job = etapa("sending", "Enviando")

        acompanhando = escopo.launch {
            try {
                val workflow = withContext(Dispatchers.IO) {
                    H3Workflow.montar(app, pedido, pedido.assetId)
                }
                val promptId = withContext(Dispatchers.IO) { c.enviarPrompt(workflow) }
                Log.i(TAG, "[AureaAI] POST /prompt → prompt_id $promptId (${w}x$h, ${H3Workflow.quadros(pedido.duracao)} quadros)")
                aoPromptId(promptId)
                job = etapa("queued", "Na fila").copy(id = promptId)

                // Polling do /history (não depende do WebSocket do frontend).
                var falhas = 0
                while (isActive) {
                    delay(2000)
                    val registro = try {
                        withContext(Dispatchers.IO) { c.historico(promptId) }.also { falhas = 0 }
                    } catch (e: ComfyErro) {
                        if (++falhas >= 15) throw e
                        estado = AureaAiEstado.Reconnecting
                        continue
                    }
                    estado = AureaAiEstado.Generating

                    if (registro == null) {
                        // Ainda não terminou: a fila diz se está rodando ou esperando.
                        val (rodando, pos) = runCatching { withContext(Dispatchers.IO) { c.situacaoNaFila(promptId) } }
                            .getOrDefault(false to 0)
                        job = if (rodando) etapa("running", "Gerando").copy(id = promptId)
                        else etapa("queued", "Na fila", pos).copy(id = promptId)
                        posicaoNaFila = pos
                        continue
                    }

                    ComfyCliente.erroDoHistorico(registro)?.let { throw ComfyErro(200, "execution_error", it) }
                    val video = ComfyCliente.videosDoHistorico(registro).firstOrNull()
                        ?: throw ComfyErro(200, "sem_video", "o /history terminou sem saída de vídeo")
                    Log.i(TAG, "[AureaAI] /history concluído → ${video.subpasta}/${video.nome}")

                    val res = Resultado(
                        videoUrl = "${c.base}/view?filename=${video.nome}", miniaturaUrl = "",
                        duracaoSegundos = H3Workflow.quadros(pedido.duracao) / pedido.fps.toDouble(),
                        largura = w, altura = h, fps = pedido.fps, comAudio = true,
                    )
                    job = etapa("finishing", "Finalizando", res = res).copy(id = promptId)
                    baixando = true
                    val arquivo = try {
                        withContext(Dispatchers.IO) {
                            c.baixar(video, File(File(app.filesDir, "aurea-ai"), "$promptId.mp4"))
                        }
                    } finally {
                        baixando = false
                    }
                    Log.i(TAG, "[AureaAI] /view → ${arquivo.length()} bytes")
                    val pronto = etapa("completed", "Concluído", res = res).copy(id = promptId)
                    job = pronto
                    estado = AureaAiEstado.Connected
                    aoTerminar(arquivo, null)
                    return@launch
                }
            } catch (e: ComfyErro) {
                Log.w(TAG, "[AureaAI] erro: ${e.paraTela()}")
                falhar(e.paraTela())
                aoTerminar(null, e.paraTela())
            } catch (e: kotlinx.coroutines.CancellationException) {
                throw e
            } catch (e: Exception) {
                Log.w(TAG, "[AureaAI] erro: $e")
                val texto = e.message ?: e.javaClass.simpleName
                falhar(texto)
                aoTerminar(null, texto)
            }
        }
    }

    fun cancelar() {
        val c = comfy ?: return
        val id = job?.id?.takeIf { it.isNotBlank() }
        escopo.launch {
            acompanhando?.cancel()
            if (id != null) runCatching { withContext(Dispatchers.IO) { c.cancelar(id) } }
            job = job?.copy(status = "cancelled", etapa = "Cancelado")
            estado = AureaAiEstado.Connected
            mensagem = "Cancelado"
            // A sessão termina sem vídeo (nada a liberar).
            fimDaGeracao?.let { fimDaGeracao = null; it(null, "Cancelado") }
        }
    }

    /** Só depois de o arquivo existir: a camada aponta para ele. */
    fun adicionarNaTimeline() {
        val arquivo = ultimoArquivo ?: return
        if (!arquivo.isFile || arquivo.length() == 0L) {
            erro = "O arquivo baixado está vazio"
            return
        }
        aoAdicionarNaTimeline(arquivo, ultimoTitulo.ifBlank { "Aurea AI" })
    }

    /** Copia o vídeo para a galeria (Filmes › Aurea). */
    fun salvarNaGaleria() {
        val arquivo = ultimoArquivo ?: return
        escopo.launch {
            val r = withContext(Dispatchers.IO) { runCatching { copiarParaGaleria(arquivo) } }
            r.onSuccess { mensagem = it }.onFailure { erro = "Não consegui salvar na galeria: ${it.message}" }
        }
    }

    private fun copiarParaGaleria(arquivo: File): String {
        val resolver = app.contentResolver
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val valores = ContentValues().apply {
                put(MediaStore.Video.Media.DISPLAY_NAME, "aurea_ai_${arquivo.nameWithoutExtension}.mp4")
                put(MediaStore.Video.Media.MIME_TYPE, "video/mp4")
                put(MediaStore.Video.Media.RELATIVE_PATH, "${Environment.DIRECTORY_MOVIES}/Aurea")
                put(MediaStore.Video.Media.IS_PENDING, 1)
            }
            val uri = resolver.insert(MediaStore.Video.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY), valores)
                ?: error("MediaStore recusou")
            try {
                resolver.openOutputStream(uri)!!.use { out -> arquivo.inputStream().use { it.copyTo(out, 1 shl 20) } }
                resolver.update(uri, ContentValues().apply { put(MediaStore.Video.Media.IS_PENDING, 0) }, null, null)
            } catch (e: Exception) {
                resolver.delete(uri, null, null)
                throw e
            }
            return "Salvo na galeria, em Filmes › Aurea"
        }
        val dir = File(app.getExternalFilesDir(Environment.DIRECTORY_MOVIES), "Aurea").apply { mkdirs() }
        val dst = File(dir, "aurea_ai_${arquivo.name}")
        arquivo.copyTo(dst, overwrite = true)
        return "Salvo em ${dst.absolutePath}"
    }

    fun atualizarHistorico() = Unit

    /** Reabre um vídeo desta sessão. */
    fun tocar(item: Job) {
        job = item
        if (item.status == "completed") {
            val f = File(File(app.filesDir, "aurea-ai"), "${item.id}.mp4")
            if (f.isFile) { ultimoArquivo = f; ultimoTitulo = "AI" }
        }
    }

    /**
     * Sobe a imagem de partida para o ComfyUI (`/upload/image`) e devolve a
     * referência que o LoadImage do workflow vai usar.
     */
    suspend fun enviarImagem(bytes: ByteArray, tipo: String): String? {
        val c = comfy ?: return null
        return try {
            withContext(Dispatchers.IO) { c.subirImagem(bytes, tipo) }.also {
                Log.i(TAG, "[AureaAI] /upload/image → $it")
            }
        } catch (e: ComfyErro) {
            erro = e.paraTela()
            null
        } catch (e: Exception) {
            erro = e.message ?: "não consegui enviar a imagem"
            null
        }
    }

    fun limparErro() { erro = ""; mensagem = "" }

    private fun falhar(texto: String) {
        erro = texto
        job = job?.copy(status = "failed", etapa = "Falhou", erro = texto)
        estado = if (comfy != null) AureaAiEstado.Connected else AureaAiEstado.Reconnecting
    }

    fun limparTudo() {
        escopo.launch { acompanhando?.cancel() }
        job = null
        mensagem = ""
        erro = ""
        ultimoArquivo = null
        ultimoTitulo = ""
    }
}

fun formarDuracao(segundos: Double): String {
    val total = segundos.toInt()
    return "%d:%02d".format(total / 60, total % 60)
}

/**
 * Extrai a imagem de partida para o modo imagem-para-vídeo.
 *
 * Devolve os bytes já prontos para subir. Só PNG/JPEG/WebP passam: o servidor
 * recusa o resto, e recusar aqui evita gastar a rede à toa.
 */
suspend fun lerImagemParaEnvio(app: Application, uri: Uri): Pair<ByteArray, String>? =
    withContext(Dispatchers.IO) {
        val tipo = app.contentResolver.getType(uri)?.lowercase() ?: "image/jpeg"
        if (tipo !in setOf("image/png", "image/jpeg", "image/webp")) return@withContext null
        val bytes = app.contentResolver.openInputStream(uri)?.use { it.readBytes() }
            ?: return@withContext null
        if (bytes.size > 12 * 1024 * 1024) return@withContext null
        bytes to tipo
    }

/** Nomes dos modos, para a UI. */
@androidx.annotation.StringRes
fun recursoDoModo(modo: String): Int = when (modo) {
    "text_to_video" -> com.aurea.aurea.R.string.ai_modo_texto
    "image_to_video" -> com.aurea.aurea.R.string.ai_modo_imagem
    else -> 0
}
