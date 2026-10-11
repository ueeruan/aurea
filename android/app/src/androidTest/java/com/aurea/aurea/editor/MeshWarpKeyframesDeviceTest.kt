package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.editor.timeline.buildRow
import com.aurea.aurea.editor.timeline.focusedKeys
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File

/** Native mesh data, Compose marker/focus, timeline instants and real GPU pixels. */
class MeshWarpKeyframesDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun distortionMeshCanBeMarkedSeenEditedAndUndoneFromTheEffectCard() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(320, 180, 30f, "Mesh keys visible") }
        compose.waitUntil(10000) { store.project.title == "Mesh keys visible" }
        compose.runOnIdle { store.addShape(1); store.addEffect(effectTypeId("aurea.distort.mesh_warp")) }
        compose.waitUntil(10000) { store.effects.size == 1 }
        val effect = store.effects.single().effectId
        val layer = checkNotNull(store.primary)
        compose.onNodeWithTag("dock.tool.Effects").performScrollTo().performClick()
        compose.onNodeWithTag("fx.mesh_warp.key").performScrollTo().assertIsEnabled().performClick()
        compose.waitUntil(10000) {
            store.keyframes[layer].orEmpty().any { it.property == TrackProperty.MESH_WARP && it.effectIndex == effect && it.time == 0 }
        }
        assertEquals(listOf(TrackKey(TrackProperty.MESH_WARP, effect, 0)), store.timelineFocus)
        assertTrue("Rows/columns/quality must remain nonanimatable", store.effectParams[effect].orEmpty().all { it.flags and 1 == 0 })
        val directory = File(context.getExternalFilesDir(null), "mesh-warp-effects").apply { mkdirs() }
        fun capture(name: String): Bitmap {
            val image = checkNotNull(store.captureBitmap(320))
            File(directory, "$name.png").outputStream().use { image.compress(Bitmap.CompressFormat.PNG, 100, it) }
            return image
        }
        val original = capture("mesh-identity")
        compose.runOnIdle { store.seek(30) }
        compose.waitUntil(10000) { store.detail?.localPlayhead == 30 }
        compose.runOnIdle { store.meshWarpDrag(layer, effect, 0, 0, 0.25f, 0.25f, false) }
        compose.waitUntil(10000) {
            store.keyframes[layer].orEmpty().count { it.property == TrackProperty.MESH_WARP && it.effectIndex == effect } == 2
        }
        val keys = store.keyframes[layer].orEmpty()
        val row = buildRow(store.layers.first { it.id == layer }, focusedKeys(keys, checkNotNull(store.timelineFocus)))
        assertArrayEquals("The visible timeline must include both mesh keys", intArrayOf(0, 30), row.instants)
        val deformed = capture("mesh-deformed")
        var bright = 0
        var changed = 0
        for (y in 0 until deformed.height) for (x in 0 until deformed.width) {
            val pixel = deformed.getPixel(x, y)
            if ((pixel and 0x00ffffff) != 0) ++bright
            if (pixel != original.getPixel(x, y)) ++changed
        }
        assertTrue("Mesh Warp must not make its layer invisible", bright > 100)
        assertTrue("A moved corner must change the native rendered image", changed > 20)
        // Explicit marker removes only this key; an undo restores it.
        compose.onNodeWithTag("fx.mesh_warp.key").performScrollTo().performClick()
        compose.waitUntil(10000) { store.keyframes[layer].orEmpty().count { it.property == TrackProperty.MESH_WARP && it.effectIndex == effect } == 1 }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(10000) { store.keyframes[layer].orEmpty().count { it.property == TrackProperty.MESH_WARP && it.effectIndex == effect } == 2 }
        File(directory, "mesh-keyframes-proof.txt").writeText("track=44 effect=$effect times=0,30 visible=true changedPixels=$changed visiblePixels=$bright undo=true")
    }
}
