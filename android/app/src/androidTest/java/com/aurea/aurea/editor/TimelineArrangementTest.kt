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

    @Test fun compactSelectionKeepsAlignmentAndTrimWithOneUndo() {
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
        compose.onNodeWithTag("timeline.batch.tools").assertIsDisplayed()
        for (mode in 3..6) compose.onNodeWithTag("timeline.arrange.$mode").assertDoesNotExist()
        compose.onNodeWithTag("stagger_row").assertDoesNotExist()
        val align = com.aurea.aurea.ui.i18n.AppText.get(context, com.aurea.aurea.R.string.editor_alinhar_inicios)
        compose.onNodeWithContentDescription(align).performClick()
        compose.waitUntil(5000) { store.layers.all { it.startFrame == 10 } }
        compose.runOnIdle { assertEquals(listOf(10, 25, 7), store.layers.map { it.endFrame - it.startFrame }); store.seek(15) }
        val trim = com.aurea.aurea.ui.i18n.AppText.get(context, com.aurea.aurea.R.string.editor_aparar_inicio_cabecote)
        compose.onNodeWithContentDescription(trim).performClick()
        compose.waitUntil(5000) { store.layers.all { it.startFrame == 15 } }
        compose.runOnIdle { assertEquals(listOf(20, 35, 17), store.layers.map { it.endFrame }); store.undo() }
        compose.waitUntil(5000) { store.layers.all { it.startFrame == 10 } }
    }
}
