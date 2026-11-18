package com.aurea.aurea.engine

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** 1.0.2 Pre Render & Pre Export: os contratos LongArray da ponte (aurea_jni.cpp). */
class PreRenderPanelBridgeTest {
    @Test fun progressCarriesRangeQualityAndStaleSegments() {
        val p = PreRenderProgress.from(longArrayOf(0, 1, 33, 33, 32, 32, 1, 0, 1000, 0, 8, 41, 2, 4))
        assertTrue(p.complete)
        assertEquals(8L, p.firstFrame)
        assertEquals(41L, p.endFrame)
        assertEquals(2, p.divisor)
        assertEquals(4, p.dirtySegments)
        // O contrato antigo de 10 posições continua valendo (gesto 540p).
        val old = PreRenderProgress.from(longArrayOf(0, 1, 10, 10, 540, 960, 1, 0, 0, 0))
        assertEquals(0, old.divisor)
        assertEquals(0, old.dirtySegments)
    }

    @Test fun rangeFitAndPreExportParse() {
        val r = PreRenderRange.from(longArrayOf(8, 41, 8, 41, 1, 4, 29970))
        assertTrue(r.chosen)
        assertTrue(r.loop)
        assertEquals(4, r.divisor)
        assertEquals(29.97, r.fps, 1e-9)
        val none = PreRenderRange.from(longArrayOf(-1, -1, 0, 48, 0, 3, 0))
        assertFalse(none.chosen)
        assertEquals(2, none.divisor)      // divisor inválido vira 1/2
        assertEquals(30.0, none.fps, 0.0)  // fps 0 vira 30
        val f = PreRenderFit.from(longArrayOf(960, 540, 2, 0, 30000, 55250, 10000, 77760864, 4294967296, 777608640, 1))
        assertEquals(960, f.width)
        assertEquals(55.25, f.fitsSeconds, 1e-9)
        assertEquals(10.0, f.rangeSeconds, 1e-9)
        assertTrue(f.fits)
        val e = PreExportStatus.from(longArrayOf(0, 1, 0, 33, 33, 8, 41, 960, 540, 2, 0, 30000, 123456, 0), "/cache/pre-export/a.mp4")
        assertTrue(e.playable)
        val stale = e.copy(stale = true)
        assertFalse(stale.playable)
        assertEquals(PreExportStatus(), PreExportStatus.from(longArrayOf(), ""))
    }
}
