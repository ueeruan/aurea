package com.aurea.aurea.editor.panels

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class CurveMathTest {
    @Test fun flatTimeTangentsMatchAnalyticCurveNearEndpoints() {
        for (end in listOf(false, true)) {
            for (x in listOf(0.000001f, 0.00001f, 0.001f, 0.999f, 0.99999f, 0.999999f)) {
                val t = if (end) 1.0 - Math.cbrt(1.0 - x.toDouble()) else Math.cbrt(x.toDouble())
                val expected = 3.0 * t - 6.0 * t * t + 4.0 * t * t * t
                val handle = if (end) 1f else 0f
                assertEquals(expected, cubicBezier(handle, 1f, handle, 0f, x).toDouble(), 0.000002)
            }
        }
    }

    @Test fun drawingUsesExactEndpointsAndPreservesOvershoot() {
        assertEquals(0f, cubicBezier(0f, 2f, 0f, 2f, 0f), 0f)
        assertEquals(1f, cubicBezier(1f, -1f, 1f, -1f, 1f), 0f)
        assertTrue(cubicBezier(0.2f, 2f, 0.8f, 2f, 0.5f) > 1f)
        assertTrue(cubicBezier(0.2f, -1f, 0.8f, -1f, 0.5f) < 0f)
        for (i in 0..1000) {
            val x = i / 1000f
            assertEquals(x, cubicBezier(0.3f, 0.3f, 0.7f, 0.7f, x), 0.000002f)
        }
    }
}
