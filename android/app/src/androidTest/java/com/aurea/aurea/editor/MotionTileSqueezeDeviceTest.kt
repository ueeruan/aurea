package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
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

class MotionTileSqueezeDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun tiledWallHasNoEmptyPixelsThroughStrongRotatedSqueeze() {
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
        compose.runOnIdle { store.newProject(160, 90, 30f, "Squeeze tiled wall") }
        compose.waitUntil(10000) { store.project.title == "Squeeze tiled wall" }
        val bitmap = Bitmap.createBitmap(160, 90, Bitmap.Config.ARGB_8888)
        bitmap.eraseColor(Color.rgb(200, 200, 200))
        val source = File(context.filesDir, "squeeze-wall.png")
        source.outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        val input = ByteBuffer.allocateDirect(160 * 90 * 4)
        bitmap.copyPixelsToBuffer(input); input.rewind(); bitmap.recycle()
        val engine = store.engineForStress
        var layer = 0L
        compose.runOnIdle {
            layer = engine.importImage(input, 160, 90, "Wall", source.absolutePath)
            store.select(layer)
        }
        compose.waitUntil(10000) { store.detail?.id == layer }
        compose.runOnIdle { store.setTransform2(3, .25f, 4, .25f) }
        fun add(key: String): Int {
            val type = effectTypeId(key)
            compose.runOnIdle { store.addEffect(type, listOf(layer)) }
            compose.waitUntil(10000) { store.effects.any { it.typeId == type } }
            return store.effects.single { it.typeId == type }.effectId
        }
        fun set(id: Int, index: Int, value: Float) {
            compose.waitUntil(10000) { store.effectParams[id]?.any { it.index == index } == true }
            compose.runOnIdle { store.setEffectParam(id, store.effectParams.getValue(id).single { it.index == index }, value) }
            compose.waitUntil(10000) { store.effectParams[id]?.single { it.index == index }?.value?.get(0) == value }
        }
        val tile = add("aurea.stylize.motion_tile")
        set(tile, 5, 1f)
        val squeeze = add("aurea.distort.squeeze")
        val output = ByteBuffer.allocateDirect(160 * 90 * 4)
        for (strength in listOf(-90f, 25f, 75f, 95f)) for (axis in listOf(0f, 45f, 90f)) {
            set(squeeze, 0, strength); set(squeeze, 2, axis)
            val size = IntArray(2)
            output.clear()
            assertEquals(160 * 90 * 4, engine.captureFrame(160, output, size))
            assertArrayEquals(intArrayOf(160, 90), size)
            for (pixel in 0 until 160 * 90) {
                assertTrue("Empty pixel $pixel at strength=$strength axis=$axis", (output.get(pixel * 4).toInt() and 255) >= 190)
            }
        }
    }
}
