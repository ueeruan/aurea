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
            assertTrue(curve.timeline >= 90f)
            assertTrue(curve.preview >= panel.preview)
            assertTrue(curve.sheet < panel.sheet)
            assertEquals(height, curve.topBar + curve.preview + curve.strip + curve.transport + curve.timeline + curve.sheet, 0.01f)
        }
    }
    @Test fun editingPanelsKeepUsableSpaceOnPhoneScreens() {
        for (height in listOf(640f, 720f, 780f, 840f, 960f)) {
            val overview = EditorLayout.solve(height, SheetContent.None, false)
            val editing = EditorLayout.solve(height, SheetContent.Panel, false)
            val dock = EditorLayout.solve(height, SheetContent.Dock, false)
            assertTrue("panel at $height", editing.sheet >= 336f)
            assertTrue("dock at $height", dock.sheet >= 240f)
            assertTrue(editing.preview >= 96f)
            assertTrue(editing.preview < overview.preview)
            assertTrue(editing.timeline >= 90f)
            assertEquals(height, editing.topBar + editing.preview + editing.strip +
                editing.transport + editing.timeline + editing.sheet, 0.01f)
            assertEquals(height * 0.54f, overview.preview, 0.01f)
        }
    }
}
