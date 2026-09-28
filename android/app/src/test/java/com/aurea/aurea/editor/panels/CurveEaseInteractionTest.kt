package com.aurea.aurea.editor.panels

import org.junit.Assert.*
import org.junit.Test

class CurveEaseInteractionTest {
    @Test fun unsupportedInversionIsDistinctFromSymmetricCurves() {
        for (kind in listOf(Interp.HOLD, Interp.BOUNCE, Interp.ELASTIC, Interp.STEPS)) {
            assertFalse(Ease(kind, 0f, 0f, 1f, 1f).supportsInversion)
        }
        for (kind in listOf(Interp.LINEAR, Interp.EASE_IN_OUT)) {
            val ease = Ease(kind, 0f, 0f, 1f, 1f)
            assertTrue(ease.supportsInversion)
            assertNull(ease.inverted())
        }
        assertEquals(Interp.EASE_OUT, Ease(Interp.EASE_IN, 0f, 0f, 1f, 1f).inverted()!!.interp)
    }
    @Test fun nonlinearFamilyThumbnailsMatchSharedEvaluatorSemantics() {
        val bounce = Ease(Interp.BOUNCE,0f,0f,1f,1f)
        assertEquals(.25f,bounce.transform(.25f),1e-6f)
        assertEquals(1f,bounce.transform(.5f),1e-6f)
        assertEquals(.75f,bounce.transform(.625f),1e-6f)
        assertEquals(.9375f,bounce.transform(.825f),1e-6f)
        val elastic = Ease(Interp.ELASTIC,0f,0f,1f,1f)
        assertEquals(0f,elastic.transform(0f),0f)
        assertEquals(1f,elastic.transform(1f),0f)
        assertTrue(elastic.transform(1f/6f) > 1.3f)
        val steps = Ease(Interp.STEPS,0f,0f,1f,1f)
        assertEquals(0f,steps.transform(.249f),0f)
        assertEquals(.25f,steps.transform(.25f),0f)
        assertEquals(.75f,steps.transform(.999f),0f)
        assertEquals(1f,steps.transform(1f),0f)
        for (ease in listOf(bounce,elastic,steps)) assertFalse(ease.hasHandles)
    }
    @Test fun smoothDefaultExposesHandlesWithoutChangingItsSavedInterpolation() {
        val ease = Ease(Interp.EASE_IN_OUT, .2f, .1f, .8f, .9f)
        assertTrue(ease.hasHandles)
        assertFalse(ease.isBezier)
        assertArrayEquals(floatArrayOf(.5f, 0f, .5f, 1f), ease.handles(), 0f)
        assertEquals(Interp.EASE_IN_OUT, ease.interp)
        assertEquals(.125f, ease.transform(.25f), 0f)
        assertEquals(.5f, ease.transform(.5f), 0f)
        assertEquals(.875f, ease.transform(.75f), 0f)
        // Merely exposing the handles cannot substitute the different cubic.
        val h = ease.handles()
        val edited = Ease(Interp.BEZIER, h[0], h[1], h[2], h[3])
        assertTrue(kotlin.math.abs(edited.transform(.25f)-ease.transform(.25f)) > .01f)
    }

    @Test fun holdHasNoContinuousHandlesButEveryContinuousBuiltInDoes() {
        assertFalse(Ease(Interp.HOLD, 0f, 0f, 1f, 1f).hasHandles)
        for (kind in listOf(Interp.LINEAR, Interp.EASE_IN, Interp.EASE_OUT, Interp.EASE_IN_OUT, Interp.BEZIER, Interp.CUSTOM)) {
            assertTrue(Ease(kind, .2f, 0f, .8f, 1f).hasHandles)
        }
    }
    @Test fun linearShowsGrabbableHandlesAwayFromItsKeys() {
        val h = Ease(Interp.LINEAR, 0f, 0f, 1f, 1f).handles()
        assertArrayEquals(floatArrayOf(1f / 3f, 1f / 3f, 2f / 3f, 2f / 3f), h, 1e-6f)
        // A bézier equivalente é a mesma reta: puxar a alça não muda nada antes do arrasto.
        val asBezier = Ease(Interp.BEZIER, h[0], h[1], h[2], h[3])
        for (t in listOf(.1f, .25f, .5f, .8f)) assertEquals(t, asBezier.transform(t), 1e-4f)
    }

    @Test fun coincidentHandlesAreDrawnApartAndEachStaysGrabbable() {
        // As duas alças no mesmo ponto (o canto de cima à esquerda).
        val shown = separatedHandles(40f, 20f, 40f, 20f, 28f, 300f, 372f, 20f, 60f)
        val gap = kotlin.math.hypot(shown[2] - shown[0], shown[3] - shown[1])
        assertEquals(60f, gap, 0.01f)
        // A de saída vai para o lado da primeira marca, a de chegada para o da segunda.
        assertTrue(shown[0] < shown[2])
        assertEquals(0, nearestHandle(shown[0], shown[1], shown, 56f))
        assertEquals(1, nearestHandle(shown[2], shown[3], shown, 56f))
        // Longe das duas, nenhuma é agarrada.
        assertEquals(-1, nearestHandle(200f, 200f, shown, 56f))
        // Afastadas o bastante, ficam onde estão.
        val apart = separatedHandles(10f, 10f, 200f, 10f, 0f, 0f, 1f, 1f, 60f)
        assertArrayEquals(floatArrayOf(10f, 10f, 200f, 10f), apart, 0f)
        // Perto (mas não iguais): abrem ao longo da própria separação.
        val near = separatedHandles(100f, 100f, 110f, 100f, 0f, 0f, 1f, 1f, 60f)
        assertArrayEquals(floatArrayOf(75f, 100f, 135f, 100f), near, 1e-4f)
    }
}
