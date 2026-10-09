package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

/** Verifies removal of the marked rows and content-sized add panels in the native editor. */
class StaggerLayersTest {
    @get:Rule val compose = createComposeRule()

    @Test fun removedRowsStayHiddenAndAudioPanelFitsItsContent() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var initialized = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            initialized = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { initialized && store.engineReady }
        compose.runOnIdle { store.newProject(480, 320, 30f, "Stagger") }
        compose.waitUntil(15000) { store.project.title == "Stagger" }
        repeat(3) { n ->
            compose.runOnIdle { store.addShape(1) }
            compose.waitUntil(5000) { store.layers.size == n + 1 }
        }
        val ids = store.layers.map { it.id }
        compose.runOnIdle {
            store.setLayerRanges(ids.toLongArray(), IntArray(3) { 0 }, IntArray(3) { 60 })
            store.selectAll()
        }
        compose.waitUntil(5000) { store.selection.size == 3 && store.layers.all { it.startFrame == 0 } }

        compose.onNodeWithTag("stagger_step").assertDoesNotExist()
        compose.onNodeWithTag("stagger_layers").assertDoesNotExist()
        compose.onNodeWithTag("stagger_keys").assertDoesNotExist()
        val batch = compose.onNodeWithTag("timeline.batch.tools").fetchSemanticsNode().boundsInRoot
        val density = context.resources.displayMetrics.density
        assertTrue("Only the two icon rows should occupy the batch panel", batch.height / density <= 132.1f)
        compose.runOnIdle { store.clearSelection() }
        compose.onNodeWithTag("addBar.Audio").assertIsDisplayed().performClick()
        val audio = compose.onNodeWithTag("editor.addPanel").fetchSemanticsNode().boundsInRoot
        assertTrue("Audio must end below its single card row", audio.height / density <= 155f)
        assertTrue("All cards and the close button must fit", audio.height / density >= 140f)
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        instrumentation.uiAutomation.executeShellCommand("screencap -p /sdcard/Download/aurea-compact-audio.png").use {
            android.os.ParcelFileDescriptor.AutoCloseInputStream(it).readBytes()
        }
    }
}
