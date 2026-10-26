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

    // Omega chrome (área 02, P0-5): o transporte da referência tem 7 células de
    // peso igual — ↶ ↷ ⇤ ▶ ⇥ · Copiar e colar · painel; grade, resolução e
    // camadas saíram. 0.3.3 recursos: o marcador da 0.3.2 voltou (8 células).
    @Test fun sevenCellTransportOnTinyPhone() = checkTransport(320)
    @Test fun sevenCellTransportOnCompactPhone() = checkTransport(360)

    private fun checkTransport(width: Int) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { Box(Modifier.width(width.dp)) { EditorScreen(store) } }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(320, 180, 30f, "Seven cell transport") }
        compose.waitUntil(10000) { store.project.title == "Seven cell transport" }
        compose.runOnIdle { store.addShape(0); store.seek(12) }
        compose.waitUntil(10000) { store.layers.size == 1 && store.playhead == 12 }
        compose.runOnIdle { store.toggleMarker() }
        compose.waitUntil(5000) { 12 in store.markers.frames }
        // 0.3.3 recursos: o marcador da 0.3.2 voltou ao transporte, depois do ⇥.
        val tags = listOf(
            "transport.undo", "transport.redo", "transport.previous", "transport.play",
            "transport.next", "transport.marker", "transport.copyPaste", "transport.viewModes",
        )
        val bounds = tags.map { compose.onNodeWithTag(it).assertIsDisplayed().fetchSemanticsNode().boundsInRoot }
        for (i in 1 until bounds.size) assertTrue(tags[i], bounds[i - 1].right <= bounds[i].left + 1)
        for (gone in listOf("transport.grid", "transport.resolution", "transport.layers")) {
            compose.onNodeWithTag(gone).assertDoesNotExist()
        }
        // ⇤ / ⇥: o começo, os marcadores e o último quadro (sem toque longo).
        compose.runOnIdle { store.clearSelection(); store.seek(0) }
        compose.waitUntil(5000) { store.playhead == 0 }
        compose.onNodeWithTag("transport.next").performClick()
        compose.waitUntil(5000) { store.playhead == 12 }
        compose.onNodeWithTag("transport.previous").performClick()
        compose.waitUntil(5000) { store.playhead == 0 }
        // O último botão abre e fecha a coluna de modos à direita da prévia.
        compose.onNodeWithTag("transport.viewModes").performClick()
        compose.onNodeWithTag("viewmode.column").assertIsDisplayed()
        compose.onNodeWithTag("viewmode.zoomIn").performClick()
        compose.waitUntil(3000) { StageView.zoom == 1.5f }
        compose.onNodeWithTag("viewmode.zoomLabel").performClick()
        compose.waitUntil(3000) { StageView.zoom == 1f }
        compose.onNodeWithTag("transport.viewModes").performClick()
        compose.waitUntil(3000) { compose.onAllNodesWithTag("viewmode.column").fetchSemanticsNodes().isEmpty() }
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
            val back = compose.onAllNodesWithContentDescription(context.getString(R.string.pn_back_to_layer_tools))
            if (back.fetchSemanticsNodes().isNotEmpty()) back.onFirst().performClick()
            compose.onNodeWithContentDescription(context.getString(R.string.editor_mais_acoes_camada)).performClick()
        }
        openClipboard()
        compose.onNodeWithText(context.getString(R.string.editor_copiar_camada)).performScrollTo().performClick()
        compose.waitUntil(5000) { store.clipboard and 1 != 0 }
        openClipboard()
        compose.onNodeWithText(context.getString(R.string.editor_colar_camada_cabecote)).performScrollTo().performClick()
        compose.waitUntil(10000) { store.layers.size == 2 }
        assertTrue(store.layers.any { it.id == original })
        assertEquals(2, store.layers.map { it.id }.toSet().size)
        compose.onNodeWithContentDescription(context.getString(R.string.editor_desfazer)).performClick()
        compose.waitUntil(10000) { store.layers.size == 1 }
        assertEquals(original, store.layers.single().id)
    }
}
