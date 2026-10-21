package com.aurea.aurea.editor.panels

import com.aurea.aurea.engine.KeyframeRow
import org.junit.Assert.*
import org.junit.Test
import kotlin.math.abs
import kotlin.math.sqrt

/** 0.3.0: the bugs in the value/speed graphs that testers reported. */
class SpeedGraph030Test {
    private val a = KeyframeRow(property = 0, effectIndex = -1, time = 0, value = 0f, interpolation = Interp.BEZIER, paramIndex = 0)
    private val b = KeyframeRow(property = 0, effectIndex = -1, time = 30, value = 300f, interpolation = Interp.LINEAR, paramIndex = 0)

    @Test fun draggingOneHandleKeepsTheOtherPairAsSaved() {
        // Ease Out (0;0)(0,58;1): the outgoing x is 0. Dragging the INCOMING
        // handle used to rewrite it as 0.01 (both were clamped on read).
        val saved = floatArrayOf(0f, 0f, .58f, 1f)
        val incoming = SpeedHandle(a, b, incoming = true, ease = saved, base = 300f)
        val h = incoming.changed(20f, 40f)
        assertEquals(0f, h[0], 0f)
        assertEquals(0f, h[1], 0f)
        assertNotEquals(.58f, h[2])
    }

    @Test fun speedHandleKeepsTheCurveYRange() {
        val out = SpeedHandle(a, b, incoming = false, ease = floatArrayOf(.3f, 0f, .7f, 1f), base = 1f)
        val h = out.changed(15f, 1e6f)
        assertEquals(EASE_Y_MAX, h[1], 0f)
        val inc = SpeedHandle(a, b, incoming = true, ease = floatArrayOf(.3f, 0f, .7f, 1f), base = 1f)
        assertEquals(EASE_Y_MIN, inc.changed(15f, 1e6f)[3], 0f)
    }

    @Test fun evenPowerStopsAtZeroInsteadOfBouncingBack() {
        assertEquals(0f, rooted(-4f, 2), 0f)
        assertEquals(2f, rooted(4f, 2), 1e-6f)
        assertEquals(-2f, rooted(-8f, 3), 1e-5f)
    }

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
