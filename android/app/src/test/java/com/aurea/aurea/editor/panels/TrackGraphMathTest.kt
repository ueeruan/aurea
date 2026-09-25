package com.aurea.aurea.editor.panels

import org.junit.Assert.*
import org.junit.Test

class TrackGraphMathTest {
    @Test fun hitTargetsRemain48DpAcrossScreenDensities() {
        for (density in listOf(1f, 2f, 3.5f)) {
            val point = GraphHitPoint(7, 80f*density, 120f*density)
            assertEquals(7, graphHitIndex(listOf(point), point.x+23f*density, point.y, 24f*density))
            assertEquals(7, graphHitIndex(listOf(point), point.x, point.y-24f*density, 24f*density))
            assertEquals(-1, graphHitIndex(listOf(point), point.x+25f*density, point.y, 24f*density))
        }
    }

    @Test fun nearbyKeysChooseNearestAndCoincidentKeysKeepSelection() {
        val points = listOf(GraphHitPoint(2, 40f, 60f), GraphHitPoint(9, 40f, 60f), GraphHitPoint(3, 64f, 60f))
        assertEquals(9, graphHitIndex(points, 40f, 60f, 24f, selected=9))
        assertEquals(3, graphHitIndex(points, 63f, 60f, 24f, selected=9))
        assertEquals(-1, graphHitIndex(points, Float.NaN, 60f, 24f))
        assertEquals(-1, graphHitIndex(emptyList(), 40f, 60f, 24f))
    }

    @Test fun hitMappingFollowsPannedAndZoomedTimeViewport() {
        val original = GraphViewport(0f, 100f, 0f, 200f)
        val moved = original.transform(2f, .1f, -.1f)
        val x = (50f-moved.from)/moved.duration*300f
        val y = 400f-(100f-moved.low)/moved.range*400f
        assertEquals(4, graphHitIndex(listOf(GraphHitPoint(4,x,y)), x+20f, y, 24f))
        assertEquals(-1, graphHitIndex(listOf(GraphHitPoint(4,x,y)), 150f, 200f, 24f))
    }
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
