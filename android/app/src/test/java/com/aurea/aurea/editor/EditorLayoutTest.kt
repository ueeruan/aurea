package com.aurea.aurea.editor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class EditorLayoutTest {
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
