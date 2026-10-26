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
        // Omega chrome (área 02, P0-4): o lote é a barra inferior flutuante de 42 dp.
        val batch = compose.onNodeWithTag("batch.bottom").fetchSemanticsNode().boundsInRoot
        val density = context.resources.displayMetrics.density
        assertTrue("The floating selection bar is 42 dp tall", batch.height / density <= 42.1f)
        compose.runOnIdle { store.clearSelection() }
        compose.onNodeWithTag("addBar.Audio").assertIsDisplayed().performClick()
        val audio = compose.onNodeWithTag("add.panel").fetchSemanticsNode().boundsInRoot
        // Omega adicionar (área 08, P0-1/P0-4): o conteúdo embutido de 193 dp, logo
        // acima da barra de adicionar (que fica): a faixa de áudio em 2 fileiras.
        assertTrue("The add content is the reference's 193 dp", kotlin.math.abs(audio.height / density - 193f) <= 1f)
        val bar = compose.onNodeWithTag("editor.addBar").fetchSemanticsNode().boundsInRoot
        // (o nó da barra começa depois do respiro de 6 dp no alto dela)
        assertTrue("The add content sits right above the add bar", kotlin.math.abs(audio.bottom - bar.top) <= 8 * density)
        compose.onNodeWithTag("add.voice").assertExists()
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        instrumentation.uiAutomation.executeShellCommand("screencap -p /sdcard/Download/aurea-compact-audio.png").use {
            android.os.ParcelFileDescriptor.AutoCloseInputStream(it).readBytes()
        }
    }
}
