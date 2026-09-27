package com.aurea.aurea.editor

import org.junit.Assert.*
import org.junit.Test
import kotlin.math.hypot

class GizmoGeometryTest {
    @Test fun viewportZoomDoesNotChangeGizmoScreenExtent() {
        for (zoom in listOf(0.05f, 0.5f, 1f, 12f)) {
            val raw = floatArrayOf(200f, 300f, 200f + 320f * zoom, 300f,
                200f, 300f + 160f * zoom, 200f, 300f)
            val p = GizmoGeometry.tips(raw, 2f)
            assertEquals(160f, hypot(p[2] - p[0], p[3] - p[1]), 0.001f)
            assertEquals(80f, hypot(p[4] - p[0], p[5] - p[1]), 0.001f)
            assertEquals(200f, p[0], 0f)
            assertEquals(300f, p[1], 0f)
            assertTrue(hypot(p[6] - p[0], p[7] - p[1]) > 48f)
            assertEquals(200f + 320f * zoom, raw[2], 0f)
        }
    }

    @Test fun invalidProjectionDoesNotProduceNaNHandles() {
        assertTrue(GizmoGeometry.tips(floatArrayOf(0f), 1f).isEmpty())
        assertTrue(GizmoGeometry.tips(FloatArray(8) { Float.NaN }, 1f).isEmpty())
    }
}
