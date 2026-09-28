package com.aurea.aurea.editor.panels

import com.aurea.aurea.editor.anchorSnap
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** O editor da curva e o mover do palco com as regras do app antigo. */
class CurveHandleEditTest {
    @Test fun powerRepeatsTheSameBezierLikeTheEngine() {
        val once = Ease(Interp.BEZIER, .42f, 0f, .58f, 1f)
        val twice = once.copy(power = 2)
        val thrice = once.copy(power = 3)
        for (t in listOf(.1f, .25f, .5f, .75f, .9f)) {
            assertEquals(cubicBezier(.42f, 0f, .58f, 1f, once.transform(t)), twice.transform(t), 1e-6f)
            assertEquals(cubicBezier(.42f, 0f, .58f, 1f, twice.transform(t)), thrice.transform(t), 1e-6f)
        }
        assertTrue(twice.transform(.25f) < once.transform(.25f))
        assertEquals(1f, thrice.transform(1f), 0f)
        // Nomeados não têm força; o mesmo nome com força diferente não é a mesma curva.
        assertEquals(.25f * .25f, Ease(Interp.EASE_IN, 0f, 0f, 1f, 1f, 3).transform(.25f), 1e-6f)
        assertTrue(!once.same(twice))
        // Inverter mantém a força.
        assertEquals(2, Ease(Interp.BEZIER, .1f, .6f, .3f, 1f, 2).inverted()!!.power)
    }

    @Test fun tappingTheLabelCyclesPowerAndKeepsTheVisibleHandles() {
        val linear = Ease(Interp.LINEAR, 0f, 0f, 1f, 1f)
        val p2 = nextPower(linear)
        assertEquals(Interp.BEZIER, p2.interp)
        assertEquals(2, p2.power)
        assertEquals(1f / 3f, p2.x1, 1e-6f)
        assertEquals(3, nextPower(p2).power)
        assertEquals(1, nextPower(nextPower(p2)).power)
        assertEquals("cubic-bezier(0.42, 0.00, 0.58, 1.00) ×3", cubicBezierLabel(Ease(Interp.BEZIER, .42f, 0f, .58f, 1f, 3)))
        assertEquals("cubic-bezier(0.42, 0.00, 0.58, 1.00)", cubicBezierLabel(Ease(Interp.BEZIER, .42f, 0f, .58f, 1f)))
    }

    @Test fun anyTouchGrabsTheNearestHandleAndItGoesWhereTheFingerIs() {
        val shown = floatArrayOf(100f, 100f, 300f, 50f)
        assertEquals(0, grabHandle(0f, 400f, shown))       // longe das duas: ainda pega a mais perto
        assertEquals(1, grabHandle(900f, 0f, shown))
        // Gráfico 400×200 com margem 20; faixa 0..1; encaixe 10 px.
        val mid = handleAt(200f, 100f, 20f, 400f, 200f, 0f, 1f, 10f)
        assertEquals(0.5f, mid[0], 1e-6f)
        assertEquals(0.5f, mid[1], 1e-6f)
        // Perto de x = 0 e de y = 1: encaixa.
        val snapped = handleAt(24f, 5f, 20f, 400f, 200f, 0f, 1f, 10f)
        assertEquals(0f, snapped[0], 0f)
        assertEquals(1f, snapped[1], 0f)
        // Acima do gráfico passa de 1 (overshoot), mas nunca de 3; x preso em 0..1.
        val over = handleAt(900f, -10_000f, 20f, 400f, 200f, 0f, 1f, 10f)
        assertEquals(1f, over[0], 0f)
        assertEquals(EASE_Y_MAX, over[1], 0f)
        val under = handleAt(100f, 10_000f, 20f, 400f, 200f, 0f, 1f, 10f)
        assertEquals(EASE_Y_MIN, under[1], 0f)
    }

    @Test fun verticalRangeFitsCurveAndHandlesWithMargin() {
        val (lo, hi) = easeRange(Ease(Interp.BEZIER, .3f, 0f, .7f, 1f))
        assertEquals(-0.08f, lo, 1e-4f)
        assertEquals(1.08f, hi, 1e-4f)
        val (lo2, hi2) = easeRange(Ease(Interp.BEZIER, .3f, -1f, .7f, 2f))
        assertTrue(lo2 < -1f && hi2 > 2f)
    }

    @Test fun stageMoveSnapsOnlyTheAnchorToTheCompositionCentre() {
        assertEquals(960f, anchorSnap(955f, 960f, 8f), 0f)
        assertTrue(anchorSnap(940f, 960f, 8f).isNaN())
        assertTrue(anchorSnap(960f, 960f, 0f).isNaN())
    }
}
