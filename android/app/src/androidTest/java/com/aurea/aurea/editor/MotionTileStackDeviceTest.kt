package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import android.util.Log
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import kotlin.math.abs

/** Editor commands + real Android GPU, with an asymmetric source to detect lost mirroring. */
class MotionTileStackDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun addingEditingAndRemovingEffectsKeepsTheMirroredWallAfterReload() {
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
        val title = "Motion Tile effect stack regression"
        compose.runOnIdle { store.newProject(160, 90, 30f, title) }
        compose.waitUntil(15000) { store.project.title == title }
        val engine = store.engineForStress
        val bitmap = Bitmap.createBitmap(160, 90, Bitmap.Config.ARGB_8888)
        for (y in 0 until 90) for (x in 0 until 160) {
            bitmap.setPixel(x, y, Color.rgb(30 + x * 200 / 159, 45 + y * 140 / 89, if (x < 55) 210 else 35))
        }
        val source = File(context.filesDir, "motion-tile-asymmetric.png")
        source.outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        val rgba = ByteBuffer.allocateDirect(160 * 90 * 4)
        bitmap.copyPixelsToBuffer(rgba)
        rgba.rewind()
        bitmap.recycle()
        var layer = 0L
        compose.runOnIdle {
            layer = engine.importImage(rgba, 160, 90, "Asymmetric mirrored source", source.absolutePath)
            assertTrue(layer > 0)
            store.select(layer)
        }
        compose.waitUntil(10000) { store.primary == layer && store.detail?.id == layer }

        fun add(key: String): Int {
            val type = effectTypeId(key)
            compose.runOnIdle { store.addEffect(type, listOf(layer)) }
            compose.waitUntil(10000) { store.effects.any { it.typeId == type } }
            val id = store.effects.single { it.typeId == type }.effectId
            compose.waitUntil(10000) { store.effectParams[id]?.isNotEmpty() == true }
            return id
        }
        fun set(id: Int, index: Int, value: Float, component: Int = 0) {
            compose.runOnIdle { store.setEffectParam(id, store.effectParams.getValue(id).single { it.index == index }, value, component) }
            compose.waitUntil(10000) { store.effectParams[id]?.singleOrNull { it.index == index }?.value?.get(component) == value }
        }
        val tile = add("aurea.stylize.motion_tile")
        set(tile, 1, 50f)
        set(tile, 2, 50f)
        set(tile, 5, 1f)
        val output = ByteBuffer.allocateDirect(160 * 90 * 4)
        fun capture(): ByteArray {
            val size = IntArray(2)
            output.clear()
            assertEquals(160 * 90 * 4, engine.captureFrame(160, output, size))
            assertArrayEquals(intArrayOf(160, 90), size)
            return ByteArray(160 * 90 * 4) { output.get(it) }
        }
        fun mirror(label: String, seam: Int): ByteArray {
            val bytes = capture()
            var worst = 0
            var contrast = 0
            fun value(x: Int, y: Int, c: Int) = bytes[(y * 160 + x) * 4 + c].toInt() and 255
            for (y in listOf(32, 40, 48, 56)) for (k in 3..14) for (c in 0..2) {
                worst = maxOf(worst, abs(value(seam - k, y, c) - value(seam + k - 1, y, c)))
                contrast = maxOf(contrast, abs(value(seam + 3, y, c) - value(seam + 14, y, c)))
            }
            Log.i("AureaDeviceTest", "Motion Tile stack $label: mirrorError=$worst contrast=$contrast")
            assertTrue("$label mirror error $worst", worst <= 5)
            assertTrue("$label must retain the patterned source, not a blank/stretched edge ($contrast)", contrast >= 12)
            compose.runOnIdle {
                assertTrue(store.effects.any { it.effectId == tile })
                assertEquals(1f, store.effectParams.getValue(tile).single { it.index == 5 }.value[0], .001f)
            }
            return bytes
        }
        val original = mirror("alone", 40)
        val blur = add("aurea.blur.gaussian")
        set(blur, 0, 3f)
        val blurred = mirror("plus Gaussian", 40)
        assertFalse("Adding blur must invalidate the previous preview", original.contentEquals(blurred))
        val glow = add("aurea.light.glow")
        set(glow, 0, 15f)
        set(glow, 1, 5f)
        set(glow, 2, .3f)
        mirror("plus Glow", 40)
        val transform = add("aurea.transform")
        set(transform, 2, 50f, 0)
        set(transform, 2, 50f, 1)
        mirror("plus Transform", 60)
        compose.runOnIdle { store.removeEffect(transform) }
        compose.waitUntil(10000) { store.effects.none { it.effectId == transform } }
        mirror("remove Transform", 40)
        set(blur, 0, 6f)
        val beforeReload = mirror("edit previous blur", 40)
        val project = File(context.filesDir, "motion-tile-stack.aurea")
        assertEquals(0, engine.saveProject(project.absolutePath))
        assertEquals(0, engine.loadProject(project.absolutePath))
        assertArrayEquals("Saved effect stack must reproduce identical pixels", beforeReload, capture())
        stabilityScreenshot("motion-tile-effect-stack.png")
    }
}
