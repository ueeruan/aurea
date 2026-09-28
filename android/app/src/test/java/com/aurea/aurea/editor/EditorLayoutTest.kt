package com.aurea.aurea.editor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class EditorLayoutTest {
    @Test fun curvesStayCompactWithoutShrinkingOtherTools() {
        for (height in listOf(568f, 640f, 780f, 960f, 1200f)) {
            val curve = EditorLayout.solve(height, SheetContent.Curve, false)
            val panel = EditorLayout.solve(height, SheetContent.Panel, false)
            assertTrue(curve.sheet <= 280.01f)
            assertTrue(curve.sheet >= 230f)
            assertTrue(curve.timeline >= 110f)
            assertTrue(curve.preview >= panel.preview)
            assertTrue(curve.sheet <= panel.sheet)
            assertEquals(height, curve.topBar + curve.preview + curve.strip + curve.transport + curve.timeline + curve.sheet, 0.01f)
        }
    }
    @Test fun editingPanelsKeepUsableSpaceOnPhoneScreens() {
        for (height in listOf(640f, 720f, 780f, 840f, 960f)) {
            val overview = EditorLayout.solve(height, SheetContent.None, false)
            val editing = EditorLayout.solve(height, SheetContent.Panel, false)
            val dock = EditorLayout.solve(height, SheetContent.Dock, false)
            assertTrue("panel at $height", editing.sheet >= 336f)
            // Doca compacta: a altura do conteúdo, o resto fica para a timeline.
            assertEquals("dock at $height", EditorLayout.DOCK, dock.sheet, 0.01f)
            assertTrue(dock.timeline >= 110f)
            assertTrue(editing.preview >= 96f)
            assertTrue(editing.preview < overview.preview)
            assertTrue(editing.timeline >= 110f)
            assertEquals(height, editing.topBar + editing.preview + editing.strip +
                editing.transport + editing.timeline + editing.sheet, 0.01f)
            assertEquals(height * 0.54f - 32f, overview.preview, 0.01f)
        }
    }
    @Test fun batchSheetFitsTheStaggerRowAtFingerSize() {
        // Puxador 12 + 4 + tempo 52 + 8 + tela 48 + 8 + escalonar 48: nada espremido.
        val content = 12f + 4f + 52f + 8f + 48f + 8f + 48f
        for (height in listOf(568f, 640f, 720f, 780f, 840f, 960f)) {
            val batch = EditorLayout.solve(height, SheetContent.Batch, false)
            assertTrue("batch at $height: ${batch.sheet}", batch.sheet >= content)
            assertTrue(batch.timeline >= 110f)
            assertEquals(height, batch.topBar + batch.preview + batch.strip + batch.transport + batch.timeline + batch.sheet, 0.01f)
        }
    }
    @Test fun dockRowsPutTheLargerHalfBelow() {
        assertEquals(listOf(4), dockRows(4))
        assertEquals(listOf(2, 3), dockRows(5))
        assertEquals(listOf(3, 3), dockRows(6))
        assertEquals(listOf(3, 4), dockRows(7))
        assertEquals(listOf(4, 4), dockRows(8))
    }
    @Test fun addBarSitsUnderTheTimelineWithoutMovingThePreview() {
        for (height in listOf(640f, 720f, 780f, 840f, 960f)) {
            val overview = EditorLayout.solve(height, SheetContent.None, false)
            val bar = EditorLayout.solve(height, SheetContent.AddBar, false)
            assertEquals("bar at $height", EditorLayout.ADD_BAR, bar.sheet, 0.01f)
            assertEquals(overview.preview, bar.preview, 0.01f)
            assertTrue(bar.timeline >= 110f)
            assertEquals(height, bar.topBar + bar.preview + bar.strip + bar.transport + bar.timeline + bar.sheet, 0.01f)
        }
    }
}
