package com.aurea.aurea.editor

import android.app.Application
import android.net.Uri
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.effects.EffectTool
import com.aurea.aurea.effects.effectCardId
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File

/** Exercises the real MediaCodec decoder, native worker, UI and undo together. */
class MotionTrackingTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext

    private fun addTracker(tool: EffectTool, label: Int) {
        compose.onNodeWithTag("layer.panel.effects").performClick()
        compose.onNodeWithTag("aurea.effects.add").performScrollTo().performClick()
        compose.onNodeWithTag("effects.search").performClick()
        compose.onNodeWithTag("effects.search.field").performTextInput(context.getString(label))
        val tracker = "effects.result.${effectCardId(tool.typeId)}"
        compose.onNodeWithTag("effects.search.results").performScrollToNode(hasTestTag(tracker))
        compose.onNodeWithTag(tracker).performClick()
        compose.waitUntil(10000) { store.effects.any { it.typeId == tool.typeId } }
    }

    @Test fun globalStabilizerImportsAnalysesAppliesAndUndoes() {
        assertTrue(context.packageName.endsWith(".uitest"))
        val file = File(context.filesDir, "motion-fixture.mp4")
        instrumentation.context.assets.open("motion-fixture.mp4").use { input -> file.outputStream().use { input.copyTo(it) } }
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(480, 320, 30f, "Motion UI") }
        compose.waitUntil(15000) { store.project.title == "Motion UI" && !store.projectOperationBusy }
        compose.runOnIdle { store.importVideo(Uri.fromFile(file)) }
        compose.waitUntil(30000) { store.busyMessage == null }
        compose.runOnIdle { assertNull(store.errorMessage) }
        compose.waitUntil(10000) { store.detail != null && store.layers.isNotEmpty() }
        addTracker(EffectTool.CameraTrack, R.string.fxui_tool_camera_track)
        compose.onNodeWithTag("tracker.camera.panel").assertExists()
        compose.onNodeWithTag("tracker.motion.panel").assertDoesNotExist()
        compose.onNodeWithTag("tracker.camera.analyze").performScrollTo().performClick()
        compose.waitUntil(10000) { store.cameraTrack != null }
        compose.waitUntil(90000) { store.cameraTrack?.state != 1 }
        compose.onNodeWithTag("panel.bar.back").performClick()

        addTracker(EffectTool.Tracking, R.string.trk_motion_title)
        compose.onNodeWithTag("tracker.motion.panel").assertExists()
        compose.onNodeWithTag("tracker.camera.panel").assertDoesNotExist()
        val trackerTypes = listOf(EffectTool.CameraTrack.typeId, EffectTool.Tracking.typeId)
        compose.runOnIdle { assertEquals(trackerTypes, store.effects.map { it.typeId }) }

        compose.onNodeWithText(context.getString(R.string.trk_tool_stabilizer)).performScrollTo().performClick()
        compose.onNodeWithTag("tracker.motion.start").performScrollTo().performClick()
        compose.waitUntil(90000) { store.motionStatus[0].toInt() != 1 }
        compose.runOnIdle {
            assertEquals(store.motionMessage, 2, store.motionStatus[0].toInt())
            assertEquals(60, store.motionStatus[3].toInt())
            assertEquals(0, store.motionStatus[5].toInt())
            store.seek(20)
        }
        compose.onNodeWithText(context.getString(R.string.trk_apply_stabilization)).performScrollTo().performClick()
        compose.waitUntil(10000) { store.effects.size == 3 }
        compose.runOnIdle { assertTrue(store.effects.any { it.typeId == EffectTool.Tracking.typeId }); store.undo() }
        compose.waitUntil(10000) { store.effects.map { it.typeId } == trackerTypes }
        compose.runOnIdle { store.restoreMotion(); assertEquals(2, store.motionStatus[0].toInt()) }
    }
}
