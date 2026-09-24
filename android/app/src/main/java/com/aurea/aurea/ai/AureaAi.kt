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
import kotlin.coroutines.coroutineContext
import java.io.File

private const val TAG = "AureaAI"

/**
 * Estado da Aurea AI no aparelho: acha o ComfyUI, gera, acompanha, baixa.
 *
 * Fala DIRETO com a API do ComfyUI (ver [ComfyCliente]): `/system_stats` para
 * saber se está no ar, `POST /prompt` com o workflow do MiniMax H3, `/history`
 * para acompanhar, `/view` para baixar, `/upload/image` para o I2V. O endereço
 * sai do discovery, e só dele: não há endereço compilado no APK.
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
                aoRecompensa = {
                    // A recompensa vem SÓ daqui (onUserEarnedReward do SDK).
                    Log.i(TAG, "[AUREA AI] reward = recebida")
                    aoRecompensa()
                },
                aoFechar = { _ ->
                    anunciando = false
                    Log.i(TAG, "[AUREA AI] reward = anúncio fechado")
                    aoFechar()
                    // O próximo já fica a caminho (para "Assistir e liberar" ou a próxima geração).
                    AureaAdsManager.preloadRewarded()
                },
                aoFalhar = { e -> anunciando = false; Log.w(TAG, "[AUREA AI] error = anúncio: $e"); if (devolveu) aoFalhar(e) },
            )
            devolveu = true
            return mostrou
        }
    }

    private var fimDaGeracao: ((File?, String?) -> Unit)? = null

    /** A sessão em disco: é o que sobrevive ao anúncio e à morte do processo. */
    private val guarda = GuardaDaSessao(app)

    init {
        // Se o processo morreu no meio (o anúncio é o que costuma provocar
        // isso), a tela volta mostrando a geração que ficou. O acompanhamento
        // em si começa quando o servidor responder — `retomarSePreciso`.
        guarda.ler()?.let { g ->
            Log.i(TAG, "[AUREA AI] history = sessão anterior encontrada (prompt_id ${g.promptId}, concluída=${g.generationCompleted})")
            sessaoAtualId = g.generationId
            sessao = g
        }
    }

    private val recompensa = AiRewardFlow(
        ads = anuncios,
        iniciarGeracao = { s, aoPromptId, aoTerminar ->
            fimDaGeracao = aoTerminar
            gerar(s.pedido, aoPromptId) { arquivo, e -> fimDaGeracao = null; aoTerminar(arquivo, e) }
        },
        aoMudar = { s ->
            // Grava SEMPRE, e antes de qualquer coisa: o processo pode morrer no
            // instante seguinte (é o que o anúncio costuma provocar).
            guarda.gravar(s)
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
     * Online só com `GET {endpoint}/system_stats` → 200 + JSON válido.
     *
     * A ordem importa: o **discovery** manda. Ele é um endereço fixo que nunca
     * muda; o que muda é o `endpoint` que ele publica. Depois que o Colab
     * publica, trocar de túnel não pede APK nem IPA novo.
     *
     * Não há segunda opção compilada: sem discovery não há endereço. É de
     * propósito — endereço compilado é endereço que envelhece dentro do
     * binário e obriga a um APK novo a cada reinício do Colab.
     */
    private fun tentarConectar(): AureaAiEstado {
        Log.i(TAG, "[AUREA AI] discovery = ${AureaAiConfig.DISCOVERY_URL}")
        val leitura = AureaAiCliente.lerDiscovery()
        Log.i(TAG, "[AUREA AI] discovery = HTTP ${leitura.http}, ${leitura.doc?.let { "online=${it.online} endpoint=${it.endpoint} updatedAt=${it.updatedAt}" } ?: "sem documento"}")

        val doc = leitura.doc
        val candidatos = listOfNotNull(
            doc?.takeIf { it.valido() }?.endpoint?.trimEnd('/')?.takeIf { it.isNotBlank() },
        )

        for (base in candidatos) {
            Log.i(TAG, "[AUREA AI] endpoint = $base")
            val c = ComfyCliente(base)
            val (http, jsonOk) = c.estaOnline()
            Log.i(TAG, "[AUREA AI] system_stats = HTTP $http, json ${if (jsonOk) "ok" else "inválido"}")
            if (jsonOk) {
                if (comfy?.base != base) comfy = c
                modelo = doc?.modelo?.ifBlank { null } ?: "MiniMax H3"
                gpu = doc?.gpu.orEmpty()
                capacidades = Capacidades.doDiscovery(
                    doc?.capacidades?.ifEmpty { null } ?: listOf("text_to_video", "image_to_video"),
                )
                mensagem = ""
                retomarSePreciso()
                return AureaAiEstado.Connected
            }
        }
        comfy = null
        // Discovery dizendo offline e nenhum endereço de pé: OFFLINE. Senão, ainda subindo.
        val final = if (doc?.online == false) AureaAiEstado.Disconnected else AureaAiEstado.Reconnecting
        return final
    }

    // -- retomar depois do anúncio ------------------------------------------

    /**
     * Retoma a geração que ficou gravada, se houver uma.
     *
     * É o caminho de volta do Rewarded: o anúncio trouxe a Activity para trás (e
     * às vezes o sistema matou o processo). A sessão gravada diz qual
     * `prompt_id` acompanhar e por qual endereço — e retomar NUNCA manda outro
     * `POST /prompt`, senão o mesmo vídeo seria gerado duas vezes na A100.
     */
    private fun retomarSePreciso() {
        if (sessaoOcupada) return
        val gravada = guarda.ler() ?: return

        // A geração já tinha terminado e o arquivo está aqui: só falta a recompensa.
        if (gravada.generationCompleted && gravada.result != null) {
            Log.i(TAG, "[AUREA AI] release = retomando sessão já concluída (recompensa=${gravada.rewardEarned})")
            sessaoAtualId = gravada.generationId
            recompensa.retomar(gravada)
            return
        }

        val promptId = gravada.promptId
        if (promptId.isNullOrBlank()) {
            // Nunca chegou a haver prompt: a sessão não tem o que retomar.
            if (!gravada.generationStarted) { guarda.limpar(); return }
            guarda.limpar()
            return
        }

        Log.i(TAG, "[AUREA AI] history = retomando prompt_id $promptId em ${gravada.endpointUsado}")
        sessaoAtualId = gravada.generationId
        recompensa.retomar(gravada)
        val base = gravada.endpointUsado ?: comfy?.base ?: return
        val c = ComfyCliente(base)
        acompanhando = escopo.launch { acompanhar(c, promptId, gravada.pedido) }
    }

    /** Acompanha um `prompt_id` já existente até o fim (retomada, sem novo POST). */
    private suspend fun acompanhar(c: ComfyCliente, promptId: String, pedido: Pedido) {
        val (w, h) = H3Workflow.dimensoes(pedido.aspecto, pedido.resolucao)
        val inicio = System.currentTimeMillis()
        fun etapa(status: String, texto: String, fila: Int = 0, res: Resultado? = null) = Job(
            id = promptId, status = status, progresso = 0.0, etapa = texto,
            posicaoNaFila = fila, segundos = (System.currentTimeMillis() - inicio) / 1000.0,
            erro = null, resultado = res,
        )
        var falhas = 0
        try {
            // `coroutineContext.isActive` e não `isActive`: aqui não há receptor
            // de CoroutineScope (esta função é suspend, não um `launch`).
            while (coroutineContext.isActive) {
                delay(2000)
                val registro = try {
                    withContext(Dispatchers.IO) { c.historico(promptId) }.also { falhas = 0 }
                } catch (e: ComfyErro) {
                    if (++falhas >= 15) throw e
                    estado = AureaAiEstado.Reconnecting
                    continue
                }
                estado = AureaAiEstado.Generating
                Log.i(TAG, "[AUREA AI] history = $promptId → ${if (registro == null) "ainda não terminou" else "terminou"}")

                if (registro == null) {
                    val (rodando, pos) = runCatching { withContext(Dispatchers.IO) { c.situacaoNaFila(promptId) } }
                        .getOrDefault(false to 0)
                    job = if (rodando) etapa("running", "Gerando") else etapa("queued", "Na fila", pos)
                    posicaoNaFila = pos
                    continue
                }

                ComfyCliente.erroDoHistorico(registro)?.let { throw ComfyErro(200, "execution_error", it) }
                val video = ComfyCliente.videosDoHistorico(registro).firstOrNull()
                    ?: throw ComfyErro(200, "sem_video", "o /history terminou sem saída de vídeo")
                Log.i(TAG, "[AUREA AI] output = ${video.subpasta}/${video.nome}")
                entregar(c, video, promptId, pedido, w, h)
                return
            }
        } catch (e: kotlinx.coroutines.CancellationException) {
            throw e
        } catch (e: Exception) {
            val texto = (e as? ComfyErro)?.paraTela() ?: (e.message ?: e.javaClass.simpleName)
            Log.w(TAG, "[AUREA AI] error = $texto")
            falhar(texto)
            fimDaGeracao?.let { fimDaGeracao = null; it(null, texto) }
        }
    }

    /** Baixa o `/view` e entrega ao fluxo da recompensa (que decide a liberação). */
    private suspend fun entregar(
        c: ComfyCliente, video: ComfyArquivo, promptId: String, pedido: Pedido, w: Int, h: Int,
    ) {
        val res = Resultado(
            videoUrl = "${c.base}/view?filename=${video.nome}", miniaturaUrl = "",
            duracaoSegundos = H3Workflow.quadros(pedido.duracao) / pedido.fps.toDouble(),
            largura = w, altura = h, fps = pedido.fps, comAudio = true,
        )
        job = Job(promptId, "finishing", 0.0, "Finalizando", 0, 0.0, null, res)
        baixando = true
        val arquivo = try {
            Log.i(TAG, "[AUREA AI] download = ${c.base}/view?filename=${video.nome}")
            withContext(Dispatchers.IO) {
                c.baixar(video, File(File(app.filesDir, "aurea-ai"), "$promptId.mp4"))
            }
        } finally {
            baixando = false
        }
        Log.i(TAG, "[AUREA AI] download_bytes = ${arquivo.length()}")
        job = Job(promptId, "completed", 0.0, "Concluído", 0, 0.0, null, res)
        estado = AureaAiEstado.Connected
        fimDaGeracao?.let { fimDaGeracao = null; it(arquivo, null) }
    }

    // -- gerar -------------------------------------------------------------

    /** Ao entrar na tela AI Video: o Rewarded já começa a carregar. */
    fun prepararAnuncio() = AureaAdsManager.preloadRewarded()

    /**
     * O botão "Gerar": o H3 é enviado na hora (um job só) e o Rewarded aparece em
     * paralelo; o vídeo só é entregue com a recompensa (AiRewardFlow).
     */
    fun gerarComRecompensa(pedido: Pedido) {
        val c = comfy ?: run { erro = "Sem conexão com o servidor"; return }
        if (job?.rodando == true || sessaoOcupada) return
        erro = ""
        mensagem = ""
        ultimoArquivo = null
        val id = java.util.UUID.randomUUID().toString()
        sessaoAtualId = id
        // O endereço que aceitar o POST fica CONGELADO nesta sessão: se o túnel
        // cair no meio, esta geração continua sendo acompanhada por ele.
        Log.i(TAG, "[AUREA AI] POST /prompt = enviando (endpoint ${c.base})")
        recompensa.gerar(pedido, id, endpoint = c.base)
    }

    /** "Assistir e liberar vídeo": outro anúncio para a MESMA geração — não gera de novo. */
    fun liberarComAnuncio() {
        val id = sessaoAtualId ?: return
        recompensa.liberarComAnuncio(id)
    }

    /**
     * "Tentar de novo" depois de o H3 falhar.
     *
     * A sessão que falhou não serve mais — não existe prompt_id para retomar. O
     * que se aproveita é o pedido: começa uma geração NOVA, com um POST novo e
     * um prompt_id novo. É o único caso em que repetir o POST é certo.
     */
    fun tentarDeNovoAposFalha() {
        val s = sessao ?: return
        if (s.status != SessaoStatus.Falhou) return
        sessao = null
        sessaoAtualId = null
        guarda.limpar()
        job = null
        erro = ""
        gerarComRecompensa(s.pedido)
    }

    /** "Tentar de novo" quando não houve anúncio (a geração não tinha começado). */
    fun tentarGerarDeNovo() {
        val id = sessaoAtualId ?: return
        recompensa.tentarDeNovo(id)
    }

    /** Recompensa + vídeo: agora sim o resultado aparece para o usuário. */
    private fun liberar(s: AiGenerationSession) {
        val arquivo = s.result ?: return
        Log.i(TAG, "[AUREA AI] release = geração concluída=${s.generationCompleted} recompensa=${s.rewardEarned} → vídeo liberado (${arquivo.length()} bytes)")
        job?.let { j -> if (j.status == "completed") historico = listOf(j) + historico.filter { it.id != j.id } }
        ultimoArquivo = arquivo
        val (w, h) = H3Workflow.dimensoes(s.pedido.aspecto, s.pedido.resolucao)
        ultimoTitulo = "AI ${w}×$h"
        // Liberado é o fim da linha: não há mais o que retomar.
        guarda.limpar()
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
                Log.i(TAG, "[AUREA AI] prompt_id = $promptId (${w}x$h, ${H3Workflow.quadros(pedido.duracao)} quadros, ${c.base})")
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
                    Log.i(TAG, "[AUREA AI] history = $promptId → ${if (registro == null) "ainda não terminou" else "terminou"}")

                    if (registro == null) {
                        // Ainda não terminou: a fila diz se está rodando ou esperando.
                        val (rodando, pos) = runCatching { withContext(Dispatchers.IO) { c.situacaoNaFila(promptId) } }
                            .getOrDefault(false to 0)
                        job = if (rodando) etapa("running", "Gerando").copy(id = promptId)
                        else etapa("queued", "Na fila", pos).copy(id = promptId)
                        posicaoNaFila = pos
                        continue
                    }

                    // O registro já pode aparecer no /history com erro (o ComfyUI
                    // põe o que falhou lá). Sem isto, o app ficaria em "Gerando..."
                    // para sempre olhando um job que já morreu.
                    ComfyCliente.erroDoHistorico(registro)?.let { throw ComfyErro(200, "execution_error", it) }

                    val video = ComfyCliente.videosDoHistorico(registro).firstOrNull()
                    if (video == null) {
                        // Terminou sem saída de vídeo: ou ainda está escrevendo o
                        // arquivo, ou o nó de saída não produziu nada. Espera a
                        // próxima volta antes de acusar erro.
                        Log.w(TAG, "[AUREA AI] output = /history sem saída de vídeo ainda")
                        if (++falhas >= 5) throw ComfyErro(200, "sem_video", "o /history terminou sem saída de vídeo")
                        continue
                    }
                    Log.i(TAG, "[AUREA AI] output = ${video.subpasta}/${video.nome}")

                    entregar(c, video, promptId, pedido, w, h)
                    return@launch
                }
            } catch (e: ComfyErro) {
                Log.w(TAG, "[AUREA AI] error = ${e.paraTela()}")
                falhar(e.paraTela())
                aoTerminar(null, e.paraTela())
            } catch (e: kotlinx.coroutines.CancellationException) {
                throw e
            } catch (e: Exception) {
                Log.w(TAG, "[AUREA AI] error = $e")
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
