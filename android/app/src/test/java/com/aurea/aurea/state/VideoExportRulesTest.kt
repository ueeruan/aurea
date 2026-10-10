package com.aurea.aurea.state

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * O espelho em Kotlin de `export_frame_size` (ExportRules.hpp). Os MESMOS casos
 * do teste do motor ExportRules.FrameSizeAlignsLongSideTo16AndKeepsShortSideEven —
 * se um lado mudar, os dois testes mudam juntos.
 */
class VideoExportRulesTest {
    @Test fun sameSizesAsTheEngine() {
        val cases = listOf(
            intArrayOf(1920, 1080, 480, 854, 480),   // Vivo Y30: era 854×480
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
}
