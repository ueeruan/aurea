package com.aurea.aurea.editor.timeline

import org.junit.Assert.*
import org.junit.Test

class TimelineReorderTest {
    private val tops = floatArrayOf(0f, 46f, 72f, 98f, 144f, 190f)
    private val ids = longArrayOf(1, 1, 1, 2, 3)

    @Test fun expandedPropertyRowsTravelTogetherAndLeaveAnEqualGap() {
        val p = Reorder.preview(tops, ids, 0, 4, 120f)
        assertArrayEquals(floatArrayOf(120f, 120f, 120f, -98f, -98f), p.offsets, 0f)
        assertEquals(92f, p.gapTop, 0f)
        assertEquals(0, p.sourceStart)
        assertEquals(3, p.sourceEnd)
        assertArrayEquals(floatArrayOf(0f, 46f, 72f, 98f, 144f, 190f), tops, 0f)
    }

    @Test fun upwardDropOnPropertyTrackUsesItsWholeLayer() {
        val p = Reorder.preview(tops, ids, 4, 1, 15f)
        assertArrayEquals(floatArrayOf(46f, 46f, 46f, 46f, -129f), p.offsets, 0f)
        assertEquals(0f, p.gapTop, 0f)
    }

    @Test fun remainingInsideSourceGroupDoesNotShiftOtherLayers() {
        val p = Reorder.preview(tops, ids, 0, 2, 12f)
        assertArrayEquals(floatArrayOf(12f, 12f, 12f, 0f, 0f), p.offsets, 0f)
        assertEquals(0f, p.gapTop, 0f)
    }

    @Test fun staleTargetDoesNotMoveAnyRows() {
        val p = Reorder.preview(tops, ids, 0, 8, 12f)
        assertArrayEquals(FloatArray(ids.size), p.offsets, 0f)
        assertTrue(p.gapTop.isNaN())
    }
}

