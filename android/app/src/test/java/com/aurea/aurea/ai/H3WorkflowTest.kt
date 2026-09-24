package com.aurea.aurea.ai

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * O workflow do MiniMax H3 que vai no `POST /prompt`: as mesmas contas do
 * template (ResolutionSelector e a expressão de duração) e os valores da tela
 * nos nós certos. Lê o MESMO asset que o app envia.
 */
class H3WorkflowTest {

    private val asset = JSONObject(File("src/main/assets/ai/minimax_h3_api.json").readText())

    @Test
    fun `dimensoes batem com a tabela do template`() {
        assertEquals(608 to 352, H3Workflow.dimensoes("16:9", "preview"))    // 0,2 MP
        assertEquals(864 to 480, H3Workflow.dimensoes("16:9", "standard"))   // 0,4 MP (padrão)
        assertEquals(1344 to 768, H3Workflow.dimensoes("16:9", "high"))      // 0,98 MP = 768p oficial
    }

    @Test
    fun `quadros seguem a grade 17k+5 do H3`() {
        assertEquals(124, H3Workflow.quadros(5))
        assertEquals(243, H3Workflow.quadros(10))
        assertEquals(362, H3Workflow.quadros(15))
        listOf(5, 10, 15).forEach { assertEquals(5, H3Workflow.quadros(it) % 17) }
    }

    @Test
    fun `texto para video poe prompt, tamanho, quadros, semente e turbo nos nos certos`() {
        val g = H3Workflow.montar(asset, Pedido(
            modo = "text_to_video", prompt = "um cachorro na praia", duracao = 5,
            aspecto = "9:16", resolucao = "standard", semente = 42, turbo = true,
        ), null)
        assertFalse(g.has("_aurea"))
        val n131 = g.getJSONObject("131").getJSONObject("inputs")
        assertEquals("um cachorro na praia", n131.getString("prompt"))
        assertEquals(480, n131.getInt("width"))
        assertEquals(864, n131.getInt("height"))
        assertEquals(124, n131.getInt("length"))
        assertFalse(n131.has("first_frame"))
        assertEquals(42L, g.getJSONObject("129").getJSONObject("inputs").getLong("noise_seed"))
        assertTrue(g.getJSONObject("139").getJSONObject("inputs").getBoolean("value"))
        assertEquals("MiniMaxH3ImageToVideo", g.getJSONObject("131").getString("class_type"))
        assertEquals("SaveVideo", g.getJSONObject("92").getString("class_type"))
    }

    @Test
    fun `imagem para video liga o LoadImage no first_frame`() {
        val g = H3Workflow.montar(asset, Pedido(
            modo = "image_to_video", prompt = "x", duracao = 5,
            aspecto = "16:9", resolucao = "standard",
        ), "aurea_ab12cd34.png")
        assertEquals("LoadImage", g.getJSONObject("8").getString("class_type"))
        assertEquals("aurea_ab12cd34.png", g.getJSONObject("8").getJSONObject("inputs").getString("image"))
        val ff = g.getJSONObject("131").getJSONObject("inputs").getJSONArray("first_frame")
        assertEquals("8", ff.getString(0))
        assertEquals(0, ff.getInt(1))
    }

    @Test
    fun `saida de video do history e achada (SaveVideo entrega em images)`() {
        val reg = JSONObject("""{"status":{"status_str":"success","completed":true},
            "outputs":{"92":{"images":[{"filename":"h3_00001_.mp4","subfolder":"aurea","type":"output"}],"animated":[true]}}}""")
        assertEquals(listOf(ComfyArquivo("h3_00001_.mp4", "aurea", "output")), ComfyCliente.videosDoHistorico(reg))
        assertEquals(null, ComfyCliente.erroDoHistorico(reg))
    }

    /**
     * O asset do app tem de ser o workflow que GEROU vídeo de verdade no servidor
     * (docs/ai/H3_WORKFLOW_REAL_FUNCIONANDO_API.json, lido do /history de uma
     * geração bem-sucedida): mesmos nós, classes, modelos e ligações. Só os
     * valores do pedido (texto, tamanho, quadros, semente, turbo, prefixo) mudam.
     */
    @Test
    fun `asset e o workflow real que gerou video (mesmos nos, modelos e ligacoes)`() {
        val real = JSONObject(File("../../docs/ai/H3_WORKFLOW_REAL_FUNCIONANDO_API.json").readText())
        val app = JSONObject(asset.toString()).apply { remove("_aurea") }
        val doPedido = setOf("131.prompt", "131.width", "131.height", "131.length", "129.noise_seed",
            "139.value", "130.fps", "92.filename_prefix")
        assertEquals(real.keys().asSequence().toSortedSet(), app.keys().asSequence().toSortedSet())
        for (no in real.keys()) {
            val r = real.getJSONObject(no)
            val a = app.getJSONObject(no)
            assertEquals("classe do nó $no", r.getString("class_type"), a.getString("class_type"))
            val ri = r.getJSONObject("inputs")
            val ai = a.getJSONObject("inputs")
            assertEquals("entradas do nó $no", ri.keys().asSequence().toSortedSet(), ai.keys().asSequence().toSortedSet())
            for (k in ri.keys()) {
                if ("$no.$k" in doPedido) continue
                val rv = ri.get(k)
                val av = ai.get(k)
                if (rv is Number && av is Number) assertEquals("$no.$k", rv.toDouble(), av.toDouble(), 0.0)
                else assertEquals("$no.$k", rv.toString(), av.toString())
            }
        }
        assertFalse("nó inexistente no ComfyUI", app.toString().contains("MiniMaxH3Sampler"))
    }
}
