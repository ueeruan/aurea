package com.aurea.aurea.engine

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PreRenderProgressTest {
    @Test fun integerBridgeKeepsCountersLargerThan32Bits() {
        val snapshot = PreRenderProgress.from(longArrayOf(1, 0, 30, 120, 540, 960, -1, 0, 5_000_000_000, 64_000_000))
        assertTrue(snapshot.running)
        assertFalse(snapshot.complete)
        assertEquals(30L, snapshot.framesDone)
        assertEquals(120L, snapshot.framesTotal)
        assertEquals(540, snapshot.width)
        assertEquals(960, snapshot.height)
        assertEquals(-1, snapshot.direction)
        assertEquals(5_000_000_000L, snapshot.diskBytes)
        assertEquals(64_000_000L, snapshot.residentBytes)
        assertEquals(0.25f, snapshot.fraction, 0f)
    }

    @Test fun unavailableOrTerminalSnapshotsDoNotProduceInvalidProgress() {
        assertEquals(PreRenderProgress(), PreRenderProgress.from(longArrayOf()))
        assertEquals(0f, PreRenderProgress.from(longArrayOf(0, 0, 0, 0, 0, 0, 1, 25, 0, 0)).fraction, 0f)
        val completed = PreRenderProgress.from(longArrayOf(0, 1, 100, 100, 540, 960, 1, 0, 0, 0))
        assertFalse(completed.running)
        assertTrue(completed.complete)
        assertEquals(1f, completed.fraction, 0f)
    }
}
