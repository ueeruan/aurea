package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.width
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

class CompactTransportClipboardTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore

    @Test fun markerStaysNextToPlayOnTinyPhone() = checkPermanentMarker(320)
    @Test fun markerStaysNextToPlayOnCompactPhone() = checkPermanentMarker(360)

    private fun checkPermanentMarker(width: Int) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { Box(Modifier.width(width.dp)) { EditorScreen(store) } }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(320, 180, 30f, "Permanent beat button") }
        compose.waitUntil(10000) { store.project.title == "Permanent beat button" }
        compose.runOnIdle { store.addShape(0); store.seek(12) }
        compose.waitUntil(10000) { store.layers.size == 1 && store.playhead == 12 }
        val marker = compose.onNodeWithTag("transport.marker").assertIsDisplayed().assertWidthIsAtLeast(48.dp).assertHeightIsAtLeast(48.dp)
        val play = compose.onNodeWithTag("transport.play").assertIsDisplayed()
        assertTrue(marker.fetchSemanticsNode().boundsInRoot.right <= play.fetchSemanticsNode().boundsInRoot.left + 1)
        marker.performClick()
        compose.waitUntil(5000) { 12 in store.markers.frames }
        play.performClick()
        compose.waitUntil(15000) { store.playing && store.playhead > 15 }
        marker.assertIsDisplayed().performClick()
        compose.waitUntil(5000) { store.markers.frames.size == 2 }
        compose.runOnIdle { assertTrue("Marking a beat must not pause playback", store.playing) }
        play.performClick()
        compose.waitUntil(5000) { !store.playing }
        compose.runOnIdle { store.seek(0) }
        compose.onNodeWithTag("transport.next").performClick()
        compose.waitUntil(5000) { store.playhead == 12 }
        compose.onNodeWithTag("transport.grid").assertIsDisplayed()
        compose.onNodeWithTag("transport.fastPreview").assertIsDisplayed()
        compose.onNodeWithTag("transport.layers").assertIsDisplayed()
    }

    @Test fun compactMenuCopiesPastesAndOneUndoRestoresLayers() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { Box(Modifier.width(360.dp)) { EditorScreen(store) } }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(720, 720, 30f, "Compact clipboard") }
        compose.waitUntil(10000) { store.project.title == "Compact clipboard" }
        compose.runOnIdle { store.addShape(0) }
        compose.waitUntil(10000) { store.layers.size == 1 && store.primary != null }
        val original = store.layers.single().id
        compose.onNodeWithTag("transport.duplicate").assertDoesNotExist()
        fun openClipboard() {
            // Copiar e colar mora no menu do projeto (engrenagem da barra de cima).
            compose.onNodeWithContentDescription(context.getString(R.string.editor_ajustes_projeto_mais)).performClick()
            compose.onNodeWithText(context.getString(R.string.editor_copiar_colar)).performScrollTo().performClick()
        }
        openClipboard()
        compose.onNodeWithText(context.getString(R.string.editor_copiar_camada)).performClick()
        compose.waitUntil(5000) { store.clipboard and 1 != 0 }
        openClipboard()
        compose.onNodeWithText(context.getString(R.string.editor_colar_camada_cabecote)).performClick()
        compose.waitUntil(10000) { store.layers.size == 2 }
        assertTrue(store.layers.any { it.id == original })
        assertEquals(2, store.layers.map { it.id }.toSet().size)
        compose.onNodeWithContentDescription(context.getString(R.string.editor_desfazer)).performClick()
        compose.waitUntil(10000) { store.layers.size == 1 }
        assertEquals(original, store.layers.single().id)
    }
}
