package com.aurea.aurea.ai

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import java.io.File

/**
 * A geração PAGA e o Rewarded. Anúncio, servidor e geração são controlados
 * passo a passo pelo teste — o que se prende é a regra de dinheiro:
 * nada é gerado antes da recompensa, uma recompensa gera uma vez, e erro
 * técnico depois dela não pede outro anúncio.
 */
class AiRewardFlowTest {

    private class AnuncioFalso : RewardedAds {
        var carregado = false
        var falharCarga: String? = null
        var mostrados = 0
        var abrir: (() -> Unit)? = null
        var recompensa: (() -> Unit)? = null
        var fechar: (() -> Unit)? = null
        override fun pronto() = carregado
        override fun carregar(aoCarregar: () -> Unit, aoFalhar: (String) -> Unit) {
            falharCarga?.let { aoFalhar(it); return }
            carregado = true
            aoCarregar()
        }
        override fun mostrar(aoAbrir: () -> Unit, aoRecompensa: () -> Unit, aoFechar: () -> Unit, aoFalhar: (String) -> Unit): Boolean {
            if (!carregado) return false
            carregado = false
            mostrados++
            abrir = aoAbrir; recompensa = aoRecompensa; fechar = aoFechar
            aoAbrir()
            return true
        }
    }

    /** O servidor: tickets e gerações pedidas (cada item de `geracoes` custaria dinheiro). */
    private class ServidorFalso {
        var recusarTicket: String? = null
        val tickets = mutableListOf<String>()
        val amarrados = mutableListOf<String>()
        val geracoes = mutableListOf<String>()              // tickets usados para gerar
        val fins = mutableListOf<(File?, String?, Boolean) -> Unit>()
        fun ticket(s: AiGenerationSession, ok: (String) -> Unit, falha: (String) -> Unit) {
            recusarTicket?.let { falha(it); return }
            val t = "T${tickets.size + 1}"
            tickets += t
            ok(t)
        }
        fun gerar(s: AiGenerationSession, aoJob: (String) -> Unit, fim: (File?, String?, Boolean) -> Unit) {
            geracoes += s.ticket!!
            aoJob("J${geracoes.size}")
            fins += fim
        }
    }

    private lateinit var ad: AnuncioFalso
    private lateinit var srv: ServidorFalso
    private lateinit var flow: AiRewardFlow
    private var seq = 0
    private val pedido = Pedido(modo = "text_to_video", prompt = "um cachorro na praia", duracao = 5, aspecto = "16:9", resolucao = "480p")
    private val video = File("aurea_ai.mp4")

    @Before
    fun preparar() {
        ad = AnuncioFalso()
        srv = ServidorFalso()
        flow = AiRewardFlow(ad, srv::ticket, { srv.amarrados += it }, srv::gerar, { }, { "g${++seq}" })
    }

    private fun s(id: String) = flow.sessao(id)!!

    @Test
    fun `nada e gerado antes da recompensa`() {
        val id = flow.gerar(pedido).generationId
        assertEquals(SessaoStatus.AnuncioNaTela, s(id).status)
        assertTrue(srv.geracoes.isEmpty())
        // O anúncio foi amarrado ao ticket ANTES de aparecer (Dynamic User ID).
        assertEquals(listOf("T1"), srv.amarrados)
        assertEquals(1, ad.mostrados)
    }

    @Test
    fun `recompensa gera uma vez e o video e liberado`() {
        val id = flow.gerar(pedido).generationId
        ad.recompensa!!()
        ad.recompensa!!()          // callback repetido
        ad.fechar!!()
        assertEquals(listOf("T1"), srv.geracoes)
        assertEquals("J1", s(id).jobId)
        assertEquals(SessaoStatus.Gerando, s(id).status)
        srv.fins.single()(video, null, false)
        assertTrue(s(id).liberado)
        assertEquals(SessaoStatus.Liberado, s(id).status)
    }

    @Test
    fun `fechar cedo nao gera e o mesmo ticket espera outro anuncio`() {
        val id = flow.gerar(pedido).generationId
        ad.fechar!!()
        assertEquals(SessaoStatus.SemRecompensa, s(id).status)
        assertTrue(srv.geracoes.isEmpty())
        flow.assistirDeNovo(id)
        assertEquals(2, ad.mostrados)
        assertEquals(listOf("T1"), srv.tickets)       // nenhum ticket novo
        assertEquals(listOf("T1", "T1"), srv.amarrados)
        ad.recompensa!!()
        assertEquals(listOf("T1"), srv.geracoes)
    }

    @Test
    fun `sem anuncio nada e gerado`() {
        ad.falharCarga = "sem rede"
        val id = flow.gerar(pedido).generationId
        assertEquals(SessaoStatus.AnuncioIndisponivel, s(id).status)
        assertTrue(srv.geracoes.isEmpty())
        ad.falharCarga = null
        flow.assistirDeNovo(id)
        assertEquals(SessaoStatus.AnuncioNaTela, s(id).status)
    }

    @Test
    fun `ticket recusado nao mostra anuncio`() {
        srv.recusarTicket = "limite_diario"
        val id = flow.gerar(pedido).generationId
        assertEquals(SessaoStatus.Falhou, s(id).status)
        assertEquals("limite_diario", s(id).erro)
        assertEquals(0, ad.mostrados)
        assertFalse(s(id).podeRepetirSemAnuncio)
    }

    @Test
    fun `erro tecnico depois da recompensa repete sem outro anuncio`() {
        val id = flow.gerar(pedido).generationId
        ad.recompensa!!()
        srv.fins.single()(null, "provedor_indisponivel", true)
        assertEquals(SessaoStatus.Falhou, s(id).status)
        assertTrue(s(id).podeRepetirSemAnuncio)
        flow.repetirSemAnuncio(id)
        assertEquals(1, ad.mostrados)                  // nenhum anúncio novo
        assertEquals(listOf("T1", "T1"), srv.geracoes) // o MESMO ticket
        assertNull(s(id).erro)
        srv.fins.last()(video, null, false)
        assertTrue(s(id).liberado)
    }

    @Test
    fun `falha que nao se repete nao gera de novo sozinha`() {
        val id = flow.gerar(pedido).generationId
        ad.recompensa!!()
        srv.fins.single()(null, "conteudo_bloqueado", false)
        assertFalse(s(id).podeRepetirSemAnuncio)
        flow.repetirSemAnuncio(id)
        assertEquals(listOf("T1"), srv.geracoes)
    }

    @Test
    fun `retomar nao gera nem mostra anuncio`() {
        val gravada = AiGenerationSession("g9", pedido, ticket = "T9", jobId = "J9", rewardEarned = true,
            generationStarted = true, status = SessaoStatus.Gerando)
        flow.retomar(gravada)
        assertEquals(SessaoStatus.Gerando, s("g9").status)
        assertTrue(srv.geracoes.isEmpty())
        assertEquals(0, ad.mostrados)
    }
}
