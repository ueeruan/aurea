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

/** Exercise actual scroll gestures and panel routing, including large system fonts. */
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
                    Box(Modifier.width(320.dp).height(EditorLayout.dock(1, fontScale).dp)) {
                        store.primary?.let { LayerToolsDock(store, ui, it) }
                    }
                }
            }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(720, 720, 30f, "Compact tools") }
        compose.waitUntil(10000) { store.project.title == "Compact tools" }
        compose.runOnIdle { store.addShape(0) }
        compose.waitUntil(10000) { store.layers.size == 1 && store.primary != null }
        compose.onNodeWithTag("dock.tool.EditShape").assertIsDisplayed()
        // Colour is beyond the initially visible tools on a narrow phone.
        compose.onNodeWithTag("dock.tools").performTouchInput { swipeLeft() }
        compose.onNodeWithTag("dock.tool.ColorFill").assertIsDisplayed().performClick()
        compose.runOnIdle {
            assertEquals(EditorPanel.Shape, ui.panel)
            fontScale = 2f
            store.addText(openEditor = false)
        }
        compose.waitUntil(10000) { store.layers.size == 2 }
        // A new layer must start at its own first tool, even after the previous swipe.
        compose.onNodeWithTag("dock.tool.EditText").assertIsDisplayed()
        compose.onNodeWithTag("dock.tool.Presets").performScrollTo().assertIsDisplayed().performClick()
        compose.runOnIdle { assertEquals(EditorPanel.Presets, ui.panel) }
        val last = compose.onNodeWithTag("dock.tool.Presets").getUnclippedBoundsInRoot()
        compose.onNodeWithTag("dock.tool.Move").performScrollTo().assertIsDisplayed().performClick()
        val first = compose.onNodeWithTag("dock.tool.Move").getUnclippedBoundsInRoot()
        assertEquals("All tools stay on one row", first.top.value, last.top.value, 1f)
        assertTrue("Large-font touch height", (first.bottom - first.top).value >= 48f)
        compose.runOnIdle { assertEquals(EditorPanel.Transform, ui.panel) }
    }
}
