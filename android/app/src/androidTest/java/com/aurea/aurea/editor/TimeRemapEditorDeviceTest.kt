package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.*
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

class TimeRemapEditorDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun legacyCurveOpensInEffectsAndUsesTheNormalKeyframeControl() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        lateinit var store: EditorStore
        var ready = false
        var panel by mutableStateOf(EditorPanel.Speed)
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            val env = PanelEnv(store, {}, { panel = it }, {}, {}, {}, { null })
            AureaTheme {
                Box(Modifier.fillMaxSize()) {
                    if (panel == EditorPanel.Speed) SpeedPanel(env) else EffectsPanel(env)
                }
            }
        }
        compose.waitUntil(30_000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(640, 360, 30f, "Time Remap editor") }
        compose.waitUntil(15_000) { store.project.title == "Time Remap editor" && !store.projectOperationBusy }
        val engine = store.engineForStress
        val layer = engine.addText("Time Remap")
        assertTrue(layer != 0L && engine.setTimeRemap(layer, true))
        assertTrue(engine.setTimeRemapValue(layer, 20, 18f))
        compose.runOnIdle { store.select(layer, openOptions = false); store.seek(12) }
        compose.waitUntil(5000) { store.detail?.timeRemap == true && store.detail?.localPlayhead == 12 }
        compose.onNodeWithTag("effects.remap.ruler").assertDoesNotExist()
        val type = effectTypeId("aurea.time.remap")
        assertTrue(store.catalog.any { it.typeId == type && it.category == "Tempo" })
        compose.onNodeWithTag("speed.open-remap").performScrollTo().performClick()
        compose.waitUntil(8000) { store.effects.any { it.typeId == type } }
        val ruler = compose.onNodeWithTag("effects.remap.ruler", useUnmergedTree = true)
        compose.waitUntil(8000) { compose.onAllNodesWithTag("effects.remap.ruler", useUnmergedTree = true).fetchSemanticsNodes().isNotEmpty() }
        ruler.assertExists()
        fun remapKeys() = store.primaryKeys().filter { it.property == TrackProperty.TIME_REMAP }
        assertTrue(remapKeys().any { it.time == 20 && it.value == 18f })
        compose.onNodeWithTag("panel.rail.keyframe").performClick()
        compose.waitUntil(5000) { remapKeys().any { it.time == 12 } }
        val value = remapKeys().first { it.time == 12 }.value
        ruler.performTouchInput { swipeLeft(durationMillis = 450) }
        compose.waitUntil(5000) { remapKeys().any { it.time == 12 && it.value != value } }
        compose.onNodeWithTag("effects.remap.reverse").performClick()
        compose.waitUntil(5000) { remapKeys().none { it.time == 12 } }
    }
}
