package com.aurea.aurea.editor

import org.junit.Assert.*
import org.junit.Test
import kotlin.math.cos
import kotlin.math.sin

class StagePinchTrackerTest {
    private val limit: (Float) -> Float = { it.coerceIn(.5f, 2f) }

    @Test fun stationaryFingersDoNotStartAnEdit() {
        val tracker = StagePinchTracker()
        tracker.start(0f, 0f, 100f, 0f, 16f)
        repeat(20) { assertFalse(tracker.update(0f, 0f, 100f, 0f, limit)) }
        assertEquals(1f, tracker.factor, 0f)
        assertEquals(0f, tracker.degrees, 0f)
    }

    @Test fun reversingAtEitherScaleLimitRespondsOnTheNextEvent() {
        val tracker = StagePinchTracker()
        tracker.start(0f, 0f, 100f, 0f, 16f)
        tracker.update(0f, 0f, 400f, 0f, limit)
        assertEquals(2f, tracker.factor, 0f)
        tracker.update(0f, 0f, 360f, 0f, limit)
        assertEquals(1.8f, tracker.factor, .00001f)
        tracker.update(0f, 0f, 40f, 0f, limit)
        assertEquals(.5f, tracker.factor, 0f)
        tracker.update(0f, 0f, 44f, 0f, limit)
        assertEquals(.55f, tracker.factor, .00001f)
    }

    @Test fun crossingFingersDoesNotFlipOrExplodeTheLayer() {
        val tracker = StagePinchTracker()
        tracker.start(-50f, 0f, 50f, 0f, 16f)
        tracker.update(-20f, 0f, 20f, 0f, limit)
        val before = tracker.factor
        assertFalse(tracker.update(-2f, 0f, 2f, 0f, limit))
        assertFalse(tracker.update(0f, 0f, 0f, 0f, limit))
        assertFalse(tracker.update(20f, 0f, -20f, 0f, limit))
        assertEquals(before, tracker.factor, 0f)
        assertEquals(0f, tracker.degrees, 0f)
        tracker.update(22f, 0f, -22f, 0f, limit)
        assertEquals(before * 1.1f, tracker.factor, .00001f)
        assertEquals(0f, tracker.degrees, 0f)
    }

    @Test fun invalidCoordinatesDoNotPoisonTheNextEvent() {
        val tracker = StagePinchTracker()
        tracker.start(0f, 0f, 100f, 0f, 16f)
        assertFalse(tracker.update(Float.NaN, 0f, 100f, 0f, limit))
        assertFalse(tracker.update(0f, 0f, Float.POSITIVE_INFINITY, 0f, limit))
        tracker.update(0f, 0f, 150f, 0f, limit)
        assertEquals(1.5f, tracker.factor, .00001f)
    }

    @Test fun rotationWrapsAcross180WithoutAFullTurnJump() {
        fun x(degrees: Double) = (100 * cos(Math.toRadians(degrees))).toFloat()
        fun y(degrees: Double) = (100 * sin(Math.toRadians(degrees))).toFloat()
        val tracker = StagePinchTracker()
        tracker.start(0f, 0f, x(179.0), y(179.0), 16f)
        tracker.update(0f, 0f, x(-179.0), y(-179.0), limit)
        assertEquals(2f, tracker.degrees, .0001f)
        assertEquals(1f, tracker.factor, .00001f)
    }
}
