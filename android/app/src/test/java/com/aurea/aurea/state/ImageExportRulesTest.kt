package com.aurea.aurea.state

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * O espelho em Kotlin das regras do export como imagem (ImageEncode.cpp:
 * plan_image_export / estimate_image_export_bytes). Os MESMOS números do teste
 * do motor ImageEncode.PlanAndEstimateFollowOneRule — se um lado mudar, os dois
 * testes precisam mudar juntos.
 */
class ImageExportRulesTest {
    @Test fun pngIsTheFullCompositionFrame() {
        val p = ImageExportRules.plan(ExportFormat.Frame, 1080, 1920, 30.0, 300, true, 0, 480, 0.0)
        assertEquals(1080, p.width)
        assertEquals(1920, p.height)
        assertEquals(1, p.frames)
        assertTrue(p.alpha)
        assertTrue(p.bytes > ImageExportRules.plan(ExportFormat.Frame, 1080, 1920, 30.0, 300, false, 0, 480, 0.0).bytes)
    }

    @Test fun gifDefaultsTo480WideAt15Fps() {
        val p = ImageExportRules.plan(ExportFormat.Gif, 1920, 1080, 30.0, 300, false, 0, 0, 0.0)
        assertEquals(480, p.width)
        assertEquals(270, p.height)
        assertEquals(150, p.frames)
        assertEquals(15.0, p.fps, 1e-9)
        assertTrue(p.bytes in 5_000_001L..19_999_999L)
    }

    @Test fun gifNeverUpscalesNorExceedsCompositionFps() {
        val p = ImageExportRules.plan(ExportFormat.Gif, 640, 360, 24.0, 48, false, 0, 720, 30.0)
        assertEquals(640, p.width)
        assertEquals(24.0, p.fps, 1e-9)
        assertEquals(48, p.frames)
    }

    @Test fun sequenceUsesShortSideAndCompositionFps() {
        val p = ImageExportRules.plan(ExportFormat.Sequence, 1920, 1080, 25.0, 50, false, 720, 480, 0.0)
        assertEquals(1280, p.width)
        assertEquals(720, p.height)
        assertEquals(50, p.frames)
        assertEquals(25.0, p.fps, 1e-9)
    }

    @Test fun limitsAndDestinationsMatchTheEngine() {
        assertFalse(ImageExportRules.tooLong(ExportFormat.Gif, 1800))
        assertTrue(ImageExportRules.tooLong(ExportFormat.Gif, 1801))
        assertTrue(ImageExportRules.tooLong(ExportFormat.Sequence, 18001))
        assertFalse(ImageExportRules.tooLong(ExportFormat.Frame, 1_000_000))
        assertEquals("Pictures/Aurea", ImageExportRules.galleryFolder(ExportFormat.Gif))
        assertEquals("Pictures/Aurea", ImageExportRules.galleryFolder(ExportFormat.Frame))
        assertEquals("Download/Aurea", ImageExportRules.galleryFolder(ExportFormat.Sequence))
        assertEquals("image/png", ExportFormat.Frame.mime)
        assertEquals("application/zip", ExportFormat.Sequence.mime)
        assertEquals("gif", ExportFormat.Gif.extension)
        // O código atravessa a ponte como ImageExportFormat (0 PNG, 1 sequência, 2 GIF).
        assertEquals(listOf(-1, 0, 1, 2), ExportFormat.entries.map { it.engineCode })
    }
}
