package com.aurea.aurea.editor.panels

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Contas puras da face Lente: FOV da focal (sensor 24 mm de altura), formatos e presets. */
class CameraLensTest {
    @Test fun fovFollowsFullFrameSensorHeight() {
        // 2·atan(24 / (2·50)) = 26,99°; 24 mm → 53,13°; 12 mm → 90°.
        assertEquals(26.99f, CameraLens.fovFromFocal(50f), 0.01f)
        assertEquals(53.13f, CameraLens.fovFromFocal(24f), 0.01f)
        assertEquals(90f, CameraLens.fovFromFocal(12f), 0.01f)
        assertEquals(0f, CameraLens.fovFromFocal(0f), 0f)
        assertEquals(0f, CameraLens.fovFromFocal(Float.NaN), 0f)
    }

    @Test fun readoutsUsePtBrDecimalsAndLensStyleAperture() {
        assertEquals("46,8", CameraLens.formatFov(46.79f))
        assertEquals("50 mm", CameraLens.formatFocal(49.6f))
        assertEquals("f/2.8", CameraLens.formatAperture(2.8f))
    }

    // Omega aparência (P1-6): ângulo de visão e zoom (aba Visão / Escala da câmera).
    @Test fun viewAngleAndZoomRoundTripThroughTheFocal() {
        assertEquals(12f, CameraLens.focalFromFov(90f), 0.01f)
        assertEquals(50f, CameraLens.fovFromFocal(CameraLens.focalFromFov(50f)), 0.01f)
        assertEquals(0f, CameraLens.focalFromFov(0f), 0f)
        assertEquals(0f, CameraLens.focalFromFov(180f), 0f)
        assertEquals(1f, CameraLens.zoomFromFocal(CameraLens.focalFromFov(CameraLens.DEFAULT_FOV)), 1e-4f)
        assertEquals(2f, CameraLens.zoomFromFocal(CameraLens.focalFromZoom(2f)), 1e-4f)
        // A faixa do motor (4–1200 mm) prende o zoom e dá a faixa do ângulo.
        assertEquals(CameraLens.KEYPAD_MAX_MM, CameraLens.focalFromZoom(1000f), 0f)
        assertEquals(CameraLens.KEYPAD_MIN_MM, CameraLens.focalFromZoom(0.001f), 0f)
        assertTrue(CameraLens.MIN_FOV < 1.2f && CameraLens.MAX_FOV > 143f && CameraLens.MAX_FOV < 144f)
    }

    @Test fun presetsAreAscendingAndMatchWithHalfMillimeterSlack() {
        val p = CameraLens.PRESETS_MM
        assertTrue(p.toList() == p.sorted())
        assertEquals(listOf(14, 18, 24, 35, 50, 85, 135, 200), p.toList())
        assertEquals(4, CameraLens.presetIndex(50.3f))
        assertEquals(-1, CameraLens.presetIndex(51f))
    }
}
