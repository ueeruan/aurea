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

class TimelineArrangementTest {
    @get:Rule val compose = createComposeRule()

    @Test fun distributeUnequalClipsAndUndoThroughTheActualToolbar() {
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
        compose.runOnIdle { store.newProject(480, 320, 30f, "Timeline arrangement") }
        compose.waitUntil(15000) { store.project.title == "Timeline arrangement" }
        repeat(3) { n ->
            compose.runOnIdle { store.addShape(1) }
            compose.waitUntil(5000) { store.layers.size == n + 1 }
        }
        val ids = store.layers.map { it.id }
        compose.runOnIdle {
            store.setLayerRanges(ids.toLongArray(), intArrayOf(10, 12, 71), intArrayOf(20, 37, 78))
            store.selectAll()
        }
        compose.waitUntil(5000) { store.selection.size == 3 && store.layers.map { it.startFrame } == listOf(10, 12, 71) }
        compose.onNodeWithTag("timeline.arrange.3").performScrollTo().performClick()
        compose.waitUntil(5000) { store.layers.map { it.startFrame } == listOf(10, 41, 71) }
        compose.runOnIdle {
            assertEquals(ids, store.layers.map { it.id })
            assertEquals(listOf(10, 25, 7), store.layers.map { it.endFrame - it.startFrame })
            store.undo()
        }
        compose.waitUntil(5000) { store.layers.map { it.startFrame } == listOf(10, 12, 71) }
        compose.onNodeWithTag("timeline.arrange.4").performScrollTo().performClick()
        compose.waitUntil(5000) { store.layers.map { it.startFrame } == listOf(10, 33, 71) }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.layers.map { it.startFrame } == listOf(10, 12, 71) }
        compose.runOnIdle { store.seek(100) }
        compose.waitUntil(5000) { store.playhead == 100 }
        compose.onNodeWithTag("timeline.arrange.6").performScrollTo().performClick()
        compose.waitUntil(5000) { store.layers.all { it.endFrame == 100 } }
        compose.runOnIdle {
            assertEquals(listOf(90, 75, 93), store.layers.map { it.startFrame })
            store.undo()
        }
        compose.waitUntil(5000) { store.layers.map { it.startFrame } == listOf(10, 12, 71) }
        compose.onNodeWithTag("timeline.arrange.5").performScrollTo().performClick()
        compose.waitUntil(5000) { store.layers.all { it.startFrame == 100 } }
        compose.runOnIdle {
            assertEquals(listOf(10, 25, 7), store.layers.map { it.endFrame - it.startFrame })
            store.undo()
        }
        compose.waitUntil(5000) { store.layers.map { it.startFrame } == listOf(10, 12, 71) }
    }
}
