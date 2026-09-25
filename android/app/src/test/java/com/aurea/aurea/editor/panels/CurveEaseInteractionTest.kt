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
}
