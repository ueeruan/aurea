package com.aurea.aurea.editor.panels

import org.junit.Assert.*
import org.junit.Test
import kotlin.math.abs
import kotlin.math.sqrt

/** 0.3.0: the bugs in the value/speed graphs that testers reported. */
class SpeedGraph030Test {
    // Tangent math tests moved to test_animation.cpp with the shared implementation.

    @Test fun namedEasesIgnoreAStaleBezierPower() {
        // The engine applies the power only to Bézier/Custom (`keyframe_ease`).
        val (_, power) = speedEase(Ease(Interp.EASE_OUT, .1f, .2f, .3f, .4f, power = 3), Interp.EASE_OUT)
        assertEquals(1, power)
        val (_, bezier) = speedEase(Ease(Interp.BEZIER, .1f, .2f, .3f, .4f, power = 3), Interp.BEZIER)
        assertEquals(3, bezier)
    }

    @Test fun zoomedSpeedGraphEdgeShowsTheSpeedThereNotZero() {
        val keys = listOf(CurveKey(0, 0f, Ease(Interp.LINEAR, 0f, 0f, 1f, 1f)), CurveKey(30, 300f, Ease(Interp.LINEAR, 0f, 0f, 1f, 1f)))
        // A window entirely inside the segment: constant 300 px/s, no drop to 0.
        val drawn = graphCurve(keys, 10.0, 20.0, 4.0, speed = true, fps = 30f)
        assertTrue(drawn.isNotEmpty())
        for (s in drawn) assertEquals(300f, s.value, 1e-2f)
    }

    @Test fun spatialSpeedIsTheMagnitudeOfTheAxisVelocities() {
        val lin = Ease(Interp.LINEAR, 0f, 0f, 1f, 1f)
        val x = listOf(CurveKey(0, 0f, lin), CurveKey(30, 300f, lin))   // 300 px/s
        val y = listOf(CurveKey(0, 0f, lin), CurveKey(30, 400f, lin))   // 400 px/s
        val drawn = spatialSpeedCurve(listOf(x, y), 5.0, 25.0, 2.0, 30f)
        assertTrue(drawn.isNotEmpty())
        for (s in drawn) assertEquals(500f, s.value, 1e-2f)
        // Outside the animation the object stands still.
        val after = spatialSpeedCurve(listOf(x, y), 31.0, 40.0, 2.0, 30f)
        for (s in after) assertEquals(0f, s.value, 0f)
        assertEquals(500f, sqrt(300f * 300f + 400f * 400f), 0f)
    }

    @Test fun roundingMatchesTheEngineHalfAwayFromZero() {
        // x1 = 0.3125 → 2.5 cycles: the engine (std::round) gives 3; Kotlin's round gave 2.
        assertEquals(3, Ease(Interp.ELASTIC, .3125f, .25f, 1f, EASE_PARAM_MARKER).elasticCycles)
        assertEquals(3, Ease(Interp.BOUNCE, .3125f, .5f, 1f, -10f).bounceCount)
        assertTrue(abs(roundHalfUp(2.5f) - 3) == 0)
    }
}
