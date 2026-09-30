package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.ui.geometry.Offset
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
import kotlin.math.abs

/** Real pointer events, native command queue, undo and preview surface. */
class PreviewGestureRegressionTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore

    private fun launch() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(720, 900, 30f, "Preview gesture regressions") }
        compose.waitUntil(15000) { store.project.title == "Preview gesture regressions" }
        compose.runOnIdle { store.addShape(1); store.snapping = false; StageView.zoomLock = false }
        compose.waitUntil(10000) { store.primary != null && store.detail?.id == store.primary }
        compose.waitForIdle()
    }

    @Test fun stationaryPinchCreatesNoUndoAndDoesNotMoveTheLayer() {
        launch()
        val before = store.detail!!
        compose.onNodeWithTag("editor.stage").performTouchInput {
            down(0, center - Offset(70f, 0f)); down(1, center + Offset(70f, 0f))
            repeat(5) { move(40) }
            up(0); up(1)
        }
        compose.waitForIdle()
        assertEquals(before.scale, store.queryDetail(before.id)!!.scale)
        assertEquals(before.rotation, store.queryDetail(before.id)!!.rotation)
        assertEquals(before.position, store.queryDetail(before.id)!!.position)
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.layers.isEmpty() } // Undo creation, no phantom pinch step.
    }

    @Test fun scaleLimitReversesImmediatelyAndRemainingFingerCannotMoveLayer() {
        launch()
        compose.runOnIdle { store.setScale3(floatArrayOf(50f, 50f, 1f)) }
        compose.waitUntil(5000) { store.detail!!.scale[0] == 50f }
        val before = store.detail!!
        compose.onNodeWithTag("editor.stage").performTouchInput {
            down(0, center - Offset(40f, 0f)); down(1, center + Offset(40f, 0f))
            updatePointerTo(0, center - Offset(100f, 0f)); updatePointerTo(1, center + Offset(100f, 0f)); move(80)
            updatePointerTo(0, center - Offset(80f, 0f)); updatePointerTo(1, center + Offset(80f, 0f)); move(80)
            up(0)
            moveTo(1, center + Offset(130f, -40f), 80); up(1)
        }
        compose.waitUntil(5000) { abs(store.detail!!.scale[0] - 80f) < .01f }
        val after = store.queryDetail(before.id)!!
        assertEquals(80f, after.scale[1], .01f)
        assertEquals(1f, after.scale[2], .00001f)
        assertEquals(before.position, after.position)
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.detail!!.scale == before.scale }
    }

    @Test fun cancellationClosesUndoGroupAndNextPinchWorks() {
        launch()
        fun pinch(cancelled: Boolean) {
            compose.onNodeWithTag("editor.stage").performTouchInput {
                down(0, center - Offset(100f, 0f)); down(1, center + Offset(100f, 0f))
                updatePointerTo(0, center - Offset(70f, 0f)); updatePointerTo(1, center + Offset(70f, 0f)); move(80)
                if (cancelled) cancel() else { up(0); up(1) }
            }
        }
        val original = store.detail!!.scale
        pinch(true)
        compose.waitUntil(5000) { abs(store.detail!!.scale[0] - original[0] * .7f) < .001f }
        val first = store.detail!!.scale
        pinch(false)
        compose.waitUntil(5000) { abs(store.detail!!.scale[0] - first[0] * .7f) < .001f }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.detail!!.scale == first }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.detail!!.scale == original }
    }

    @Test fun magnifierReversesAtZoomLimitWithoutEditingTheLayer() {
        launch()
        val before = store.detail!!
        compose.runOnIdle { StageView.zoomLock = true; StageView.set(store, 7f, 0f, 0f) }
        compose.onNodeWithTag("editor.stage").performTouchInput {
            down(0, center - Offset(80f, 0f)); down(1, center + Offset(80f, 0f))
            updatePointerTo(0, center - Offset(100f, 0f)); updatePointerTo(1, center + Offset(100f, 0f)); move(80)
            updatePointerTo(0, center - Offset(90f, 0f)); updatePointerTo(1, center + Offset(90f, 0f)); move(80)
            up(0); up(1)
        }
        compose.waitUntil(5000) { abs(StageView.zoom - 7.2f) < .001f }
        assertEquals(before.scale, store.queryDetail(before.id)!!.scale)
        assertEquals(before.position, store.queryDetail(before.id)!!.position)
        compose.runOnIdle { StageView.zoomLock = false; StageView.reset(store); store.undo() }
        compose.waitUntil(5000) { store.layers.isEmpty() }
    }
}
