package com.aurea.aurea.editor.timeline

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.max
import kotlin.math.min

/**
 * Catraca da auto-rolagem (docs/diagnostico/export-falhas.md, A.3-1 / C2-1):
 * segurar um clipe na borda direita não pode esticar a composição sem fim.
 */
class GestureLimitTest {

    @Test fun `o teto e o maior entre conteudo e duracao mais 10 s`() {
        assertEquals(900L + 300L, GestureLimit.autoScrollLimit(900, 600, 30.0))
        assertEquals(1200L + 600L, GestureLimit.autoScrollLimit(900, 1200, 60.0))
        // 29,97 fps: arredonda a folga para cima (nunca menos que 10 s).
        assertEquals(1000L + 300L, GestureLimit.autoScrollLimit(1000, 0, 29.97))
        // fps inválido cai em 30; valores negativos não reduzem o teto abaixo da folga.
        assertEquals(300L, GestureLimit.autoScrollLimit(-5, -5, Double.NaN))
        // Nunca passa do protocolo de frames.
        assertEquals(Int.MAX_VALUE - 1L, GestureLimit.autoScrollLimit(Int.MAX_VALUE.toLong(), 0, 30.0))
    }

    @Test fun `fora de gesto nada muda`() {
        assertEquals(5_000.5, GestureLimit.clampView(5_000.5, 10.0, GestureLimit.NONE), 0.0)
        assertEquals(Int.MAX_VALUE - 1, GestureLimit.clampFrame(Int.MAX_VALUE - 1, GestureLimit.NONE))
        assertEquals(Int.MAX_VALUE - 1 - 100, GestureLimit.maxShift(100, GestureLimit.NONE))
    }

    @Test fun `a vista nao avanca alem do teto e nao e puxada de volta`() {
        assertEquals(1200.0, GestureLimit.clampView(1250.0, 1190.0, 1200), 0.0)
        assertEquals(1150.0, GestureLimit.clampView(1150.0, 1100.0, 1200), 0.0)
        // Recuar sempre pode.
        assertEquals(1000.0, GestureLimit.clampView(1000.0, 1100.0, 1200), 0.0)
        // Já além do teto (não deveria acontecer): fica onde está, não salta para trás.
        assertEquals(1300.0, GestureLimit.clampView(1400.0, 1300.0, 1200), 0.0)
        assertEquals(1290.0, GestureLimit.clampView(1290.0, 1300.0, 1200), 0.0)
    }

    @Test fun `deslocamento e alvo presos ao teto`() {
        assertEquals(300, GestureLimit.maxShift(900, 1200))
        assertEquals(0, GestureLimit.maxShift(1300, 1200))
        assertEquals(1200, GestureLimit.clampFrame(5000, 1200))
        assertEquals(800, GestureLimit.clampFrame(800, 1200))
    }

    /**
     * Simula o laço de [TimelineController] com o dedo PARADO na borda direita
     * movendo um clipe: a cada quadro a vista anda pela auto-rolagem (limitada
     * também ao `navigationEnd` vivo, como `holdView`), o alvo é reaplicado no
     * dedo parado e o `navigationEnd` passa a ser o fim do próprio clipe.
     */
    private fun holdAtRightEdge(steps: Int, limited: Boolean): Pair<Int, Double> {
        val fps = 30.0
        val ppf = 0.5 // zoom mínimo: muitos quadros por px
        val halfWidth = 180.0 // px do centro até a borda
        val stepPx = AutoScroll.step(1f, 360f * 2.625f, 1f / 60f).toDouble()
        val start = 0
        val length = 300
        var end = 900
        var duration = 900
        var navEnd = end
        val limit = if (limited) GestureLimit.autoScrollLimit(navEnd.toLong(), duration.toLong(), fps) else GestureLimit.NONE
        val maxDelta = GestureLimit.maxShift(end, limit)
        val baseStart = end - length
        var view = 800.0
        // O dedo pegou o clipe 10 px antes da borda; ele fica sob o dedo.
        val grab = baseStart - (view + (halfWidth - 10) / ppf)
        for (i in 0 until steps) {
            val next = view + stepPx / ppf
            view = min(TimeAxis.clampView(GestureLimit.clampView(next, view, limit), 0), navEnd.toDouble())
            val desired = (view + (halfWidth - 10) / ppf + grab).toInt()
            val target = desired.coerceIn(start, baseStart + maxDelta)
            end = target + length
            duration = max(duration, end) // o motor estende a duração
            navEnd = end
        }
        return end to view
    }

    @Test fun `segurar na borda direita nao vira catraca`() {
        // 10 min de dedo parado a 60 Hz.
        val steps = 60 * 600
        val limit = GestureLimit.autoScrollLimit(900, 900, 30.0)
        val (end, view) = holdAtRightEdge(steps, limited = true)
        assertTrue("fim $end passou do teto $limit", end <= limit)
        assertTrue("vista $view passou do teto $limit", view <= limit)
        // O teto realmente é alcançado (a auto-rolagem ainda anda até a folga).
        assertEquals(limit.toInt(), end)
        // Controle: sem o teto, a mesma simulação estica a composição a horas.
        val (free, _) = holdAtRightEdge(steps, limited = false)
        assertTrue("sem teto deveria crescer muito: $free", free > 30 * 3600)
    }

    @Test fun `aparar a borda final segurada na margem respeita o teto`() {
        val limit = GestureLimit.autoScrollLimit(600, 600, 24.0)
        var navEnd = 600
        var view = 590.0
        repeat(100_000) {
            val next = view + 25.0
            view = min(GestureLimit.clampView(next, view, limit), navEnd.toDouble())
            val target = GestureLimit.clampFrame((view + 40).toInt(), limit)
            navEnd = max(navEnd, target)
        }
        assertEquals(limit.toInt(), navEnd)
        assertTrue(view <= limit)
    }
}
