package com.aurea.aurea.editor.panels

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Curvas com parâmetros do editor de curva (pedido de beta: a curva que passa
 * do ponto e balança): Overshoot, Elástico configurável e Quique, os botões de
 * tipo e a faixa vertical do gráfico. A conta é a de core/Math.hpp.
 */
class CurveParametricEaseTest {
    private fun samples(e: Ease, n: Int = 2001) = FloatArray(n) { e.transform(it / (n - 1f)) }

    @Test fun overshootPassesTheEndLandsExactlyAndMirrors() {
        val base = Ease(Interp.OVERSHOOT, 0f, 0f, 1f, 1f)
        // Sem marcador: o overshoot padrão (≈ 10 % além do fim).
        assertEquals(1.1f, samples(base).max(), 0.002f)
        for (amount in listOf(.1f, OVERSHOOT_DEFAULT_AMOUNT, .6f, 1f)) {
            val e = base.overshoot(amount)
            assertEquals(EASE_PARAM_MARKER, e.y2, 0f)
            assertEquals(0f, e.transform(0f), 0f)
            assertEquals(1f, e.transform(1f), 0f)
            assertTrue(samples(e).max() > 1f)
            assertTrue(samples(e).min() >= 0f)
        }
        assertTrue(samples(base.overshoot(.8f)).max() > samples(base.overshoot(.2f)).max())
        val out = base.overshoot(.5f)
        assertTrue(out.supportsInversion)
        val inv = out.inverted()!!
        assertEquals(.5f, inv.overshootAmount, 0f)
        for (i in 0..200) {
            val t = i / 200f
            assertEquals(1f - out.transform(1f - t), inv.transform(t), 1e-5f)
        }
        assertTrue(samples(inv).min() < 0f)       // antecipação: recua antes de partir
        assertEquals(out, inv.inverted())
    }

    @Test fun elasticDefaultIsTheLegacyCurveAndParametersChangeIt() {
        val legacy = Ease(Interp.ELASTIC, 0f, 0f, 1f, 1f)
        val def = legacy.elastic()
        assertEquals(3, def.elasticCycles)
        assertEquals(ELASTIC_DEFAULT_DAMPING, def.elasticDamping, 0f)
        for (i in 0..200) assertEquals(legacy.transform(i / 200f), def.transform(i / 200f), 1e-5f)
        // A curva antiga (sem parâmetros) continua sem inversão; a configurável inverte.
        assertFalse(legacy.supportsInversion)
        assertNull(legacy.inverted())
        assertTrue(def.supportsInversion)
        assertNotNull(def.inverted())
        for (cycles in 1..8) for (damping in listOf(0f, .5f, 1f)) {
            val e = def.elastic(cycles, damping)
            assertEquals(cycles, e.elasticCycles)
            assertEquals(0f, e.transform(0f), 0f)
            assertEquals(1f, e.transform(1f), 0f)
            assertTrue(samples(e).max() > 1f)        // passa do fim
        }
        // Assenta: no padrão, os últimos 10 % já estão no valor final.
        for (i in 900..1000) assertEquals(1f, def.transform(i / 1000f), .01f)
        // Mais amortecimento, cauda menor.
        fun tail(e: Ease) = (500..1000).maxOf { kotlin.math.abs(e.transform(it / 1000f) - 1f) }
        assertTrue(tail(def.elastic(4, .9f)) < tail(def.elastic(4, .1f)))
        // Trocar só um parâmetro mantém o outro.
        assertEquals(.8f, def.elastic(damping = .8f).elastic(cycles = 6).elasticDamping, 0f)
    }

    @Test fun bounceNeverPassesTheEnd() {
        for (count in 1..8) for (strength in listOf(.1f, .5f, .9f)) {
            val e = DefaultBounce.bounce(count, strength)
            assertTrue(samples(e).all { it in 0f..1f })
        }
        assertTrue(samples(Ease(Interp.BOUNCE, 0f, 0f, 1f, 1f)).all { it <= 1f })
    }

    @Test fun quickTypesMapEveryCurveAndKeepParametersOnRepeat() {
        assertEquals(CurveQuickType.Linear, quickTypeOf(Ease(Interp.LINEAR, 0f, 0f, 1f, 1f)))
        for (kind in listOf(Interp.BEZIER, Interp.CUSTOM, Interp.EASE_IN, Interp.EASE_OUT, Interp.EASE_IN_OUT))
            assertEquals(CurveQuickType.Ease, quickTypeOf(Ease(kind, .2f, 0f, .8f, 1f)))
        assertEquals(CurveQuickType.Overshoot, quickTypeOf(Ease(Interp.OVERSHOOT, 0f, 0f, 1f, 1f)))
        assertEquals(CurveQuickType.Elastic, quickTypeOf(Ease(Interp.ELASTIC, 0f, 0f, 1f, 1f)))
        assertEquals(CurveQuickType.Bounce, quickTypeOf(DefaultBounce))
        assertNull(quickTypeOf(Ease(Interp.HOLD, 0f, 0f, 1f, 1f)))
        assertNull(quickTypeOf(Ease(Interp.STEPS, 0f, 0f, 1f, 1f)))
        val bezier = Ease(Interp.BEZIER, .1f, .9f, .3f, 1f, 2)
        // Cada botão aplica a família dele; parâmetros com o marcador.
        for (type in CurveQuickType.entries) {
            val e = quickTypeEase(type, bezier)
            assertEquals(type, quickTypeOf(e))
            if (e.isParametric) assertEquals(EASE_PARAM_MARKER, e.y2, 0f)
        }
        // Tocar no tipo já escolhido não muda a curva (nem os parâmetros).
        assertEquals(bezier, quickTypeEase(CurveQuickType.Ease, bezier))
        val springy = Ease(Interp.ELASTIC, 0f, 0f, 1f, 1f).elastic(7, .6f)
        assertEquals(springy, quickTypeEase(CurveQuickType.Elastic, springy))
        val far = Ease(Interp.OVERSHOOT, 0f, 0f, 1f, 1f).overshoot(.9f)
        assertEquals(far, quickTypeEase(CurveQuickType.Overshoot, far))
        val bouncy = DefaultBounce.bounce(6, .8f)
        assertEquals(bouncy, quickTypeEase(CurveQuickType.Bounce, bouncy))
        // Vindo de outra família, os parâmetros de lá não vazam (o x1 do overshoot não vira saltos).
        assertEquals(DefaultBounce, quickTypeEase(CurveQuickType.Bounce, far))
        assertEquals(OVERSHOOT_DEFAULT_AMOUNT, quickTypeEase(CurveQuickType.Overshoot, springy).overshootAmount, 0f)
        assertEquals(ELASTIC_DEFAULT_CYCLES, quickTypeEase(CurveQuickType.Elastic, bouncy).elasticCycles)
    }

    @Test fun graphRangeGrowsToShowTheWholeOvershootAndEveryOscillation() {
        val over = Ease(Interp.OVERSHOOT, 0f, 0f, 1f, 1f).overshoot(1f)
        val (lo, hi) = easeRange(over)
        assertTrue(hi > samples(over).max())
        assertTrue(lo < 0f)
        // Elástico de 8 oscilações: 41 pontos perdiam picos; a faixa cobre todos.
        val spring = Ease(Interp.ELASTIC, 0f, 0f, 1f, 1f).elastic(8, 0f)
        val (sLo, sHi) = easeRange(spring)
        val dense = samples(spring, 20001)
        assertTrue(sHi >= dense.max())
        assertTrue(sLo <= dense.min())
        // Com a curva do motor, a faixa é a dos pontos desenhados.
        val drawn = floatArrayOf(0f, 1.5f, -.25f, 1f)
        val (dLo, dHi) = easeRange(Ease(Interp.LINEAR, 0f, 0f, 1f, 1f).copy(interp = Interp.ELASTIC), drawn)
        assertEquals(-.25f - 1.75f * .08f, dLo, 1e-5f)
        assertEquals(1.5f + 1.75f * .08f, dHi, 1e-5f)
    }

    @Test fun playheadPointSitsOnTheDrawnPolyline() {
        val s = floatArrayOf(0f, 1f, .5f)
        assertEquals(0f, easeSampleAt(s, 0f), 0f)
        assertEquals(.5f, easeSampleAt(s, .25f), 1e-6f)
        assertEquals(1f, easeSampleAt(s, .5f), 1e-6f)
        assertEquals(.75f, easeSampleAt(s, .75f), 1e-6f)
        assertEquals(.5f, easeSampleAt(s, 1f), 0f)
        assertEquals(.5f, easeSampleAt(s, 2f), 0f)
    }

    @Test fun savedPresetKeepsTheParametersAndTheMarker() {
        for (e in listOf(Ease(Interp.OVERSHOOT, 0f, 0f, 1f, 1f).overshoot(.7f).inverted()!!,
                         Ease(Interp.ELASTIC, 0f, 0f, 1f, 1f).elastic(5, .4f), DefaultBounce.bounce(2, .3f))) {
            val v = curvePresetValues(e)
            assertArrayEquals(floatArrayOf(e.interp.toFloat(), e.x1, e.y1, e.x2, EASE_PARAM_MARKER, 1f), v, 0f)
            val back = curvePresetEase(v)
            assertTrue(back.same(e))
            for (i in 0..50) assertEquals(e.transform(i / 50f), back.transform(i / 50f), 0f)
        }
    }
}
