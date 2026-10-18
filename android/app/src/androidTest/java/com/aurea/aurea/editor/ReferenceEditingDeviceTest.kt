package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.ParcelFileDescriptor
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.primaryKeys
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File

/** Production editor + real JNI engine: boundary taps, property navigation and atomic mask groups. */
class ReferenceEditingDeviceTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext

    private fun capture(name: String) {
        compose.waitForIdle()
        instrumentation.waitForIdleSync()
        val bitmap = instrumentation.uiAutomation.takeScreenshot() ?: run {
            val capture = instrumentation.uiAutomation.executeShellCommand("screencap -p")
            ParcelFileDescriptor.AutoCloseInputStream(capture).use { checkNotNull(BitmapFactory.decodeStream(it)) }
        }
        File(context.filesDir, "ui-reference-$name.png").outputStream().use {
            assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it))
        }
        bitmap.recycle()
    }

    @Test fun timelineBoundariesNavigationAndMaskGroupUseTheNativeEditor() {
        var grouping by mutableStateOf(false)
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme {
                EditorScreen(store)
                if (grouping) GroupSelectionSheet(store) { grouping = false }
            }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(320, 320, 30f, "Reference editing regression") }
        compose.waitUntil(15000) { store.project.title == "Reference editing regression" && !store.projectOperationBusy }
        compose.runOnIdle { store.addShape(0) }
        compose.waitUntil(10000) { store.layers.size == 1 && store.primary != null }
        compose.runOnIdle { store.toggleTransformKeyframe(intArrayOf(0)) }
        compose.waitUntil(10000) { store.primaryKeys().count { it.property == 0 } == 1 }
        compose.runOnIdle { store.seek(30) }
        compose.waitUntil(10000) { store.playhead == 30 }
        compose.runOnIdle { store.setTransform(0, 180f) }
        compose.waitUntil(10000) { store.primaryKeys().count { it.property == 0 } == 2 }
        compose.runOnIdle { store.setLayerRanges(longArrayOf(store.primary!!), intArrayOf(0), intArrayOf(30)) }
        compose.waitUntil(10000) { store.layers.single().endFrame == 30 }
        compose.runOnIdle {
            val navigation = checkNotNull(store.keyframeNavigation(store.primary!!, TrackKey(0), 15))
            assertEquals(2, navigation.visibleCount)
            assertTrue(navigation.hasPrevious && navigation.hasNext && navigation.hasSegment)
            assertEquals(0, navigation.previousFrame)
            assertEquals(30, navigation.nextFrame)
            assertEquals(0, navigation.segmentStart)
            assertEquals(30, navigation.segmentEnd)
            store.seek(0)
        }
        compose.waitUntil(10000) { store.playhead == 0 }
        capture("first-key")
        val density = context.resources.displayMetrics.density
        compose.onNodeWithTag("editor.timeline").performTouchInput {
            click(Offset(width / 2f, (42f + 2.5f + 23f / 2f) * density))
        }
        compose.waitUntil(10000) { store.selectedKeyframe?.second?.time == 0 }
        // The first-key tap may open its curve. Back restores the layer dock.
        instrumentation.sendKeyDownUpSync(android.view.KeyEvent.KEYCODE_BACK)
        compose.runOnIdle { store.seek(30) }
        compose.waitUntil(10000) { store.playhead == 30 }
        capture("last-key")
        compose.onNodeWithTag("editor.timeline").performTouchInput {
            click(Offset(width / 2f, (42f + 2.5f + 23f / 2f) * density))
        }
        compose.waitUntil(10000) { store.selectedKeyframe?.second?.time == 30 }
        instrumentation.sendKeyDownUpSync(android.view.KeyEvent.KEYCODE_BACK)
        compose.runOnIdle { store.seek(0) }
        compose.waitUntil(10000) { store.playhead == 0 }
        compose.runOnIdle { store.addShape(1) }
        compose.waitUntil(10000) { store.layers.size == 2 }
        val original = store.layers.map { it.id }.toSet()
        // query_layers publishes front-to-back: zIndex 0 is the top UI row.
        val top = store.layers.minBy { it.zIndex }.id
        compose.runOnIdle { store.selectAll(); grouping = true }
        compose.onNodeWithText(context.getString(R.string.editor_group_mask)).assertIsEnabled().performClick()
        compose.waitUntil(10000) { store.layers.size == 1 && store.layers.single().kind == 12 }
        val group = store.layers.single().id
        assertEquals(group, store.primary)
        capture("mask-group")
        compose.runOnIdle { store.openPrecomp(group) }
        compose.waitUntil(10000) { store.precompDepth == 1 && store.layers.size == 2 }
        assertEquals(22, store.layers.minBy { it.zIndex }.blendMode)
        compose.runOnIdle { store.closePrecomp() }
        compose.waitUntil(10000) { store.precompDepth == 0 && store.layers.size == 1 }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(10000) { store.layers.map { it.id }.toSet() == original }
        assertEquals(0, store.layers.single { it.id == top }.blendMode)
        compose.runOnIdle { store.selectAll(); grouping = true }
        compose.onNodeWithText(context.getString(R.string.editor_group_exclude)).assertIsEnabled().performClick()
        compose.waitUntil(10000) { store.layers.size == 1 && store.layers.single().kind == 12 }
        val excludedGroup = store.layers.single().id
        compose.runOnIdle { store.openPrecomp(excludedGroup) }
        compose.waitUntil(10000) { store.precompDepth == 1 && store.layers.size == 2 }
        assertEquals(23, store.layers.minBy { it.zIndex }.blendMode)
        capture("exclude-group-contents")
    }
}
