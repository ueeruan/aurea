package com.aurea.aurea.editor

import android.app.Application
import android.net.Uri
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
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
        compose.waitUntil(15000) { store.project.title == "Motion UI" }
        compose.runOnIdle { store.importVideo(Uri.fromFile(file)) }
        compose.waitUntil(30000) { store.busyMessage == null }
        compose.runOnIdle { assertNull(store.errorMessage) }
        compose.waitUntil(10000) { store.detail != null && store.layers.isNotEmpty() }
        compose.onNodeWithContentDescription(context.getString(R.string.editor_rastreio)).performClick()
        compose.onNodeWithText("Stabilizer").performScrollTo().performClick()
        compose.waitUntil(90000) { store.motionStatus[0].toInt() != 1 }
        compose.runOnIdle {
            assertEquals(store.motionMessage, 2, store.motionStatus[0].toInt())
            assertEquals(60, store.motionStatus[3].toInt())
            assertEquals(0, store.motionStatus[5].toInt())
            store.seek(20)
        }
        compose.onNodeWithText("Apply stabilization").performScrollTo().performClick()
        compose.waitUntil(10000) { store.effects.isNotEmpty() }
        compose.runOnIdle { assertEquals(1, store.effects.size); store.undo() }
        compose.waitUntil(10000) { store.effects.isEmpty() }
        compose.runOnIdle { store.restoreMotion(); assertEquals(2, store.motionStatus[0].toInt()) }
    }
}
