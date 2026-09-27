package com.aurea.aurea.editor.panels

import org.junit.Assert.assertEquals
import org.junit.Test

class GraphKeyTimeTest {
    @Test fun horizontalDragSnapsToFramesWithoutOverwritingNeighbours() {
        assertEquals(34, graphDragFrame(30, 4.3f, 0, 60))
        assertEquals(59, graphDragFrame(30, 200f, 0, 60))
        assertEquals(1, graphDragFrame(30, -200f, 0, 60))
        assertEquals(30, graphDragFrame(30, 200f, 29, 31))
    }
    @Test fun negativeKeysAndExtremeDragsRemainSafe() {
        assertEquals(-7, graphDragFrame(-10, 3f, null, 0))
        assertEquals(Int.MAX_VALUE, graphDragFrame(30, Float.MAX_VALUE, null, null))
        assertEquals(30, graphDragFrame(30, Float.NaN, null, null))
    }
}
