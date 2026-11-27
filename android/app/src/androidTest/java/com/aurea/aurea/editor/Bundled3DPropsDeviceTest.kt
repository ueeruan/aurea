package com.aurea.aurea.editor

import android.app.Application
import android.util.Log
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import java.nio.ByteBuffer
import kotlin.math.abs
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

/** Exercises the model library importer and device GPU through the editor store. */
class Bundled3DPropsDeviceTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore

    private fun openEditor(title: String) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        val factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application)
        compose.setContent {
            store = viewModel(factory = factory)
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(320, 180, 30f, title) }
        compose.waitUntil(15000) { store.project.title == title && !store.projectOperationBusy }
        compose.runOnIdle {
            store.setCompositionBackground(0f, 0f, 0f, 1f)
            store.engineForStress.setEditMode(false)
        }
    }

    private fun capture(frame: Int): ByteArray {
        compose.runOnIdle { store.seek(frame) }
        compose.waitUntil(10000) { store.playhead == frame }
        val rgba = ByteBuffer.allocateDirect(320 * 180 * 4)
        val size = IntArray(2)
        assertEquals(rgba.capacity(), store.engineForStress.captureFrame(320, rgba, size))
        assertArrayEquals(intArrayOf(320, 180), size)
        return ByteArray(rgba.capacity()).also { rgba.rewind(); rgba.get(it) }
    }

    private fun changedPixels(a: ByteArray, b: ByteArray): Int {
        fun channel(bytes: ByteArray, index: Int) = bytes[index].toInt() and 255
        return a.indices.step(4).count { i ->
            abs(channel(a, i) - channel(b, i)) +
                abs(channel(a, i + 1) - channel(b, i + 1)) +
                abs(channel(a, i + 2) - channel(b, i + 2)) > 24
        }
    }

    @Test fun sevenLibraryPropsRenderWithEditablePartsAndSpacing() {
        openEditor("Bundled 3D props device")
        val parts = linkedMapOf(
            "television" to 10, "phone" to 11, "microphone" to 8,
            "headphones" to 10, "console" to 9, "mouse" to 7, "building" to 17,
        )
        val blank = capture(0)
        for ((name, expectedParts) in parts) {
            compose.runOnIdle { store.importLibraryModel("aurea-$name", name) }
            compose.waitUntil(30000) { store.layers.size == 1 && store.busyMessage == null }
            val model = store.layers.single()
            assertTrue("$name must be a 3D layer", model.isThreeD)
            compose.runOnIdle { store.select(model.id, openOptions = false) }
            compose.waitUntil(10000) { store.primary == model.id }
            val names = store.modelPartNames()
            assertEquals("$name named editable part count", expectedParts, names.toSet().size)
            assertTrue("$name has an unnamed part", names.none { it.isBlank() })
            val original = capture(0)
            val visible = changedPixels(blank, original)
            assertTrue("$name was not visible in the device render", visible >= 24)
            Log.i("AureaBundled3D", "$name parts=${names.size} visiblePixels=$visible")
            if (name == "television") {
                val spacing = effectTypeId("aurea.3d.spacing")
                compose.runOnIdle { store.addEffect(spacing, listOf(model.id)) }
                compose.waitUntil(10000) { store.effects.any { it.typeId == spacing } }
                val moved = changedPixels(original, capture(0))
                assertTrue("3D Spacing must change the rendered parts", moved >= 24)
                Log.i("AureaBundled3D", "spacing changedPixels=$moved")
            }
            compose.runOnIdle { store.deleteLayers(listOf(model.id), ripple = false) }
            compose.waitUntil(10000) { store.layers.isEmpty() }
        }
    }

    @Test fun moneyRainUsesThe3DEmitterAndRendersOnDevice() {
        openEditor("3D money rain device")
        val blank = capture(0)
        compose.onNodeWithTag("addBar.more").performClick()
        compose.onNodeWithTag("addBar.grid.Model3D").performClick()
        compose.onNodeWithTag("add.model3d.money").performScrollTo().performClick()
        compose.waitUntil(10000) { store.layers.size == 1 && store.particles != null }
        assertTrue("Money Rain emitter must be 3D", store.layers.single().isThreeD)
        assertEquals("Money Rain must use the money particle shape", 7f, store.particles?.get(21) ?: -1f, 0f)
        val visible = changedPixels(blank, capture(29))
        assertTrue("Money Rain particles are missing from the device render", visible >= 16)
        Log.i("AureaBundled3D", "moneyRain visiblePixels=$visible")
    }
}
