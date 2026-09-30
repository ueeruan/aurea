package com.aurea.aurea.editor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class StageZoomMathTest {
    // Palco 1080×1500 px, composição 1080×1920 (9:16): encaixe pela altura.
    private val boxW = 1080f
    private val boxH = 1500f
    private val compW = 1080f
    private val compH = 1920f
    private val base = minOf(boxW / compW, boxH / compH)

    @Test fun zoomIsClampedBetweenFitAndEightTimes() {
        assertEquals(1f, StageZoomMath.clampZoom(0.2f), 0f)
        assertEquals(8f, StageZoomMath.clampZoom(40f), 0f)
        assertEquals(3.5f, StageZoomMath.clampZoom(3.5f), 0f)
        assertEquals(1f, StageZoomMath.clampZoom(Float.NaN), 0f)
        assertEquals(1f, StageZoomMath.clampZoom(Float.POSITIVE_INFINITY), 0f)
    }

    @Test fun atFitThereIsNoPan() {
        assertEquals(0f, StageZoomMath.maxPan(1f, 800f), 0f)
        assertEquals(0f, StageZoomMath.clampPan(300f, 1f, 800f), 0f)
        assertEquals(0f, StageZoomMath.clampPan(Float.NaN, 4f, 800f), 0f)
        assertEquals(0f, StageZoomMath.clampPan(50f, Float.NaN, 800f), 0f)
        assertEquals(0f, StageZoomMath.clampPan(50f, 4f, Float.NaN), 0f)
        assertEquals(0f, StageZoomMath.clampPan(50f, 4f, -800f), 0f)
    }

    @Test fun panIsLimitedSoTheEdgeStopsWhereItSitsAtFit() {
        // 3×: a composição mede 3·800; a borda pode ir até onde fica em 1×.
        assertEquals(800f, StageZoomMath.maxPan(3f, 800f), 1e-3f)
        assertEquals(800f, StageZoomMath.clampPan(5000f, 3f, 800f), 1e-3f)
        assertEquals(-800f, StageZoomMath.clampPan(-5000f, 3f, 800f), 1e-3f)
        assertEquals(120f, StageZoomMath.clampPan(120f, 3f, 800f), 0f)
    }

    @Test fun zoomAroundFocusKeepsTheCompositionPointUnderTheFinger() {
        val focusX = 700f
        val focusY = 400f
        val z0 = 1.5f
        val px0 = 40f
        val py0 = -25f
        val cx = StageZoomMath.toComp(focusX, boxW, 0f, compW, base, z0, px0)
        val cy = StageZoomMath.toComp(focusY, boxH, 0f, compH, base, z0, py0)
        val z1 = 4f
        val px1 = StageZoomMath.zoomAround(px0, z0, z1, focusX, boxW / 2)
        val py1 = StageZoomMath.zoomAround(py0, z0, z1, focusY, boxH / 2)
        assertEquals(focusX, StageZoomMath.toScreen(cx, boxW, 0f, compW, base, z1, px1), 1e-2f)
        assertEquals(focusY, StageZoomMath.toScreen(cy, boxH, 0f, compH, base, z1, py1), 1e-2f)
    }

    @Test fun pinchPanFollowsTheMidpointOfTheFingers() {
        // Mesmo zoom, meio dos dedos anda 90 px: o pan anda 90 px.
        assertEquals(90f, StageZoomMath.pinchPan(0f, 2f, 2f, 500f, 590f, boxW / 2), 1e-3f)
        // Zoom e pan juntos: o ponto sob o meio inicial vai para o meio final.
        val c = StageZoomMath.toComp(300f, boxW, 0f, compW, base, 2f, 0f)
        val p = StageZoomMath.pinchPan(0f, 2f, 3f, 300f, 360f, boxW / 2)
        assertEquals(360f, StageZoomMath.toScreen(c, boxW, 0f, compW, base, 3f, p), 1e-2f)
    }

    @Test fun screenAndProjectMappingAreExactInverses() {
        for (z in listOf(1f, 2.25f, 8f)) {
            for (pan in listOf(-300f, 0f, 175f)) {
                for (c in listOf(0f, 13.5f, 540f, 1079f)) {
                    val s = StageZoomMath.toScreen(c, boxW, 8f, compW, base, z, pan)
                    assertEquals(c, StageZoomMath.toComp(s, boxW, 8f, compW, base, z, pan), 1e-2f)
                }
            }
        }
        // 1× sem pan = o encaixe centrado de sempre.
        assertEquals((boxW - compW * base) / 2, StageZoomMath.origin(boxW, 0f, compW, base, 1f, 0f), 1e-3f)
    }

    @Test fun projectDeltaOfADragShrinksWithZoom() {
        // Arrastar 100 px de tela a 4× move 1/4 do que move em 1× (precisão).
        val at1 = StageZoomMath.toComp(600f, boxW, 0f, compW, base, 1f, 0f) - StageZoomMath.toComp(500f, boxW, 0f, compW, base, 1f, 0f)
        val at4 = StageZoomMath.toComp(600f, boxW, 0f, compW, base, 4f, 90f) - StageZoomMath.toComp(500f, boxW, 0f, compW, base, 4f, 90f)
        assertEquals(at1 / 4f, at4, 1e-3f)
    }

    @Test fun percentAndZoomedFlag() {
        assertEquals(250, StageZoomMath.percent(2.5f))
        assertFalse(StageZoomMath.isZoomed(1f))
        assertTrue(StageZoomMath.isZoomed(1.1f))
    }
}
