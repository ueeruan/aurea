package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.engine.EngineStatus
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Real transport callbacks and JNI, isolated from personal projects by the uitest package. */
class TransportNavigationDeviceTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore

    private fun seek(frame: Int) {
        compose.runOnIdle { store.seek(frame) }
        compose.waitUntil(5000) { store.playhead == frame }
    }

    private fun arrow(tag: String, expected: Int) {
        compose.onNodeWithTag("transport.$tag").performClick()
        compose.waitUntil(5000) { store.playhead == expected }
        compose.runOnIdle { assertEquals(expected, store.playhead) }
    }

    @Test fun activeLayerKeysAndMarkersCompeteByDistanceInBothDirections() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName == "com.aurea.aurea.uitest")
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(320, 240, 30f, "Beta 3 transport navigation") }
        compose.waitUntil(15000) { store.project.title == "Beta 3 transport navigation" && !store.projectOperationBusy }
        compose.runOnIdle { store.addNull(false) }
        compose.waitUntil(5000) { store.layers.size == 1 }
        val first = store.layers.single().id
        compose.runOnIdle { store.select(first, openOptions = false) }
        for (frame in listOf(20, 60)) {
            seek(frame)
            compose.runOnIdle { store.toggleTransformKeyframe(intArrayOf(0)) }
            compose.waitUntil(5000) { store.keyframes[first].orEmpty().any { it.property == 0 && it.time == frame } }
        }
        for (frame in listOf(40, 80)) {
            seek(frame)
            compose.runOnIdle { store.toggleMarker() }
            compose.waitUntil(5000) { frame in store.markers.frames }
        }
        seek(0)
        compose.runOnIdle { store.addNull(false) }
        compose.waitUntil(5000) { store.layers.size == 2 }
        val second = store.layers.single { it.id != first }.id
        compose.runOnIdle { store.select(second, openOptions = false) }
        seek(30)
        compose.runOnIdle { store.toggleTransformKeyframe(intArrayOf(0)) }
        compose.waitUntil(5000) { store.keyframes[second].orEmpty().any { it.property == 0 && it.time == 30 } }

        compose.runOnIdle { store.select(first, openOptions = false) }
        seek(0)
        arrow("next", 20); arrow("next", 40); arrow("next", 60); arrow("next", 80)
        arrow("next", 80) // No frame step after the last event.
        arrow("previous", 60); arrow("previous", 40); arrow("previous", 20)
        arrow("previous", 20) // The other layer's key never participates.

        compose.runOnIdle { store.clearSelection() }
        seek(0)
        arrow("next", 40); arrow("next", 80); arrow("previous", 40)
        arrow("previous", 40) // Without a layer, skip all project keyframes.
        compose.runOnIdle { store.select(second, openOptions = false) }
        seek(0)
        arrow("next", 30); arrow("next", 40)
        compose.runOnIdle { store.setLayerRanges(longArrayOf(first, second), intArrayOf(0, 0), intArrayOf(60, 60)) }
        compose.waitUntil(5000) { store.layers.all { it.endFrame == 60 } }
        arrow("next", 80) // A legal project marker after both clips is still reachable.
        val buffer = ByteBuffer.allocateDirect(PodLayout.STATUS_BYTES).order(ByteOrder.nativeOrder())
        compose.runOnIdle {
            assertTrue(store.engineForStress.readStatus(buffer))
            assertEquals(80L, EngineStatus().also { it.readFrom(buffer) }.playhead)
        }
        compose.runOnIdle { store.clearSelection() }
        seek(40)
        arrow("next", 80)
        compose.runOnIdle {
            assertTrue(store.engineForStress.readStatus(buffer))
            assertEquals(80L, EngineStatus().also { it.readFrom(buffer) }.playhead)
        }
    }
}
