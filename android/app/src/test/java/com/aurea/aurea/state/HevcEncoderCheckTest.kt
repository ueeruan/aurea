package com.aurea.aurea.state

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** BETA 2: HEVC só com ENCODER HEVC de hardware que aceita o tamanho/fps; senão H.264 direto. */
class HevcEncoderCheckTest {
    private val hw = HevcEncoderCheck.Encoder("c2.qti.hevc.encoder", hardware = true, supportsSize = true)
    private val hwSmall = HevcEncoderCheck.Encoder("c2.mtk.hevc.encoder", hardware = true, supportsSize = false)
    private val sw = HevcEncoderCheck.Encoder("c2.android.hevc.encoder", hardware = false, supportsSize = true)

    @Test fun hardwareEncoderThatAcceptsTheSizeKeepsHevc() {
        val c = HevcEncoderCheck.choose(34, listOf(sw, hw), 1920, 1080, 30.0)
        assertTrue(c.hevc)
        assertEquals("c2.qti.hevc.encoder", c.encoder)
    }

    @Test fun decoderOnlyOrSoftwareOnlyOrTooSmallFallsBackToH264WithReason() {
        val none = HevcEncoderCheck.choose(34, emptyList(), 1920, 1080, 30.0)
        assertFalse(none.hevc); assertNull(none.encoder); assertTrue(none.reason.contains("sem encoder"))
        val software = HevcEncoderCheck.choose(34, listOf(sw), 1920, 1080, 30.0)
        assertFalse(software.hevc); assertTrue(software.reason.contains("software"))
        val small = HevcEncoderCheck.choose(34, listOf(hwSmall, sw), 3840, 2160, 60.0)
        assertFalse(small.hevc); assertTrue(small.reason.contains("3840x2160@60"))
        // API < 29: o motor só usa encoder de software; HEVC lá é lento demais.
        assertFalse(HevcEncoderCheck.choose(28, listOf(hw), 1280, 720, 30.0).hevc)
    }

    @Test fun exportSizeFollowsTheEngineRule() {
        assertEquals(1920 to 1080, HevcEncoderCheck.exportSize(1920, 1080, 1080))
        assertEquals(1920 to 1080, HevcEncoderCheck.exportSize(1920, 1080, 0))
        assertEquals(1280 to 720, HevcEncoderCheck.exportSize(1920, 1080, 720))
        assertEquals(2160 to 3840, HevcEncoderCheck.exportSize(1080, 1920, 2160))
        assertEquals(854 to 480, HevcEncoderCheck.exportSize(1920, 1080, 480))
        assertEquals(0 to 0, HevcEncoderCheck.exportSize(0, 1080, 720))
    }
}
