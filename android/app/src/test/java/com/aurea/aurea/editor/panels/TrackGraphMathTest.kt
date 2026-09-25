package com.aurea.aurea.editor.panels

import org.junit.Assert.*
import org.junit.Test

class TrackGraphMathTest {
    @Test fun velocityUsesActualIntegerFramesWithoutRepeatedSampleSpikes() {
        val values = FloatArray(160) { ((30.0 * it / 159).toInt() * 2).toFloat() }
        val valueCurve = graphSamples(values, 0, 30, 30f, false)
        assertEquals(31, valueCurve.size)
        val velocity = graphSamples(values, 0, 30, 30f, true)
        assertEquals(30, velocity.size)
        velocity.forEach { assertEquals(60f, it.value, 0.0001f) }
    }

    @Test fun holdAndReversedTracksHaveCorrectSignedUnitsPerSecond() {
        val reverse = graphSamples(floatArrayOf(10f, 5f, 0f), 0, 30, 60f, true)
        reverse.forEach { assertEquals(-20f, it.value, 0.0001f) }
        val hold = graphSamples(FloatArray(160) { 5f }, -3, 3, 24f, true)
        assertEquals(6, hold.size)
        hold.forEach { assertEquals(0f, it.value, 0f) }
    }

    @Test fun zoomPreservesAnchorAndPanUsesVisibleRanges() {
        val before = GraphViewport(10f, 110f, -20f, 80f)
        val zoomed = before.transform(2f, 0f, 0f, 0.25f, 0.75f)
        assertEquals(before.from + before.duration * 0.25f, zoomed.from + zoomed.duration * 0.25f, 0.0001f)
        assertEquals(before.low + before.range * 0.75f, zoomed.low + zoomed.range * 0.75f, 0.0001f)
        val panned = before.transform(1f, 0.2f, -0.1f)
        assertEquals(-10f, panned.from, 0.0001f)
        assertEquals(-30f, panned.low, 0.0001f)
        assertEquals(before, before.transform(Float.NaN, 0f, 0f))
    }
}
