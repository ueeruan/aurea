package com.aurea.aurea.editor.panels

import org.junit.Assert.*
import org.junit.Test

class CurveEaseInteractionTest {
    @Test fun configurableBounceMatchesLandingsAndMirrorsWithoutChangingStrength() {
        val bounce = Ease(Interp.BOUNCE,.375f,.5f,1f,-10f)
        assertEquals(1f,bounce.transform(1f/2.75f),.00001f)
        assertEquals(.75f,bounce.transform(1.5f/2.75f),.00001f)
        assertTrue(bounce.supportsInversion)
        assertFalse(bounce.same(bounce.bounce(count=4)))
        for (count in 1..8) for (strength in listOf(.1f,.5f,.9f)) {
            val curve=bounce.bounce(count,strength)
            val reverse=curve.inverted()!!
            for (i in 0..1000) {
                val t=i/1000f
                assertEquals(curve.transform(t),1f-reverse.transform(1f-t),.00001f)
                assertTrue(curve.transform(t) in 0f..1f)
            }
        }
    }
    @Test fun unsupportedInversionIsDistinctFromSymmetricCurves() {
        for (kind in listOf(Interp.HOLD, Interp.ELASTIC, Interp.STEPS)) {
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

    @Test fun handlesStayOnTheirValuesAndCoincidentOnesStayGrabbable() {
        // Nada afasta as alças: elas ficam onde os valores dizem e o toque
        // compara com essas posições (antes, a 30 dp uma "empurrava" a outra).
        val keys = floatArrayOf(28f, 300f, 372f, 20f)
        val apart = floatArrayOf(100f, 100f, 112f, 100f)
        assertEquals(0, grabHandle(101f, 100f, apart, keys))
        assertEquals(1, grabHandle(111f, 100f, apart, keys))
        // As duas no mesmo ponto: o lado do toque (primeira marca → segunda) decide.
        val same = floatArrayOf(40f, 20f, 40f, 20f)
        assertEquals(0, grabHandle(40f, 20f, same, keys))      // em cima: a de saída
        assertEquals(0, grabHandle(30f, 30f, same, keys))      // para o lado da primeira marca
        assertEquals(1, grabHandle(52f, 14f, same, keys))      // para o lado da segunda marca
        // Arrastar uma não mexe na outra: o valor da outra fica idêntico.
        val e = Ease(Interp.BEZIER, .40f, .10f, .41f, .10f)
        val p = handleAt(200f, 60f, 20f, 400f, 200f, 0f, 1f, 0f)
        val moved = e.copy(x1 = p[0], y1 = p[1])
        assertEquals(e.x2, moved.x2, 0f)
        assertEquals(e.y2, moved.y2, 0f)
    }
}
