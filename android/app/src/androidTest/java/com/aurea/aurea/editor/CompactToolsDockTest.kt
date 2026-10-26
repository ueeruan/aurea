package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

/** Omega painel (área 04): o painel da camada de 200 dp, o roteamento dos botões e a fonte grande. */
class CompactToolsDockTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore

    @Test fun allToolsRemainReachableAndChangingLayerResetsTheRow() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        val ui = EditorUi()
        var fontScale by mutableFloatStateOf(1.3f)
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            val density = LocalDensity.current.density
            CompositionLocalProvider(LocalDensity provides Density(density, fontScale)) {
                AureaTheme {
                    Box(Modifier.width(320.dp).height(EditorLayout.layerPanel(fontScale).dp)) {
                        store.primary?.let { LayerPanel(store, ui, it) }
                    }
                }
            }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(720, 720, 30f, "Compact tools") }
        compose.waitUntil(10000) { store.project.title == "Compact tools" }
        compose.runOnIdle { store.addShape(0) }
        compose.waitUntil(10000) { store.layers.size == 1 && store.primary != null }
        compose.onNodeWithTag("layer.panel").assertIsDisplayed()
        compose.onNodeWithTag("layer.panel.edit").assertIsDisplayed()
        // A grade fixa: as 3 vagas da fileira 2 e as 4 da fileira 3 da forma.
        compose.onNodeWithTag("layer.panel.borderShadow").assertIsDisplayed()
        compose.onNodeWithTag("layer.panel.blendOpacity").assertIsDisplayed()
        compose.onNodeWithTag("layer.panel.colorFill").assertIsDisplayed().performClick()
        compose.runOnIdle {
            assertEquals(EditorPanel.Shape, ui.panel)
            fontScale = 2f
            store.addText(openEditor = false)
        }
        compose.waitUntil(10000) { store.layers.size == 2 }
        // O texto tem a vaga "Editar texto" e os presets (de texto) na fileira 3.
        compose.onNodeWithTag("layer.panel.edit").assertIsDisplayed()
        compose.onNodeWithTag("layer.panel.presets").assertIsDisplayed().performClick()
        compose.runOnIdle { assertEquals(EditorPanel.Presets, ui.panel) }
        val last = compose.onNodeWithTag("layer.panel.presets").getUnclippedBoundsInRoot()
        compose.onNodeWithTag("layer.panel.moveTransform").assertIsDisplayed().performClick()
        val first = compose.onNodeWithTag("layer.panel.moveTransform").getUnclippedBoundsInRoot()
        assertEquals("Row 3 keeps one line", first.top.value, last.top.value, 1f)
        assertTrue("Large-font touch height", (first.bottom - first.top).value >= 48f)
        compose.runOnIdle { assertEquals(EditorPanel.Transform, ui.panel) }
    }

    @Test fun deleteIsVisibleOnSmallPhonesAndUndoRestoresTheLayer() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        val ui = EditorUi()
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            val density = LocalDensity.current.density
            CompositionLocalProvider(LocalDensity provides Density(density, 2f)) {
            AureaTheme {
                Box(Modifier.width(320.dp).height(48.dp)) {
                    store.primary?.let { LayerTopBar(store, ui, it) }
                }
            }
            }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(720, 720, 30f, "Delete layer controls") }
        compose.waitUntil(10000) { store.project.title == "Delete layer controls" }
        compose.runOnIdle { store.addShape(0) }
        compose.waitUntil(10000) { store.layers.size == 1 && store.primary != null }
        var layerId = 0L
        compose.runOnIdle { layerId = store.primary!! }
        compose.onNodeWithTag("layer.delete").assertIsDisplayed().assertIsEnabled().performClick()
        compose.waitUntil(10000) { store.layers.isEmpty() }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(10000) { store.layers.size == 1 && store.layers.single().id == layerId }
    }
}
