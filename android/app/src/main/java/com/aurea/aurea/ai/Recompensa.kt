package com.aurea.aurea.ai

import java.io.File
import java.util.UUID

/**
 * O que a geração por IA precisa de um anúncio Rewarded. O [AureaAiState] liga
 * isto ao AureaAdsManager; os testes, a um falso.
 *
 * `mostrar` devolve `false` (e não chama nada) se não havia anúncio para
 * mostrar. `aoRecompensa` vem SÓ do callback oficial de recompensa
 * (onUserEarnedReward); `aoFechar` é só o aviso de que a tela fechou.
 */
interface RewardedAds {
    fun pronto(): Boolean
    fun carregar(aoCarregar: () -> Unit, aoFalhar: (String) -> Unit)
    fun mostrar(aoAbrir: () -> Unit, aoRecompensa: () -> Unit, aoFechar: () -> Unit, aoFalhar: (String) -> Unit): Boolean
}

/** Em que ponto uma geração está, do ponto de vista de quem usa. */
enum class SessaoStatus {
    /** Pedindo o ticket ao servidor e/ou esperando o Rewarded carregar. Nada foi gerado. */
    Preparando,
    /** Não deu para ter anúncio (rede, SDK). Nada foi gerado; dá para tentar de novo. */
    AnuncioIndisponivel,
    /** Anúncio na tela. A geração ainda NÃO começou (ela é paga: só depois da recompensa). */
    AnuncioNaTela,
    /** O anúncio fechou sem recompensa. Nada foi gerado; o mesmo pedido espera outro anúncio. */
    SemRecompensa,
    /** Recompensa confirmada: a geração real está rodando no servidor. */
    Gerando,
    /** Vídeo pronto, baixado e validado. */
    Liberado,
    /** Não há vídeo. [AiGenerationSession.podeRepetirSemAnuncio] diz se tentar de novo pede anúncio. */
    Falhou,
}

/**
 * UMA geração. Cada uma tem o próprio ticket no servidor: a recompensa de um
 * anúncio vale para esta geração e para nenhuma outra. Imutável.
 */
data class AiGenerationSession(
    val generationId: String,
    val pedido: Pedido,
    /** O ticket do servidor (amarrado ao anúncio pelo Dynamic User ID). */
    val ticket: String? = null,
    /** O job aceito pelo servidor. Existindo, retomar é só ACOMPANHAR — nunca gerar de novo. */
    val jobId: String? = null,
    val rewardEarned: Boolean = false,
    val generationCompleted: Boolean = false,
    val result: File? = null,
    val status: SessaoStatus = SessaoStatus.Preparando,
    /** A geração paga já foi pedida para este ticket. */
    val generationStarted: Boolean = false,
    val adClosedEarly: Boolean = false,
    /** Código curto do erro (ver `explicarFalhaDeVideo`). */
    val erro: String? = null,
    val adError: String? = null,
    /**
     * A falha foi técnica e a recompensa continua valendo: "Tentar de novo" usa
     * o MESMO ticket, sem mostrar outro anúncio.
     */
    val podeRepetirSemAnuncio: Boolean = false,
) {
    val liberado: Boolean get() = generationCompleted && rewardEarned && result != null && erro == null
}

/**
 * A ordem de uma geração PAGA:
 *   ticket no servidor → Rewarded (com o ticket como Dynamic User ID)
 *   → recompensa → geração real → vídeo.
 *
 * Regras:
 *  - nada é gerado antes da recompensa (o provedor cobra por geração);
 *  - a recompensa só vem de `aoRecompensa` (callback oficial do SDK); o
 *    servidor ainda confere o callback ASSINADO do LevelPlay antes de gerar;
 *  - uma recompensa inicia UMA geração: duplo toque, callback repetido ou volta
 *    do fundo não geram de novo;
 *  - erro técnico depois da recompensa não pede outro anúncio: o mesmo ticket
 *    gera de novo quando o usuário tocar em "Tentar de novo";
 *  - fechar o anúncio cedo não gera nada e não perde o pedido.
 *
 * Tudo roda na thread principal.
 */
class AiRewardFlow(
    private val ads: RewardedAds,
    /** Pede o ticket ao servidor (valida o pedido ANTES do anúncio). */
    private val pedirTicket: (sessao: AiGenerationSession, aoTicket: (String) -> Unit, aoFalhar: (String) -> Unit) -> Unit,
    /** Amarra o próximo Rewarded ao ticket (LevelPlay.setDynamicUserId). */
    private val amarrarAnuncio: (ticket: String) -> Unit,
    /**
     * Gera de verdade com o ticket já recompensado. Chama `aoJob` quando o
     * servidor aceitar e `aoTerminar` uma vez: arquivo, ou código de erro (com
     * `repetirSemAnuncio` quando a recompensa continua valendo).
     */
    private val iniciarGeracao: (sessao: AiGenerationSession, aoJob: (String) -> Unit,
                                 aoTerminar: (arquivo: File?, erro: String?, repetirSemAnuncio: Boolean) -> Unit) -> Unit,
    private val aoMudar: (AiGenerationSession) -> Unit,
    private val novoId: () -> String = { UUID.randomUUID().toString() },
) {
    private val sessoes = LinkedHashMap<String, AiGenerationSession>()

    fun sessao(id: String): AiGenerationSession? = sessoes[id]
    fun todas(): List<AiGenerationSession> = sessoes.values.toList()

    private fun por(s: AiGenerationSession): AiGenerationSession {
        sessoes[s.generationId] = s
        aoMudar(s)
        return s
    }

    /** Toque em "Gerar": ticket → anúncio. A geração só começa na recompensa. */
    fun gerar(pedido: Pedido, id: String = novoId()): AiGenerationSession {
        require(id !in sessoes) { "sessão repetida" }
        por(AiGenerationSession(id, pedido, status = SessaoStatus.Preparando))
        pedirTicket(sessoes.getValue(id),
            { ticket ->
                val s = sessoes[id]
                if (s != null && s.ticket == null) {
                    por(s.copy(ticket = ticket))
                    amarrarAnuncio(ticket)
                    prepararEApresentar(id)
                }
            },
            { codigo ->
                // Sem ticket (limite, IA desligada, rede): nenhum anúncio, nada gerado.
                sessoes[id]?.let { por(it.copy(status = SessaoStatus.Falhou, erro = codigo, podeRepetirSemAnuncio = false)) }
            })
        return sessoes.getValue(id)
    }

    /**
     * Põe de volta uma sessão gravada em disco. Não pede ticket, não mostra
     * anúncio e não gera: quem retoma o ACOMPANHAMENTO do job é o estado.
     */
    fun retomar(s: AiGenerationSession): AiGenerationSession {
        sessoes[s.generationId] = s
        return avaliar(s)
    }

    /** "Assistir de novo": anúncio que não veio ou fechou cedo. O mesmo ticket, o mesmo pedido. */
    fun assistirDeNovo(id: String) {
        val s = sessoes[id] ?: return
        if (s.generationStarted || s.rewardEarned) return
        if (s.status != SessaoStatus.AnuncioIndisponivel && s.status != SessaoStatus.SemRecompensa) return
        val ticket = s.ticket ?: return
        por(s.copy(status = SessaoStatus.Preparando, adError = null, adClosedEarly = false))
        amarrarAnuncio(ticket)
        prepararEApresentar(id)
    }

    /** "Tentar de novo" depois de erro técnico: mesmo ticket, SEM anúncio. */
    fun repetirSemAnuncio(id: String) {
        val s = sessoes[id] ?: return
        if (s.status != SessaoStatus.Falhou || !s.podeRepetirSemAnuncio || !s.rewardEarned || s.ticket == null) return
        iniciar(s.copy(jobId = null, erro = null, podeRepetirSemAnuncio = false, generationStarted = false))
    }

    private fun prepararEApresentar(id: String) {
        if (ads.pronto()) { apresentar(id); return }
        ads.carregar(
            aoCarregar = { apresentar(id) },
            aoFalhar = { e -> semAnuncio(id, e) },
        )
    }

    private fun semAnuncio(id: String, e: String) {
        val s = sessoes[id] ?: return
        if (s.rewardEarned || s.generationStarted) return
        por(s.copy(status = SessaoStatus.AnuncioIndisponivel, adError = e))
    }

    private fun apresentar(id: String) {
        if (sessoes[id] == null) return
        val mostrou = ads.mostrar(
            aoAbrir = {
                val s = sessoes[id]
                if (s != null && !s.rewardEarned) por(s.copy(status = SessaoStatus.AnuncioNaTela, adError = null))
            },
            aoRecompensa = {
                val s = sessoes[id]
                // Um anúncio, uma geração: recompensa repetida não gera de novo.
                if (s != null && !s.rewardEarned && !s.generationStarted) iniciar(s.copy(rewardEarned = true))
            },
            aoFechar = {
                val s = sessoes[id]
                if (s != null && !s.rewardEarned) por(s.copy(status = SessaoStatus.SemRecompensa, adClosedEarly = true))
            },
            aoFalhar = { e -> semAnuncio(id, e) },
        )
        if (!mostrou) semAnuncio(id, "anúncio indisponível")
    }

    private fun iniciar(base: AiGenerationSession) {
        val s = por(base.copy(generationStarted = true, status = SessaoStatus.Gerando, erro = null))
        val id = s.generationId
        iniciarGeracao(s,
            { job -> sessoes[id]?.let { por(it.copy(jobId = job)) } },
            { arquivo, erro, repetir -> terminou(id, arquivo, erro, repetir) })
    }

    private fun terminou(id: String, arquivo: File?, erro: String?, repetirSemAnuncio: Boolean) {
        val s = sessoes[id] ?: return
        if (erro != null || arquivo == null) {
            por(s.copy(generationCompleted = false, result = null, erro = erro ?: "geracao_falhou",
                status = SessaoStatus.Falhou, podeRepetirSemAnuncio = repetirSemAnuncio && s.rewardEarned))
            return
        }
        avaliar(s.copy(generationCompleted = true, result = arquivo))
    }

    private fun avaliar(s: AiGenerationSession): AiGenerationSession {
        val status = when {
            s.erro != null -> SessaoStatus.Falhou
            s.liberado -> SessaoStatus.Liberado
            s.generationStarted -> SessaoStatus.Gerando
            s.adClosedEarly -> SessaoStatus.SemRecompensa
            else -> s.status
        }
        return por(s.copy(status = status))
    }
}
