package com.aurea.aurea.ai

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * O discovery da Aurea AI (`GET DISCOVERY_URL`) e as regras de estado que a tela
 * mostra. O JSON abaixo é a resposta REAL do discovery (formato de 2026-09-24).
 */
class DiscoveryTest {

    private val real = """
        {
          "online": true,
          "endpoint": "https://exemplo-de-tunel.trycloudflare.com",
          "model": "MiniMax H3",
          "gpu": "A100",
          "capabilities": ["text_to_video", "image_to_video"]
        }
    """.trimIndent()

    @Test
    fun `le a resposta real do discovery sem token`() {
        val d = Discovery.ler(real)!!
        assertTrue(d.online)
        assertEquals("https://exemplo-de-tunel.trycloudflare.com", d.endpoint)
        assertEquals("MiniMax H3", d.modelo)
        assertEquals("A100", d.gpu)
        assertEquals(listOf("text_to_video", "image_to_video"), d.capacidades)
        assertEquals("", d.appToken)
        assertTrue(d.valido())
    }

    @Test
    fun `endpoint com barra no fim nao vira barra dupla depois`() {
        val d = Discovery.ler(real.replace(".com\"", ".com/\""))!!
        assertEquals("https://exemplo-de-tunel.trycloudflare.com", d.endpoint)
    }

    @Test
    fun `online false nao e valido`() {
        val d = Discovery.ler(real.replace("\"online\": true", "\"online\": false"))!!
        assertFalse(d.online)
        assertFalse(d.valido())
    }

    @Test
    fun `endpoint sem https e recusado`() {
        val d = Discovery.ler(real.replace("https://", "http://"))!!
        assertFalse(d.valido())
    }

    @Test
    fun `texto que nao e json devolve nulo em vez de explodir`() {
        assertNull(Discovery.ler("<html>portal de wifi</html>"))
        assertNull(Discovery.ler(""))
    }

    @Test
    fun `capacidades do discovery montam a tela com os valores do contrato`() {
        val c = Capacidades.doDiscovery(listOf("text_to_video", "image_to_video", "outra"))
        assertEquals(listOf("text_to_video", "image_to_video"), c.modos)
        assertEquals(listOf(5, 10, 15), c.duracoes)
        assertTrue(c.temImagemParaVideo())
    }

    @Test
    fun `tentativas - na hora, depois 2 s, depois a cada 5 s`() {
        assertEquals(2L, recuo(0))
        assertEquals(5L, recuo(1))
        assertEquals(5L, recuo(10))
    }


    @Test
    fun `so online e gerando deixam gerar`() {
        assertTrue(AureaAiEstado.Connected.podeGerar())
        assertTrue(AureaAiEstado.Generating.podeGerar())
        assertFalse(AureaAiEstado.Checking.podeGerar())
        assertFalse(AureaAiEstado.Reconnecting.podeGerar())
        assertFalse(AureaAiEstado.Disconnected.podeGerar())
        assertFalse(AureaAiEstado.Error.podeGerar())
    }

    @Test
    fun `o rotulo diz online, reconectando e offline`() {
        assertEquals("Online", AureaAiEstado.Connected.rotulo())
        assertEquals("Reconectando", AureaAiEstado.Reconnecting.rotulo())
        assertEquals("Offline", AureaAiEstado.Disconnected.rotulo())
    }
}
