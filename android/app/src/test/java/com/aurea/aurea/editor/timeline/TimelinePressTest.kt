package com.aurea.aurea.editor.timeline

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Intenção do dedo na timeline (densidade 1: px = dp). Reproduz o "às vezes a
 * camada é escolhida e vai junto": uma rolagem um pouco torta sobre o clipe
 * era classificada como MOVER (empate de 45°, `|dx| ≥ |dy|`), e o prazo do
 * toque longo vencia com o dedo rastejando abaixo do slop — o primeiro
 * movimento depois dele levantava (reordenar) ou movia a camada.
 */
class TimelinePressTest {
    private val m = TimelineMetrics(1f)

    @Test
    fun `rolagem um pouco torta sobre o clipe nao move o clipe`() {
        // 1ª amostra além do slop de 8 dp: dx 7, dy 6. A regra antiga (|dx| ≥ |dy|)
        // escolhia o clipe e o movia; agora é scrub (horizontal, mas não edição).
        assertTrue(Press.horizontal(7f, 6f))
        assertFalse(Press.timeEdit(7f, 6f))
        assertFalse(Press.stackEdit(7f, 6f))
        // dx 6, dy 7: rolagem, como antes.
        assertFalse(Press.horizontal(6f, 7f))
        assertFalse(Press.timeEdit(6f, 7f))
    }

    @Test
    fun `so um eixo claro edita`() {
        // Mover/aparar/losango: 2:1 no tempo (≈ 27° de folga).
        assertTrue(Press.timeEdit(70f, 0f))
        assertTrue(Press.timeEdit(30f, 15f))
        assertFalse(Press.timeEdit(30f, 16f))
        // Reordenar depois do toque longo: 2:1 na pilha.
        assertTrue(Press.stackEdit(0f, 23f))
        assertTrue(Press.stackEdit(5f, 10f))
        assertFalse(Press.stackEdit(6f, 10f))
        // Uma diagonal não é nem um nem outro: só rola.
        assertFalse(Press.timeEdit(12f, 10f))
        assertFalse(Press.stackEdit(12f, 10f))
    }

    @Test
    fun `dedo rastejando abaixo do slop nao e toque longo`() {
        val s = Press.Stillness(m.holdJitter)
        s.down(100f, 100f, 0L)
        // 1,2 dp a cada 100 ms: 6 dp em 500 ms, abaixo do slop de 8 — o prazo vencia,
        // o háptico batia e o movimento seguinte virava reordenar/mover.
        for (i in 1..5) s.move(100f, 100f + 1.2f * i, 100L * i)
        assertFalse(s.still(TimelineController.LONG_PRESS_MS))
    }

    @Test
    fun `dedo parado com tremor de 1 dp e toque longo`() {
        val s = Press.Stillness(m.holdJitter)
        s.down(100f, 100f, 0L)
        for (i in 1..5) s.move(100f + if (i % 2 == 0) 1f else -1f, 100.5f, 100L * i)
        assertTrue(s.still(TimelineController.LONG_PRESS_MS))
    }

    @Test
    fun `dedo que assentou antes do prazo e toque longo`() {
        val settled = Press.Stillness(m.holdJitter)
        settled.down(100f, 100f, 0L)
        settled.move(103f, 100f, 200L)
        settled.move(106f, 100f, 350L)
        settled.move(106.5f, 100f, 450L)   // tremor: não conta como movimento
        assertTrue(settled.still(TimelineController.LONG_PRESS_MS))

        val late = Press.Stillness(m.holdJitter)
        late.down(100f, 100f, 0L)
        late.move(103f, 100f, 200L)
        late.move(106f, 100f, 400L)        // ainda andava 100 ms antes do prazo
        assertFalse(late.still(TimelineController.LONG_PRESS_MS))
    }

    @Test
    fun `fileira compacta troca de camada a cada linha inteira do arrasto vertical`() {
        assertEquals(0, Press.compactSteps(45f, 46f))
        assertEquals(1, Press.compactSteps(46f, 46f))
        assertEquals(2, Press.compactSteps(100f, 46f))
        assertEquals(-1, Press.compactSteps(-50f, 46f))
        assertEquals(0, Press.compactSteps(Float.NaN, 46f))
        assertEquals(0, Press.compactSteps(100f, 0f))
    }
}
