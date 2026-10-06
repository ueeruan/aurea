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
import com.aurea.aurea.editor.panels.paramOf
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import java.io.File
import java.nio.ByteBuffer
import kotlin.math.abs
import kotlin.math.tan
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

/** Real GPU regression: a catalog emitter must follow only the active camera. */
class ParticleCameraDeviceTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore

    private fun capture(frame: Int): ByteArray {
        compose.runOnIdle { store.seek(frame) }
        compose.waitUntil(10000) { store.playhead == frame }
        val rgba = ByteBuffer.allocateDirect(320 * 180 * 4)
        val size = IntArray(2)
        assertEquals(320 * 180 * 4, store.engineForStress.captureFrame(320, rgba, size))
        assertArrayEquals(intArrayOf(320, 180), size)
        return ByteArray(rgba.capacity()).also { rgba.rewind(); rgba.get(it) }
    }

    private fun centroid(pixels: ByteArray): Pair<Double, Double> {
        var energy = 0.0; var x = 0.0; var y = 0.0
        for (i in pixels.indices step 4) {
            val value = (pixels[i].toInt() and 255) + (pixels[i + 1].toInt() and 255) +
                (pixels[i + 2].toInt() and 255)
            energy += value
            x += value * (i / 4 % 320 + .5)
            y += value * (i / 4 / 320 + .5)
        }
        assertTrue("The emitter must render visible particles", energy > 2000.0)
        return Pair(x / energy, y / energy)
    }

    @Test fun catalogEmitterFollowsCameraPanAndStopsAtItsCut() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle {
            store.newProject(320, 180, 30f, "Particle camera device")
            store.engineForStress.setEditMode(false)
        }
        compose.waitUntil(15000) { store.project.title == "Particle camera device" }
        val engine = store.engineForStress
        // An opaque source reaches the effect stack; Show source=false removes
        // it from the result. This also exercises an ordinary image layer.
        val source = Bitmap.createBitmap(320, 180, Bitmap.Config.ARGB_8888)
        source.eraseColor(Color.WHITE)
        val sourceFile = File(context.filesDir, "particle-camera-source.png")
        val rgba = ByteBuffer.allocateDirect(320 * 180 * 4)
        source.copyPixelsToBuffer(rgba); rgba.rewind()
        sourceFile.outputStream().use { assertTrue(source.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        source.recycle()
        var layer = 0L
        compose.runOnIdle {
            store.setCompositionBackground(0f, 0f, 0f, 1f)
            layer = engine.importImage(rgba, 320, 180, "Emitter source", sourceFile.absolutePath)
            assertTrue(layer > 0); store.select(layer)
        }
        compose.waitUntil(10000) { store.primary == layer && store.detail?.id == layer }
        val type = effectTypeId("aurea.generate.particular") // persisted compatibility key
        compose.runOnIdle { store.addEffect(type, listOf(layer)) }
        compose.waitUntil(10000) { store.effects.any { it.typeId == type } }
        val effect = store.effects.single { it.typeId == type }.effectId
        compose.waitUntil(10000) { store.effectParams[effect]?.size == 40 }
        compose.runOnIdle {
            val values = mapOf(0 to 30f, 1 to 1000f, 22 to 30000f, 24 to 12f, 26 to 100f, 37 to 71f)
            for (p in listOf(5, 6, 7, 9, 15, 16, 17, 18, 20, 23, 25, 28, 29, 31, 36, 38))
                store.setEffectParam(effect, checkNotNull(store.paramOf(effect, p)), 0f)
            for ((p, value) in values) store.setEffectParam(effect, checkNotNull(store.paramOf(effect, p)), value)
        }
        compose.waitUntil(10000) { store.paramOf(effect, 37)?.value?.get(0) == 71f }
        val baseline = centroid(capture(15))
        assertEquals(160.0, baseline.first, 1.0)
        assertEquals(90.0, baseline.second, 1.0)
        compose.runOnIdle { store.seek(0) }
        compose.waitUntil(10000) { store.playhead == 0 }
        val camera = engine.addCamera()
        assertTrue(camera > 0)
        // Param0 is focal length in millimeters on the 24mm-high sensor.
        assertTrue(engine.setCameraParam(camera, 0, (12.0 / tan(Math.toRadians(20.0))).toFloat()))
        assertTrue(engine.layoutTransform(camera, 0, 195f))
        assertTrue(engine.layoutTransform(camera, 1, 90f))
        assertTrue(engine.layoutTransform(camera, 2, (-90.0 / tan(Math.toRadians(20.0))).toFloat()))
        assertTrue(engine.editClipTime(camera, 0, 10))
        assertTrue(engine.editClipTime(camera, 1, 20))
        for (frame in listOf(9, 10, 15, 20, 15, 20, 9)) {
            val at = centroid(capture(frame))
            val expectedX = baseline.first - if (frame in 10 until 20) 35.0 else 0.0
            assertTrue("Camera context wrong at frame $frame: $at", abs(at.first - expectedX) <= 1.0)
            assertEquals(baseline.second, at.second, 1.0)
            Log.i("AureaParticleCamera", "frame=$frame centroid=${at.first},${at.second} expectedX=$expectedX")
        }
    }
}
