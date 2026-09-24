package com.aurea.aurea.ai

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import java.io.File

/**
 * O direito ao vídeo da IA: Rewarded + geração em paralelo. Anúncio e H3 são
 * falsos e controlados passo a passo — o que se testa é a regra:
 * vídeo só com `generationCompleted && rewardEarned`, e recompensa só por
 * onUserEarnedReward.
 */
class AiRewardFlowTest {

    /** Rewarded falso: o teste decide carregar, abrir, recompensar e fechar. */
    private class AnuncioFalso : RewardedAds {
        var carregado = false
        var falharCarga: String? = null
        var mostrados = 0
        var abrir: (() -> Unit)? = null
        var recompensa: (() -> Unit)? = null
        var fechar: (() -> Unit)? = null
        val esperando = mutableListOf<Pair<() -> Unit, (String) -> Unit>>()

        override fun pronto() = carregado
        override fun carregar(aoCarregar: () -> Unit, aoFalhar: (String) -> Unit) {
            falharCarga?.let { aoFalhar(it); return }
            esperando += aoCarregar to aoFalhar
        }
        fun chegou() { carregado = true; esperando.toList().also { esperando.clear() }.forEach { it.first() } }
        override fun mostrar(aoAbrir: () -> Unit, aoRecompensa: () -> Unit, aoFechar: () -> Unit, aoFalhar: (String) -> Unit): Boolean {
            if (!carregado) return false
            carregado = false            // consumido
            mostrados++
            abrir = aoAbrir; recompensa = aoRecompensa; fechar = aoFechar
            return true
        }
    }

    private class H3Falso {
        val chamadas = mutableListOf<String>()                           // generationIds
        val fins = mutableMapOf<String, (File?, String?) -> Unit>()
        val pids = mutableMapOf<String, (String) -> Unit>()
        fun iniciar(s: AiGenerationSession, pid: (String) -> Unit, fim: (File?, String?) -> Unit) {
            chamadas += s.generationId; fins[s.generationId] = fim; pids[s.generationId] = pid
        }
    }

    private lateinit var ad: AnuncioFalso
    private lateinit var h3: H3Falso
    private lateinit var flow: AiRewardFlow
    private val mudancas = mutableListOf<AiGenerationSession>()
    private var seq = 0
    private val pedido = Pedido(modo = "text_to_video", prompt = "um gato", duracao = 5, aspecto = "16:9", resolucao = "standard")
    private val video = File("h3_00001_.mp4")

    @Before
    fun preparar() {
        ad = AnuncioFalso()
        h3 = H3Falso()
        mudancas.clear()
        flow = AiRewardFlow(ad, h3::iniciar, { mudancas += it }, { "g${++seq}" })
    }

    private fun s(id: String) = flow.sessao(id)!!

    // 1
    @Test
    fun `anuncio completo e geracao completa liberam o video`() {
        ad.carregado = true
        val id = flow.gerar(pedido).generationId
        ad.abrir!!()
        assertEquals(listOf(id), h3.chamadas)
        ad.recompensa!!(); ad.fechar!!()
        h3.fins.getValue(id)(video, null)
        assertTrue(s(id).liberado)
        assertEquals(SessaoStatus.Liberado, s(id).status)
    }

    // 2
    @Test
    fun `recompensa antes do fim do H3 - aguarda e libera depois`() {
        ad.carregado = true
        val id = flow.gerar(pedido).generationId
        ad.abrir!!(); ad.recompensa!!(); ad.fechar!!()
        assertTrue(s(id).rewardEarned)
        assertFalse(s(id).liberado)
        assertEquals("\"Seu vídeo está sendo finalizado...\"", SessaoStatus.Gerando, s(id).status)
        h3.fins.getValue(id)(video, null)
        assertEquals(SessaoStatus.Liberado, s(id).status)
    }

    // 3
    @Test
    fun `H3 termina antes da recompensa - libera quando a recompensa chega`() {
        ad.carregado = true
        val id = flow.gerar(pedido).generationId
        ad.abrir!!()
        h3.fins.getValue(id)(video, null)
        assertEquals(SessaoStatus.Bloqueado, s(id).status)
        assertFalse(s(id).liberado)
        ad.recompensa!!()
        assertEquals(SessaoStatus.Liberado, s(id).status)
    }

    // 4 e 5
    @Test
    fun `fechar o anuncio cedo bloqueia o video e a geracao continua`() {
        ad.carregado = true
        val id = flow.gerar(pedido).generationId
        ad.abrir!!(); ad.fechar!!()                       // sem recompensa
        assertTrue(s(id).adClosedEarly)
        assertEquals("a geração não foi cancelada", SessaoStatus.Gerando, s(id).status)
        h3.fins.getValue(id)(video, null)                 // o H3 terminou mesmo assim
        assertEquals(SessaoStatus.Bloqueado, s(id).status)
        assertFalse(s(id).liberado)
        assertEquals("resultado não foi apagado", video, s(id).result)
    }

    // 6 e 7
    @Test
    fun `novo anuncio libera o MESMO video e nao gera de novo`() {
        ad.carregado = true
        val id = flow.gerar(pedido).generationId
        ad.abrir!!(); ad.fechar!!()
        h3.fins.getValue(id)(video, null)
        flow.liberarComAnuncio(id)                       // não há anúncio pronto: carrega
        assertEquals(1, ad.esperando.size)
        ad.chegou()
        ad.abrir!!(); ad.recompensa!!(); ad.fechar!!()
        assertEquals(SessaoStatus.Liberado, s(id).status)
        assertEquals(video, s(id).result)
        assertEquals("uma geração só", 1, h3.chamadas.size)
        assertEquals(2, ad.mostrados)
    }

    // 8
    @Test
    fun `sem Rewarded a geracao nao comeca`() {
        val id = flow.gerar(pedido).generationId
        assertEquals("\"Preparando geração...\"", SessaoStatus.Preparando, s(id).status)
        assertTrue(h3.chamadas.isEmpty())
        // AdMob indisponível: erro amigável, nada gerado, dá para tentar de novo.
        ad.esperando.toList().forEach { it.second("load 2: network error") }
        assertEquals(SessaoStatus.AnuncioIndisponivel, s(id).status)
        assertTrue(h3.chamadas.isEmpty())
        flow.tentarDeNovo(id)
        ad.chegou()
        assertTrue("só gera com o anúncio NA TELA", h3.chamadas.isEmpty())
        ad.abrir!!()
        assertEquals(listOf(id), h3.chamadas)
    }

    // 9
    @Test
    fun `duas geracoes nao compartilham recompensa`() {
        ad.carregado = true
        val a = flow.gerar(pedido).generationId
        ad.abrir!!(); val recompensaA = ad.recompensa!!; ad.fechar!!()      // A: fechou cedo
        ad.carregado = true
        val b = flow.gerar(pedido.copy(prompt = "um cachorro")).generationId
        ad.abrir!!(); ad.recompensa!!(); ad.fechar!!()                       // B: recompensada
        h3.fins.getValue(a)(File("a.mp4"), null)
        h3.fins.getValue(b)(File("b.mp4"), null)
        assertEquals(SessaoStatus.Bloqueado, s(a).status)
        assertEquals(SessaoStatus.Liberado, s(b).status)
        assertEquals(File("b.mp4"), s(b).result)
        assertFalse(s(a).rewardEarned)
        assertTrue(recompensaA !== ad.recompensa)
    }

    // 10
    @Test
    fun `o estado sobrevive a tela sair e voltar (esta no fluxo, nao na tela)`() {
        ad.carregado = true
        val id = flow.gerar(pedido).generationId
        ad.abrir!!(); ad.recompensa!!(); ad.fechar!!()
        // A "tela" some e volta: quem desenha relê a sessão pelo id.
        val relida = flow.sessao(id)!!
        assertTrue(relida.rewardEarned)
        h3.fins.getValue(id)(video, null)
        assertTrue(flow.sessao(id)!!.liberado)
    }

    // 11
    @Test
    fun `erro do H3 nao concede resultado inexistente`() {
        ad.carregado = true
        val id = flow.gerar(pedido).generationId
        ad.abrir!!(); ad.recompensa!!(); ad.fechar!!()
        h3.fins.getValue(id)(null, "execution_error no nó 125")
        assertEquals(SessaoStatus.Falhou, s(id).status)
        assertFalse(s(id).liberado)
        assertNull(s(id).result)
        flow.liberarComAnuncio(id)
        assertEquals("nada a liberar, nenhum anúncio novo", 1, ad.mostrados)
    }

    // 12 e 13
    @Test
    fun `onAdDismissed nao concede e so onUserEarnedReward concede`() {
        ad.carregado = true
        val id = flow.gerar(pedido).generationId
        ad.abrir!!()
        h3.fins.getValue(id)(video, null)
        assertFalse("abrir não concede", s(id).rewardEarned)
        ad.fechar!!()
        assertFalse("fechar não concede", s(id).rewardEarned)
        assertFalse(s(id).liberado)
        assertTrue(mudancas.none { it.generationId == id && it.status == SessaoStatus.Liberado })
    }

    @Test
    fun `o prompt_id do ComfyUI fica na sessao certa`() {
        ad.carregado = true
        val id = flow.gerar(pedido).generationId
        ad.abrir!!()
        h3.pids.getValue(id)("aaa8f675")
        assertEquals("aaa8f675", s(id).promptId)
    }
}
