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
    /** Esperando um Rewarded carregar. A geração NÃO começou. */
    Preparando,
    /** Não deu para ter anúncio (rede, AdMob). A geração NÃO começou; dá para tentar de novo. */
    AnuncioIndisponivel,
    /** Anúncio na tela; o H3 já está gerando por baixo. */
    AnuncioNaTela,
    /** H3 gerando; o anúncio já fechou. */
    Gerando,
    /** Vídeo pronto e ainda bloqueado: falta a recompensa. */
    Bloqueado,
    /** Vídeo pronto e liberado. */
    Liberado,
    /** O H3 falhou: não há vídeo para liberar. */
    Falhou,
}

/**
 * UMA geração. Cada uma tem o próprio id: a recompensa do anúncio de uma nunca
 * libera outra. Imutável — cada mudança vira uma cópia nova.
 */
data class AiGenerationSession(
    val generationId: String,
    val pedido: Pedido,
    val promptId: String? = null,
    val rewardEarned: Boolean = false,
    val generationCompleted: Boolean = false,
    val result: File? = null,
    val status: SessaoStatus = SessaoStatus.Preparando,
    /** O H3 já foi chamado para esta sessão (uma vez só, nunca de novo). */
    val generationStarted: Boolean = false,
    /** O usuário fechou o anúncio antes da recompensa. */
    val adClosedEarly: Boolean = false,
    /** Erro do H3 (não há vídeo). */
    val erro: String? = null,
    /** Por que não houve anúncio da última vez (a tela explica e oferece tentar de novo). */
    val adError: String? = null,
) {
    /** A ÚNICA regra de liberação. */
    val liberado: Boolean get() = generationCompleted && rewardEarned && result != null && erro == null
}

/**
 * A camada de "direito ao vídeo" POR CIMA da geração: não sabe nada de H3 nem
 * de AdMob. Regras:
 *  - a geração só começa quando o anúncio for EFETIVAMENTE apresentado (nunca
 *    antes de haver anúncio: não se gasta A100 sem anúncio para mostrar);
 *  - a recompensa só vem de `aoRecompensa` (onUserEarnedReward) — fechar ou
 *    abrir o anúncio não conta, tempo não conta;
 *  - o vídeo só é entregue com `generationCompleted && rewardEarned`;
 *  - fechar o anúncio cedo não cancela a geração, não apaga o resultado e não
 *    gera de novo: o resultado fica bloqueado até um novo anúncio recompensar.
 *
 * Tudo roda na thread principal (os callbacks do AdMob e do estado já vêm nela).
 */
class AiRewardFlow(
    private val ads: RewardedAds,
    /** Começa a geração REAL desta sessão. Chama `aoTerminar` uma vez: arquivo, ou erro. */
    private val iniciarGeracao: (sessao: AiGenerationSession, aoPromptId: (String) -> Unit,
                                 aoTerminar: (arquivo: File?, erro: String?) -> Unit) -> Unit,
    /** Toda mudança de uma sessão (para a tela). */
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

    /** Toque em "Gerar". Devolve a sessão nova. */
    fun gerar(pedido: Pedido, id: String = novoId()): AiGenerationSession {
        require(id !in sessoes) { "sessão repetida" }
        val s = por(AiGenerationSession(id, pedido))
        prepararEApresentar(s.generationId, liberar = false)
        return sessoes.getValue(s.generationId)
    }

    /** "Tentar de novo" de uma sessão que ficou sem anúncio (a geração não tinha começado). */
    fun tentarDeNovo(id: String) {
        val s = sessoes[id] ?: return
        if (s.generationStarted || s.status != SessaoStatus.AnuncioIndisponivel) return
        por(s.copy(status = SessaoStatus.Preparando, adError = null))
        prepararEApresentar(id, liberar = false)
    }

    /**
     * "Assistir e liberar vídeo": outro Rewarded para a MESMA sessão. Não gera
     * de novo — a recompensa só destrava o resultado que já existe (ou que está
     * para existir).
     */
    fun liberarComAnuncio(id: String) {
        val s = sessoes[id] ?: return
        if (s.rewardEarned || s.erro != null) return
        prepararEApresentar(id, liberar = true)
    }

    private fun prepararEApresentar(id: String, liberar: Boolean) {
        if (ads.pronto()) { apresentar(id, liberar); return }
        sessoes[id]?.let { if (!liberar) por(it.copy(status = SessaoStatus.Preparando)) }
        ads.carregar(
            aoCarregar = { apresentar(id, liberar) },
            aoFalhar = { e -> semAnuncio(id, liberar, e) },
        )
    }

    private fun semAnuncio(id: String, liberar: Boolean, e: String) {
        val s = sessoes[id] ?: return
        // Sem anúncio: a geração não começa; na liberação, o vídeo segue como estava.
        if (!liberar && !s.generationStarted) por(s.copy(status = SessaoStatus.AnuncioIndisponivel, adError = e))
        else por(s.copy(adError = e))
    }

    private fun apresentar(id: String, liberar: Boolean) {
        if (sessoes[id] == null) return
        sessoes[id]?.let { if (it.adError != null) por(it.copy(adError = null)) }
        val mostrou = ads.mostrar(
            aoAbrir = {
                val s = sessoes[id] ?: return@mostrar
                if (!liberar && !s.generationStarted) {
                    // O anúncio ESTÁ na tela: agora sim o H3 começa, em paralelo.
                    por(s.copy(generationStarted = true, status = SessaoStatus.AnuncioNaTela))
                    iniciarGeracao(sessoes.getValue(id),
                        { pid -> sessoes[id]?.let { por(it.copy(promptId = pid)) } },
                        { arquivo, erro -> terminou(id, arquivo, erro) })
                } else if (!s.generationCompleted) {
                    por(s.copy(status = SessaoStatus.AnuncioNaTela))
                }
            },
            aoRecompensa = {
                val s = sessoes[id] ?: return@mostrar
                avaliar(s.copy(rewardEarned = true))
            },
            aoFechar = {
                val s = sessoes[id] ?: return@mostrar
                avaliar(if (s.rewardEarned) s else s.copy(adClosedEarly = true))
            },
            aoFalhar = { e -> semAnuncio(id, liberar, e) },
        )
        // `false` = não havia anúncio (e nenhum callback é chamado).
        if (!mostrou) semAnuncio(id, liberar, "anúncio indisponível")
    }

    private fun terminou(id: String, arquivo: File?, erro: String?) {
        val s = sessoes[id] ?: return
        if (erro != null || arquivo == null) {
            // Erro do H3: nada é liberado — não existe resultado.
            por(s.copy(generationCompleted = false, result = null, erro = erro ?: "sem vídeo", status = SessaoStatus.Falhou))
            return
        }
        avaliar(s.copy(generationCompleted = true, result = arquivo))
    }

    /** Recalcula o status a partir dos DOIS estados independentes. */
    private fun avaliar(s: AiGenerationSession) {
        val status = when {
            s.erro != null -> SessaoStatus.Falhou
            s.liberado -> SessaoStatus.Liberado
            s.generationCompleted -> SessaoStatus.Bloqueado
            s.status == SessaoStatus.AnuncioNaTela && !s.rewardEarned && !s.adClosedEarly -> SessaoStatus.AnuncioNaTela
            else -> SessaoStatus.Gerando
        }
        por(s.copy(status = status))
    }
}
