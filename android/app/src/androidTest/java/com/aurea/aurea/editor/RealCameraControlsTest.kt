package com.aurea.aurea.editor

import android.app.Application
import android.os.SystemClock
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.LifecycleRegistry
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Actual Compose controls -> EditorStore -> JNI capture. A supplied pose is
 * deliberate: this test does not claim emulator AR camera hardware tracking. */
class RealCameraControlsTest {
    @get:Rule val compose = createComposeRule()

    @Test fun recordStopAndDoneUseNativeStateAndCreateFirstKeys() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName == "com.aurea.aurea.uitest")
        lateinit var store: EditorStore
        var rendered = false
        // Keep external sensors paused while exercising the real UI with a known pose.
        lateinit var owner: LifecycleOwner
        InstrumentationRegistry.getInstrumentation().runOnMainSync {
            owner = object : LifecycleOwner {
                private val registry = LifecycleRegistry(this).apply { currentState = Lifecycle.State.CREATED }
                override val lifecycle: Lifecycle get() = registry
            }
        }
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            rendered = true
            AureaTheme {
                CompositionLocalProvider(LocalLifecycleOwner provides owner) {
                    Box(Modifier.width(360.dp)) { RealCameraBar(store, Modifier) }
                }
            }
        }
        compose.waitUntil(30000) { rendered && store.engineReady }
        compose.runOnIdle { store.newProject(800, 600, 30f, "Real Camera Compose controls") }
        compose.waitUntil(30000) {
            store.screen == com.aurea.aurea.state.Screen.Editor && store.project.title == "Real Camera Compose controls"
        }
        compose.runOnIdle { store.startRealCamera() }
        compose.waitUntil(10000) { RealCamera.session != null && store.layers.any { it.id == RealCamera.session?.layer } }
        val layer = RealCamera.session!!.layer
        compose.onNodeWithTag("realcam.gravar").assertIsDisplayed().performClick()
        compose.runOnIdle {
            assertTrue(RealCamera.recording)
            assertEquals(1L, store.realCameraStatus()[1])
            val identity = floatArrayOf(1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f)
            assertTrue(store.realCameraSample(identity, SystemClock.elapsedRealtimeNanos(), false))
            store.syncRealCameraStatus()
            assertTrue(RealCamera.recording)
            assertTrue(RealCamera.lastFrame >= 0)
            // Losing a sensor must never disable Stop for an active take.
            RealCamera.available = false
        }
        compose.onNodeWithTag("realcam.gravar").assertIsDisplayed().performClick()
        compose.runOnIdle {
            assertFalse(RealCamera.recording)
            assertEquals(0L, store.realCameraStatus()[1])
            val buffer = ByteBuffer.allocateDirect(PodLayout.KEYFRAME_ROW_BYTES * 64).order(ByteOrder.nativeOrder())
            val count = store.engineForStress.queryKeyframes(layer, buffer, 64)
            assertEquals(3, count)
            val keys = List(count) { KeyframeRow.read(buffer, it) }
            assertEquals(setOf(TrackProperty.ROTATION_X, TrackProperty.ROTATION_Y, TrackProperty.ROTATION_Z), keys.map { it.property }.toSet())
            val saved = File(context.filesDir, "real-camera-2175/owned-compose-controls.aurea").apply { parentFile!!.mkdirs() }
            assertEquals(0, store.engineForStress.saveProject(saved.absolutePath))
        }
        compose.onNodeWithTag("realcam.concluir").performClick()
        compose.waitUntil(5000) { RealCamera.session == null }
        compose.runOnIdle { assertEquals(0L, store.realCameraStatus()[0]) }
    }
}
