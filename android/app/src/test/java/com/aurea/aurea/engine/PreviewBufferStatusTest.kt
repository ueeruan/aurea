package com.aurea.aurea.engine

import com.aurea.aurea.state.PreviewState
import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.*
import org.junit.Test

class PreviewBufferStatusTest {
    @Test fun readsSignedBufferFlagsWithoutChangingAdjacentAbiFields() {
        val bytes = ByteBuffer.allocate(PodLayout.STATUS_BYTES).order(ByteOrder.nativeOrder())
        bytes.putInt(104, 57)
        bytes.putInt(108, Int.MIN_VALUE or (1 shl 30) or (120 shl 8) or 39)
        bytes.putFloat(112, 29.97f)
        val engine = EngineStatus().also { it.readFrom(bytes) }
        val preview = PreviewState(bufferStatus = engine.previewBufferStatus)
        assertEquals(57, engine.modelRevision)
        assertEquals(29.97f, engine.compFps, .001f)
        assertTrue(preview.buffering)
        assertTrue(preview.bufferLimited)
        assertEquals(39, preview.bufferedFrames)
        assertEquals(120, preview.bufferTarget)
    }

    @Test fun clearedCacheReturnsTheUiToIdle() {
        val preview = PreviewState(bufferStatus = 0)
        assertFalse(preview.buffering)
        assertFalse(preview.bufferLimited)
        assertEquals(0, preview.bufferedFrames)
        assertEquals(0, preview.bufferTarget)
    }
}
