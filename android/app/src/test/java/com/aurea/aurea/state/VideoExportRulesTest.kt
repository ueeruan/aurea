package com.aurea.aurea.state

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Kotlin mirror of export_frame_size_v2. Picture dimensions must remain
 * independent of an encoder's internal storage alignment.
 */
class VideoExportRulesTest {
    @Test fun sameSizesAsTheEngine() {
        val cases = listOf(
            intArrayOf(1920, 1080, 480, 854, 480),
            intArrayOf(1920, 1080, 720, 1280, 720),
            intArrayOf(1920, 1080, 1080, 1920, 1080),
            intArrayOf(1920, 1080, 1440, 2560, 1440),
            intArrayOf(1920, 1080, 2160, 3840, 2160),
            intArrayOf(1080, 1920, 480, 480, 854),
            intArrayOf(1080, 1920, 1080, 1080, 1920),
            intArrayOf(1080, 1080, 1080, 1080, 1080),
            intArrayOf(1080, 1080, 480, 480, 480),
            intArrayOf(1080, 1350, 1080, 1080, 1350),
            intArrayOf(1280, 720, 0, 1280, 720),
            intArrayOf(1000, 700, 0, 1000, 700),
            intArrayOf(64, 36, 36, 64, 36),
            intArrayOf(1080, 1920, 720, 720, 1280),
        )
        for (c in cases) {
            assertEquals("${c[0]}x${c[1]} @${c[2]}", c[3] to c[4], VideoExportRules.frameSize(c[0], c[1], c[2]))
        }
        assertEquals(0 to 0, VideoExportRules.frameSize(0, 1080, 480))
        assertEquals(4 to 2, VideoExportRules.frameSize(4, 2, 2))
    }

    /** 0.3.6: lado ímpar não recusa mais ("erro de codec"); sai alinhado como o motor. */
    @Test fun oddCompositionsExportAligned() {
        assertEquals(1920 to 1080, VideoExportRules.frameSize(1919, 1079, 0))
        assertEquals(1080 to 1920, VideoExportRules.frameSize(1079, 1920, 0))
        assertEquals(848 to 480, VideoExportRules.frameSize(853, 480, 0))
        assertEquals(1080 to 1344, VideoExportRules.alignedFrameSize(1080, 1350, 0))
        assertEquals(1280 to 720, VideoExportRules.alignedFrameSize(1920, 1080, 720))
    }
}
