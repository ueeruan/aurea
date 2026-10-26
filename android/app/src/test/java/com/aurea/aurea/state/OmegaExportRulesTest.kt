package com.aurea.aurea.state

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Omega 0.3.2, área 08: os espelhos em Kotlin das regras do motor que a página
 * de exportação mostra (valem sem a biblioteca nativa; o motor tem o mesmo
 * teste em engine/tests/test_omega_add.cpp).
 */
class OmegaExportRulesTest {
    @Test fun fpsOptionsWorkInHundredthsAndTruncate() {
        // > 15 fps: 1/4 e 1/2 (30 → 7,5 e 15; 25 → 6,25 e 12,5).
        assertEquals(750, exportFpsOption(3000, 0))
        assertEquals(1500, exportFpsOption(3000, 1))
        assertEquals(3000, exportFpsOption(3000, 2))
        assertEquals(625, exportFpsOption(2500, 0))
        assertEquals(1250, exportFpsOption(2500, 1))
        assertEquals(749, exportFpsOption(2997, 0))
        assertEquals(1498, exportFpsOption(2997, 1))
        // ≤ 15 fps: 1/3 e 2/3 (12 → 4 e 8), sem mínimo.
        assertEquals(400, exportFpsOption(1200, 0))
        assertEquals(800, exportFpsOption(1200, 1))
        assertEquals(500, exportFpsOption(1500, 0))
        assertEquals(33, exportFpsOption(100, 0))
    }

    @Test fun qualityPercentMatchesTheOldChipsAndInterpolates() {
        assertEquals(0.6, exportQualityFactor(QUALITY_MIN), 1e-9)
        assertEquals(1.0, exportQualityFactor(QUALITY_DEFAULT), 1e-9)
        assertEquals(1.5, exportQualityFactor(QUALITY_MAX), 1e-9)
        assertEquals(0.8, exportQualityFactor(40), 1e-9)
        assertEquals(1.25, exportQualityFactor(80), 1e-9)
        // Os códigos antigos (0/1/2) continuam valendo.
        assertEquals(0.6, exportQualityFactor(0), 1e-9)
        assertEquals(1.0, exportQualityFactor(1), 1e-9)
        assertEquals(1.5, exportQualityFactor(2), 1e-9)
        assertTrue(exportQualityFactor(150) <= 1.5)
        assertEquals(QUALITY_DEFAULT, ExportOptions().quality)
    }
}
