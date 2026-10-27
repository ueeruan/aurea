package com.aurea.aurea.editor.timeline

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs

/**
 * 0.3.6 (editor): marcas da régua sem sobreposição e a camada escolhida trazida
 * para a parte da timeline que o painel por cima não cobre.
 */
class Bugs036TimelineTest {
    private fun ticks(sec: Long, fps: Float, pps: Float): List<Long> {
        val out = ArrayList<Long>()
        OmegaRuler.frameTicks(sec, fps, pps) { out += it }
        return out
    }

    @Test fun frameTicksNeverSitOnTopOfTheSecondOrHalfTicks() {
        for (fps in listOf(24f, 25f, 29.97f, 30f, 50f, 59.94f, 60f)) {
            for (pps in listOf(8f, 10f, 15f, 20f, 40f, 60f, 100f, 200f, 800f)) {
                val ppfDp = pps / fps
                for (sec in 0L..3L) {
                    val first = OmegaRuler.frameOf(sec.toDouble(), fps)
                    val half = OmegaRuler.frameOf(sec + 0.5, fps)
                    val next = OmegaRuler.frameOf(sec + 1.0, fps)
                    val list = ticks(sec, fps, pps)
                    for (f in list) {
                        val label = "fps $fps pps $pps sec $sec frame $f"
                        assertTrue(label, f > first && f < next)
                        // Ao menos 4 dp de qualquer tique maior (segundo e meio segundo).
                        assertTrue(label, (f - first) * ppfDp >= OmegaRuler.MIN_TICK_DP - 1e-3f)
                        assertTrue(label, abs(f - half) * ppfDp >= OmegaRuler.MIN_TICK_DP - 1e-3f)
                        assertTrue(label, (next - f) * ppfDp >= OmegaRuler.MIN_TICK_DP - 1e-3f)
                        assertEquals(label, OmegaRuler.FRAME, OmegaRuler.tickClass(f, fps))
                    }
                    // Contados do começo do segundo: o mesmo passo a partir dele.
                    val step = OmegaRuler.frameStep(pps, fps).toLong()
                    for (f in list) assertEquals("fps $fps pps $pps", 0L, (f - first) % step)
                }
            }
        }
    }

    @Test fun closeZoomStillDrawsEveryFrameBetweenTheBigTicks() {
        // 30 fps a 800 dp/s: 26,7 dp por quadro — todos os quadros que não são segundo nem meio.
        val list = ticks(0L, 30f, 800f)
        assertEquals((1L..29L).filter { it != 15L }, list)
    }

    @Test fun theOldPowerOfTwoStepLandedNextToTheHalfSecond() {
        // 30 fps a 15 dp/s: passo 8 solto (0, 8, 16…) punha um tique no 16, colado no meio (15).
        val list = ticks(0L, 30f, 15f)
        assertTrue(list.none { abs(it - 15L) * (15f / 30f) < OmegaRuler.MIN_TICK_DP })
    }

    @Test fun revealKeepsTheRowAboveThePanel() {
        val viewport = TimelineReveal.viewport(height = 280f, rowsTop = 42f, obscured = 200f)
        assertEquals(38f, viewport, 0.01f)
        // Fileira 3 (de 28) com a lista no topo: some atrás do painel → a rolagem a traz.
        val top = 3 * 28f
        val target = TimelineReveal.target(top, top + 28f, 0f, viewport)
        assertEquals(top + 28f - viewport, target, 0.01f)
        // Sem o painel (vista inteira) ela já estava à vista.
        val whole = TimelineReveal.viewport(280f, 42f, 0f)
        assertEquals(0f, TimelineReveal.target(top, top + 28f, 0f, whole), 0.01f)
        // Acima da vista: sobe até o topo dela; fileira maior que a vista: mostra o topo.
        assertEquals(10f, TimelineReveal.target(10f, 38f, 50f, viewport), 0.01f)
        assertEquals(100f, TimelineReveal.target(100f, 200f, 0f, viewport), 0.01f)
        // Sem vista (não medida ainda): não mexe.
        assertEquals(7f, TimelineReveal.target(100f, 128f, 7f, 0f), 0.01f)
    }
}
