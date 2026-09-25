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

/** Quanto esperar o callback ASSINADO do anúncio chegar ao servidor antes de desistir. */
private const val ESPERA_DA_RECOMPENSA_MS = 90_000L

/** Uma geração que não termina neste tempo é dada como perdida (a 8Scale leva ~30 s). */
private const val TEMPO_MAXIMO_DO_JOB_MS = 15 * 60_000L

/**
 * Estado da Aurea AI no aparelho: fala com o BACKEND do Aurea (que fala com a
 * 8Scale), gera, acompanha, baixa e entrega.
 *
 * O app não conhece a 8Scale nem a chave dela. Conhece só
 * [VideoGenerationProvider] — hoje o [AureaBackendVideoProvider].
 *
 * Mesma forma do `CaptionsState`: Compose state por fora, corrotina por dentro,
 * e nada de bloquear a thread da interface.
 */
class AureaAiState(
    private val app: Application,
    private val escopo: CoroutineScope,
    /** Chamado quando o vídeo baixado deve entrar na timeline (o importador de sempre). */
    private val aoAdicionarNaTimeline: (arquivo: File, titulo: String) -> Unit,
    private val provedor: VideoGenerationProvider = AureaBackendVideoProvider({ IdDoAparelho.de(app) }),
    private val aoMudar: () -> Unit = {},
) {
    // -- o que a tela observa ---------------------------------------------
    var estado by mutableStateOf(AureaAiEstado.Checking)
        private set
    var capacidades by mutableStateOf(Capacidades.VAZIO)
        private set
    /** Sem GPU a mostrar: quem gera é o provedor do backend. */
    var gpu by mutableStateOf("")
        private set
    var modelo by mutableStateOf("")
        private set
    var mensagem by mutableStateOf("")
        private set
    var promptMax by mutableStateOf(800)
        private set
    /** "Gerações de hoje: 3/5" — o número é do servidor. Nulo = ainda não leu. */
    var cota by mutableStateOf<CotaDeVideo?>(null)
        private set

    /** Relê a cota no servidor (depois de abrir, de gerar e de falhar). */
    fun atualizarCota() {
        escopo.launch {
            runCatching { withContext(Dispatchers.IO) { provedor.cota() } }.onSuccess { cota = it }
        }
    }

    var job by mutableStateOf<Job?>(null)
        private set
    var posicaoNaFila by mutableStateOf(0)
        private set
    var historico by mutableStateOf<List<Job>>(emptyList())
        private set
    var baixando by mutableStateOf(false)
        private set
    var erro by mutableStateOf("")
        private set
    /** `true` enquanto o Rewarded está na tela. */
    var anunciando by mutableStateOf(false)
        private set

    /** A geração DESTA tela: ticket, anúncio, job e entrega juntos. */
    var sessao by mutableStateOf<AiGenerationSession?>(null)
        private set
    private var sessaoAtualId: String? = null

    /** Uma geração em andamento: botão "Gerar" travado (nada de dois pedidos por um toque). */
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
            var devolveu = false
            val mostrou = AureaAdsManager.showRewarded(
                aoAbrir = { anunciando = true; aoAbrir() },
                aoRecompensa = {
                    Log.i(TAG, "[AUREA AI] reward = recebida")
                    aoRecompensa()
                },
                aoFechar = { _ ->
                    anunciando = false
                    aoFechar()
                    AureaAdsManager.preloadRewarded()
                },
                aoFalhar = { e -> anunciando = false; Log.w(TAG, "[AUREA AI] anúncio: $e"); if (devolveu) aoFalhar(e) },
            )
            devolveu = true
            return mostrou
        }
    }

    private val guarda = GuardaDaSessao(app)

    private val recompensa = AiRewardFlow(
        ads = anuncios,
        pedirTicket = { s, aoTicket, aoFalhar ->
            escopo.launch {
                try {
                    val t = withContext(Dispatchers.IO) { provedor.ticket(s.pedido) }
                    Log.i(TAG, "[AUREA AI] ticket = ok")
                    aoTicket(t)
                } catch (e: FalhaDeVideo) {
                    Log.w(TAG, "[AUREA AI] ticket recusado: ${e.codigo}")
                    aoFalhar(e.codigo)
                }
            }
        },
        amarrarAnuncio = { ticket -> AureaAdsManager.definirUsuarioDaRecompensa(ticket) },
        iniciarGeracao = { s, aoJob, aoTerminar ->
            acompanhando?.cancel()
            acompanhando = escopo.launch { gerarEAcompanhar(s, aoJob, aoTerminar) }
        },
        aoMudar = { s ->
            // Grava SEMPRE, antes de tudo: o anúncio costuma matar o processo.
            guarda.gravar(s)
            if (s.generationId == sessaoAtualId) {
                sessao = s
                if (s.status == SessaoStatus.Liberado || s.status == SessaoStatus.Falhou || s.status == SessaoStatus.Gerando) atualizarCota()
                when (s.status) {
                    SessaoStatus.Liberado -> liberar(s)
                    SessaoStatus.Falhou -> { erro = explicarFalhaDeVideo(s.erro); job = job?.copy(status = "failed", etapa = "Falhou") }
                    else -> Unit
                }
            }
        },
    )

    init {
        guarda.ler()?.let { g ->
            Log.i(TAG, "[AUREA AI] sessão anterior encontrada (job ${g.jobId}, concluída=${g.generationCompleted})")
            sessaoAtualId = g.generationId
            sessao = g
        }
    }

    var ultimoArquivo by mutableStateOf<File?>(null)
        private set
    var ultimoTitulo by mutableStateOf("")
        private set

    private var laco: CoroutineJob? = null
    private var acompanhando: CoroutineJob? = null
    private var tentativa = 0

    val conectado: Boolean get() = estado.podeGerar()

    // -- conexão -------------------------------------------------------------

    /** Lê a configuração do backend (o que ele deixa pedir) e fica de olho. */
    fun conectar() {
        laco?.cancel()
        tentativa = 0
        laco = escopo.launch { procurar() }
    }

    fun desconectar() {
        laco?.cancel(); laco = null
        estado = AureaAiEstado.Disconnected
        capacidades = Capacidades.VAZIO
    }

    private suspend fun procurar() {
        while (escopo.isActive) {
            if (tentativa == 0 && !estado.podeGerar()) estado = AureaAiEstado.Checking
            val cfg = try {
                withContext(Dispatchers.IO) { provedor.config() }
            } catch (e: FalhaDeVideo) {
                Log.w(TAG, "[AUREA AI] config: ${e.codigo}")
                null
            }
            if (cfg == null) {
                estado = if (tentativa == 0) AureaAiEstado.Reconnecting else AureaAiEstado.Disconnected
                mensagem = explicarFalhaDeVideo("sem_conexao")
                delay(recuo(tentativa) * 1000)
                tentativa++
                continue
            }
            tentativa = 0
            modelo = cfg.modelo
            promptMax = cfg.promptMax
            capacidades = Capacidades(
                modos = cfg.modos, duracoes = cfg.duracoes, aspectos = cfg.aspectos,
                resolucoes = cfg.resolucoes, fps = listOf(16), audio = false,
                jobsSimultaneos = 1, fila = 0,
            )
            if (cfg.ligado) {
                if (mensagem == explicarFalhaDeVideo("sem_conexao") || mensagem == explicarFalhaDeVideo("ia_desligada")) mensagem = ""
                estado = if (sessao?.status == SessaoStatus.Gerando) AureaAiEstado.Generating else AureaAiEstado.Connected
                runCatching { withContext(Dispatchers.IO) { provedor.cota() } }.onSuccess { cota = it }
                retomarSePreciso()
            } else {
                estado = AureaAiEstado.Disconnected
                mensagem = explicarFalhaDeVideo("ia_desligada")
            }
            delay(60_000)
        }
    }

    // -- retomar depois do anúncio ------------------------------------------

    /**
     * A sessão gravada volta: com job, o acompanhamento continua (GET, nunca
     * outro POST pago); sem job mas com recompensa, o ticket — idempotente no
     * servidor — devolve o mesmo job ou gera o que ainda não tinha sido gerado.
     */
    private fun retomarSePreciso() {
        // Uma vez por abertura: dali em diante a sessão viva é a da tela.
        if (retomou) return
        retomou = true
        val gravada = guarda.ler() ?: return
        if (acompanhando?.isActive == true) return
        sessaoAtualId = gravada.generationId
        when {
            gravada.generationCompleted && gravada.result != null -> recompensa.retomar(gravada)
            gravada.generationStarted && gravada.ticket != null -> {
                recompensa.retomar(gravada)
                acompanhando = escopo.launch {
                    gerarEAcompanhar(gravada, { }, { arquivo, e, repetir ->
                        // A sessão foi retomada: o fim volta pelo fluxo normal.
                        val atual = sessao ?: gravada
                        val fim = if (e != null || arquivo == null)
                            atual.copy(erro = e ?: "geracao_falhou", status = SessaoStatus.Falhou,
                                podeRepetirSemAnuncio = repetir && atual.rewardEarned)
                        else atual.copy(generationCompleted = true, result = arquivo)
                        recompensa.retomar(fim)
                    })
                }
            }
            // O processo morreu com o anúncio na tela (ou antes dele): nada foi
            // gerado. A tela oferece assistir de novo, sem travar o "Gerar".
            !gravada.generationStarted &&
                (gravada.status == SessaoStatus.Preparando || gravada.status == SessaoStatus.AnuncioNaTela) ->
                recompensa.retomar(gravada.copy(status = SessaoStatus.SemRecompensa, adClosedEarly = true))
            else -> recompensa.retomar(gravada)
        }
    }

    private var retomou = false

    // -- gerar -------------------------------------------------------------

    fun prepararAnuncio() = AureaAdsManager.preloadRewarded()

    /** O botão "Gerar": ticket → anúncio → (recompensa) → geração real. */
    fun gerarComRecompensa(pedido: Pedido) {
        if (!estado.podeGerar() || sessaoOcupada) return
        // O servidor recusaria de qualquer jeito; aqui só evita o anúncio à toa.
        if (cota?.esgotada == true) { erro = explicarFalhaDeVideo("limite_diario"); return }
        erro = ""
        mensagem = ""
        ultimoArquivo = null
        job = null
        val id = java.util.UUID.randomUUID().toString()
        sessaoAtualId = id
        recompensa.gerar(pedido, id)
    }

    /** "Assistir de novo": o anúncio não veio ou fechou cedo. Mesmo pedido, nada gerado ainda. */
    fun liberarComAnuncio() {
        val id = sessaoAtualId ?: return
        recompensa.assistirDeNovo(id)
    }

    fun tentarGerarDeNovo() = liberarComAnuncio()

    /**
     * "Tentar de novo" depois de uma falha. Erro técnico com a recompensa ainda
     * valendo: o mesmo ticket, sem anúncio. Senão, uma geração nova (com anúncio),
     * reaproveitando o pedido — o prompt nunca se perde.
     */
    fun tentarDeNovoAposFalha() {
        val s = sessao ?: return
        if (s.status != SessaoStatus.Falhou) return
        erro = ""
        if (s.podeRepetirSemAnuncio) {
            recompensa.repetirSemAnuncio(s.generationId)
            return
        }
        sessao = null
        sessaoAtualId = null
        guarda.limpar()
        job = null
        gerarComRecompensa(s.pedido)
    }

    /**
     * A geração real: pede o job (esperando o callback assinado do anúncio
     * chegar ao servidor), acompanha o estado REAL e baixa o arquivo. Sem
     * porcentagem inventada: a 8Scale não dá progresso, então só etapas.
     */
    private suspend fun gerarEAcompanhar(
        s: AiGenerationSession,
        aoJob: (String) -> Unit,
        aoTerminar: (File?, String?, Boolean) -> Unit,
    ) {
        val ticket = s.ticket ?: run { aoTerminar(null, "ticket_invalido", false); return }
        val inicio = System.currentTimeMillis()
        fun etapa(status: String, texto: String) = Job(
            id = job?.id.orEmpty(), status = status, progresso = 0.0, etapa = texto,
            posicaoNaFila = 0, segundos = (System.currentTimeMillis() - inicio) / 1000.0,
            erro = null, resultado = null,
        )
        estado = AureaAiEstado.Generating
        try {
            // 1) O job. `recompensa_pendente` = o callback do LevelPlay ainda não
            //    chegou ao servidor: espera, sem mostrar outro anúncio.
            var jobId = s.jobId
            if (jobId == null) {
                job = etapa("sending", "Enviando…")
                while (jobId == null) {
                    jobId = try {
                        withContext(Dispatchers.IO) { provedor.gerar(ticket) }
                    } catch (e: FalhaDeVideo) {
                        val esperando = e.codigo == "recompensa_pendente" || e.codigo == "em_andamento"
                        if (esperando && System.currentTimeMillis() - inicio < ESPERA_DA_RECOMPENSA_MS) {
                            delay(2000); null
                        } else throw e
                    }
                }
                Log.i(TAG, "[AUREA AI] job = $jobId")
                aoJob(jobId)
            }

            // 2) Acompanhar o estado real.
            var falhas = 0
            while (coroutineContext.isActive) {
                val j = try {
                    withContext(Dispatchers.IO) { provedor.status(jobId) }.also { falhas = 0 }
                } catch (e: FalhaDeVideo) {
                    // Consulta que falha não muda o job: tenta de novo (rede some e volta).
                    if (!e.transitorio || ++falhas >= 30) throw e
                    estado = AureaAiEstado.Reconnecting
                    delay(3000)
                    continue
                }
                estado = AureaAiEstado.Generating
                job = Job(
                    id = j.id, status = j.status, progresso = 0.0,
                    etapa = when (j.status) {
                        "queued" -> "Enviando…"
                        "generating" -> "Gerando vídeo…"
                        "completed" -> "Finalizando…"
                        else -> j.etapa
                    },
                    posicaoNaFila = 0, segundos = j.segundos, erro = j.erro, resultado = null,
                )
                if (j.terminado) {
                    if (j.status != "completed" || !j.prontoParaBaixar) {
                        estado = AureaAiEstado.Connected
                        aoTerminar(null, j.erro ?: "geracao_falhou", j.repetirSemAnuncio)
                        return
                    }
                    break
                }
                if (System.currentTimeMillis() - inicio > TEMPO_MAXIMO_DO_JOB_MS) throw FalhaDeVideo("tempo_esgotado")
                delay(2000)
            }

            // 3) Baixar (arquivo temporário → valida MP4 → nome final).
            baixando = true
            val arquivo = try {
                withContext(Dispatchers.IO) {
                    provedor.baixar(jobId, File(File(app.filesDir, "aurea-ai"), "$jobId.mp4"))
                }
            } finally {
                baixando = false
            }
            Log.i(TAG, "[AUREA AI] download = ${arquivo.length()} bytes")
            val meta = withContext(Dispatchers.IO) { metadataDoVideo(arquivo) }
                ?: throw FalhaDeVideo("resultado_nao_e_video")
            job = job?.copy(status = "completed", etapa = "Concluído", resultado = meta)
            estado = AureaAiEstado.Connected
            aoTerminar(arquivo, null, false)
        } catch (e: kotlinx.coroutines.CancellationException) {
            throw e
        } catch (e: FalhaDeVideo) {
            Log.w(TAG, "[AUREA AI] falha = ${e.codigo}")
            estado = AureaAiEstado.Connected
            // Técnica (rede, provedor fora, download) = a recompensa segue valendo.
            // `recompensa_pendente`: o anúncio FOI assistido; só a confirmação do
            // provedor ainda não chegou ao servidor — repetir não pede outro anúncio.
            val repetir = e.transitorio || e.codigo.startsWith("download") || e.codigo == "tempo_esgotado" ||
                e.codigo == "recompensa_pendente"
            aoTerminar(null, e.codigo, repetir)
        } catch (e: Exception) {
            Log.w(TAG, "[AUREA AI] falha = $e")
            estado = AureaAiEstado.Connected
            aoTerminar(null, "geracao_falhou", true)
        }
    }

    /** Só enquanto está na fila: depois que a geração começa, ela vai até o fim. */
    fun cancelar() {
        val id = sessao?.jobId ?: return
        escopo.launch {
            try {
                withContext(Dispatchers.IO) { provedor.cancelar(id) }
            } catch (e: kotlinx.coroutines.CancellationException) {
                throw e
            } catch (e: FalhaDeVideo) {
                if (sessao?.jobId == id) erro = explicarFalhaDeVideo(e.codigo)
            } catch (e: Exception) {
                if (sessao?.jobId == id) erro = explicarFalhaDeVideo("sem_conexao")
            }
        }
    }

    private fun liberar(s: AiGenerationSession) {
        val arquivo = s.result ?: return
        Log.i(TAG, "[AUREA AI] vídeo entregue (${arquivo.length()} bytes)")
        job?.let { j -> historico = listOf(j) + historico.filter { it.id != j.id } }
        ultimoArquivo = arquivo
        ultimoTitulo = "Aurea AI"
        guarda.limpar()
        aoMudar()
    }

    /** O arquivo já foi validado como MP4; quem sonda e cria a camada é o importador do motor. */
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

    fun atualizarHistorico() = conectar()

    /** Reabre um vídeo desta sessão. */
    fun tocar(item: Job) {
        job = item
        if (item.status == "completed") {
            val f = File(File(app.filesDir, "aurea-ai"), "${item.id}.mp4")
            if (f.isFile) { ultimoArquivo = f; ultimoTitulo = "Aurea AI" }
        }
    }

    /** Sobe a imagem de partida (I2V) para o backend; devolve o id que o pedido cita. */
    suspend fun enviarImagem(bytes: ByteArray, tipo: String): String? = try {
        withContext(Dispatchers.IO) { provedor.enviarImagem(bytes, tipo) }
    } catch (e: FalhaDeVideo) {
        erro = explicarFalhaDeVideo(e.codigo)
        null
    }

    fun limparErro() { erro = ""; mensagem = "" }

    fun limparTudo() {
        escopo.launch { acompanhando?.cancel() }
        job = null
        mensagem = ""
        erro = ""
        ultimoArquivo = null
        ultimoTitulo = ""
    }
}

/**
 * Duração, tamanho e fps REAIS do arquivo baixado, lidos pelo decodificador do
 * aparelho. Nulo = o aparelho não consegue ler como vídeo (não entra na timeline).
 */
fun metadataDoVideo(arquivo: File): Resultado? {
    val r = android.media.MediaMetadataRetriever()
    return try {
        r.setDataSource(arquivo.absolutePath)
        fun chave(k: Int) = r.extractMetadata(k)
        val ms = chave(android.media.MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull() ?: return null
        val w = chave(android.media.MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)?.toIntOrNull() ?: return null
        val h = chave(android.media.MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)?.toIntOrNull() ?: return null
        if (ms <= 0 || w <= 0 || h <= 0) return null
        val quadros = if (Build.VERSION.SDK_INT >= 28)
            chave(android.media.MediaMetadataRetriever.METADATA_KEY_VIDEO_FRAME_COUNT)?.toIntOrNull() else null
        val fps = if (quadros != null && quadros > 0) Math.round(quadros * 1000.0 / ms).toInt() else 0
        Resultado(
            videoUrl = "", miniaturaUrl = "", duracaoSegundos = ms / 1000.0,
            largura = w, altura = h, fps = fps,
            comAudio = chave(android.media.MediaMetadataRetriever.METADATA_KEY_HAS_AUDIO) == "yes",
        )
    } catch (_: Exception) {
        null
    } finally {
        runCatching { r.release() }
    }
}

fun formarDuracao(segundos: Double): String {
    val total = segundos.toInt()
    return "%d:%02d".format(total / 60, total % 60)
}

/**
 * Extrai a imagem de partida para o modo imagem-para-vídeo. Só PNG/JPEG/WebP
 * até 8 MB passam: o servidor recusa o resto, e recusar aqui poupa a rede.
 */
suspend fun lerImagemParaEnvio(app: Application, uri: Uri): Pair<ByteArray, String>? =
    withContext(Dispatchers.IO) {
        val tipo = app.contentResolver.getType(uri)?.lowercase() ?: "image/jpeg"
        if (tipo !in setOf("image/png", "image/jpeg", "image/webp")) return@withContext null
        val bytes = app.contentResolver.openInputStream(uri)?.use { readImageUploadBytes(it) }
            ?: return@withContext null
        bytes to tipo
    }

/** Nomes dos modos, para a UI. */
@androidx.annotation.StringRes
fun recursoDoModo(modo: String): Int = when (modo) {
    "text_to_video" -> com.aurea.aurea.R.string.ai_modo_texto
    "image_to_video" -> com.aurea.aurea.R.string.ai_modo_imagem
    else -> 0
}
