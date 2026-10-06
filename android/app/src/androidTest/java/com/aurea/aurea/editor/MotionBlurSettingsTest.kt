package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.editor.panels.PanelContent
import com.aurea.aurea.engine.MotionBlurSettings
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import java.util.concurrent.atomic.AtomicBoolean
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

class MotionBlurSettingsTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore

    private fun openProject() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            val ui = remember { EditorUi() }
            AureaTheme {
                Column(Modifier.fillMaxSize()) {
                    PreviewStage(store, ui, Modifier.fillMaxWidth().weight(1f))
                    PanelContent(store, EditorPanel.Transform, {}, {}, {}, Modifier.height(500.dp))
                }
            }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(320, 240, 30f, "Motion blur settings") }
        compose.waitUntil(15000) { store.project.title == "Motion blur settings" && !store.projectOperationBusy }
    }

    private fun settings(): MotionBlurSettings = checkNotNull(store.engineForStress.queryMotionBlurSettings())

    @Test fun oneNativeSettingsEditUndoesTogetherAndPersistsAcrossReopen() {
        openProject()
        val initial = settings()
        val target = initial.copy(enabled = true, angle = 270f, phase = -120.5f, samples = 8, adaptiveLimit = 64)
        compose.runOnIdle { assertTrue(store.engineForStress.setMotionBlurSettings(target)) }
        compose.waitUntil(5000) { store.shutterAngle == 270f && store.shutterPhase == -120.5f }
        assertEquals(target, settings())
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { settings() == initial }
        compose.runOnIdle { store.redo() }
        compose.waitUntil(5000) { settings() == target }
        val path = checkNotNull(store.project.path)
        val saved = AtomicBoolean(false)
        compose.runOnIdle { store.saveProject { saved.set(true) } }
        compose.waitUntil(15000) { saved.get() }
        compose.runOnIdle { store.newProject(320, 240, 30f, "Blur reopen checkpoint") }
        compose.waitUntil(15000) { store.project.title == "Blur reopen checkpoint" && !store.projectOperationBusy }
        compose.runOnIdle { store.openProject(path) }
        compose.waitUntil(15000) { store.project.title == "Motion blur settings" && !store.projectOperationBusy }
        assertEquals(target, settings())
    }

    @Test fun invalidSamplesAndNonfiniteExposureNeverMutateNativeSettings() {
        openProject()
        val initial = settings()
        for (invalid in listOf(initial.copy(angle = Float.NaN), initial.copy(phase = Float.POSITIVE_INFINITY),
            initial.copy(samples = 1), initial.copy(samples = 65), initial.copy(adaptiveLimit = 257),
            initial.copy(samples = 32, adaptiveLimit = 16))) {
            compose.runOnIdle { assertFalse(store.engineForStress.setMotionBlurSettings(invalid)) }
            assertEquals(initial, settings())
        }
    }

    @Test fun degreeControlsKeepPhaseIndependentAndCenterFractionalExposure() {
        openProject()
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        compose.runOnIdle { store.addNull(false) }
        compose.waitUntil(5000) { store.detail != null }
        compose.runOnIdle { store.setCompositionMotionBlur(true); store.setLayerMotionBlur(store.primary!!, true) }
        compose.waitUntil(5000) { store.compMotionBlur && store.detail!!.motionBlur }
        compose.onNodeWithContentDescription(context.getString(R.string.panel_desfoque_movimento)).performClick()
        compose.onNodeWithTag("motionblur.shutter").assertExists().performTouchInput {
            down(Offset(width * .6f, height / 2f))
            moveBy(Offset(50f, 0f), 150)
            up()
        }
        compose.waitUntil(5000) { store.shutterAngle != 180f }
        assertEquals(-90f, settings().phase, .001f)
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.shutterAngle == 180f }
        compose.runOnIdle { store.changeShutterAngle(181f); store.changeShutterPhase(30f) }
        compose.onNodeWithTag("motionblur.advanced").performScrollTo().performClick()
        compose.onNodeWithTag("motionblur.center").performScrollTo().performClick()
        compose.waitUntil(5000) { store.shutterPhase == -90.5f }
        compose.onNodeWithTag("motionblur.samples").performScrollTo().assertExists()
        compose.onNodeWithTag("motionblur.adaptive").performScrollTo().assertExists()
        stabilityScreenshot("motion-blur-advanced.png")
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.shutterPhase == 30f }
    }
}
