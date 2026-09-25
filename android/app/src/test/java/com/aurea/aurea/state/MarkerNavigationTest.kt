package com.aurea.aurea.state

import org.junit.Assert.*
import org.junit.Test

class MarkerNavigationTest {
    @Test fun exactStrictNeighborIgnoresOrderAndDuplicates() {
        val markers = intArrayOf(90, 10, 40, 40)
        assertEquals(40, markerNavigationTarget(markers, 10, 1))
        assertEquals(10, markerNavigationTarget(markers, 40, -1))
        assertEquals(90, markerNavigationTarget(markers, 40, 1))
        assertEquals(40, markerNavigationTarget(markers, 41, -1))
        assertEquals(40, markerNavigationTarget(markers, 39, 1))
    }
    @Test fun boundariesStayPutWithoutKeyframeOrSingleFrameFallback() {
        val markers = intArrayOf(90, 10, 40)
        assertEquals(90, markerNavigationTarget(markers, 90, 1))
        assertEquals(10, markerNavigationTarget(markers, 10, -1))
        assertEquals(100, markerNavigationTarget(markers, 100, 1))
        assertEquals(0, markerNavigationTarget(markers, 0, -1))
        assertEquals(40, markerNavigationTarget(intArrayOf(40), 40, 1))
        assertEquals(40, markerNavigationTarget(intArrayOf(40), 40, -1))
        assertEquals(40, markerNavigationTarget(markers, 40, 0))
    }
    @Test fun absentMarkersPreserveExistingNavigation() {
        assertNull(markerNavigationTarget(intArrayOf(), 40, 1))
        assertNull(markerNavigationTarget(intArrayOf(), 40, -1))
    }
}
