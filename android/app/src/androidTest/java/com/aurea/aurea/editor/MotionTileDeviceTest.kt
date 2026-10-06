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
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import kotlin.math.abs

/** Real Android rendering of a repeated source beyond the old 24x coverage cap. */
class MotionTileDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun tinyAndDistantLayerKeepsEveryPixelCoveredAndExposesUniformScale() {
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
        val title = "Motion Tile real-device coverage"
        compose.runOnIdle { store.newProject(160, 90, 30f, title) }
        compose.waitUntil(15000) { store.project.title == title }
        val engine = store.engineForStress
        val source = File(context.filesDir, "motion-tile-solid.png")
        val bitmap = Bitmap.createBitmap(64, 36, Bitmap.Config.ARGB_8888)
        bitmap.eraseColor(Color.rgb(220, 220, 220))
        val rgba = ByteBuffer.allocateDirect(64 * 36 * 4)
        bitmap.copyPixelsToBuffer(rgba)
        rgba.rewind()
        source.outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        bitmap.recycle()
        var layer = 0L
        compose.runOnIdle {
            store.setCompositionBackground(0f, 0f, 0f, 1f)
            layer = engine.importImage(rgba, 64, 36, "Opaque tile source", source.absolutePath)
            assertTrue(layer > 0)
            store.select(layer)
        }
        compose.waitUntil(10000) { store.primary == layer && store.detail?.id == layer }
        val type = effectTypeId("aurea.stylize.motion_tile")
        compose.runOnIdle { store.addEffect(type, listOf(layer)) }
        compose.waitUntil(10000) { store.effects.any { it.typeId == type } }
        val effect = store.effects.single { it.typeId == type }.effectId
        compose.waitUntil(10000) { store.effectParams[effect]?.any { it.index == 10 } == true }
        compose.runOnIdle {
            // Use the same metadata path as the editor, including the appended
            // parameter index: older width/height/keyframe addresses stay intact.
            val spec = store.effectSpecs(type).single { it.index == 10 }
            val scale = store.effectParams.getValue(effect).single { it.index == 10 }
            assertFalse(spec.hidden)
            assertTrue(spec.label.isNotBlank())
            assertEquals("%", spec.unit)
            assertEquals(100f, spec.defaultValue[0], .001f)
            assertEquals(1000f, spec.hardMax, .001f)
            store.setEffectParam(effect, scale, 75f)
            store.setEffectParam(effect, store.effectParams.getValue(effect).single { it.index == 5 }, 1f)
        }
        compose.waitUntil(10000) { store.effectParams[effect]?.singleOrNull { it.index == 10 }?.value?.get(0) == 75f }

        val output = ByteBuffer.allocateDirect(160 * 90 * 4)
        for (scale in listOf(.01f, .03f, .5f)) {
            for (x in listOf(80f, -2000f)) {
                compose.runOnIdle {
                    store.setTransform2(TrackProperty.SCALE_X, scale, TrackProperty.SCALE_Y, scale)
                    store.setTransform2(TrackProperty.POSITION_X, x, TrackProperty.POSITION_Y, 45f)
                }
                compose.waitUntil(10000) {
                    val detail = store.detail
                    detail != null && abs(detail.scale[0] - scale) < .0001f && abs(detail.position[0] - x) < .01f
                }
                val size = IntArray(2)
                output.clear()
                // Offscreen capture executes the actual device GPU backend;
                // keep the Android UI thread free while rendering completes.
                val count = engine.captureFrame(160, output, size)
                assertEquals(160, size[0])
                assertEquals(90, size[1])
                assertEquals(160 * 90 * 4, count)
                var darkPixels = 0
                var minimum = 255
                for (i in 0 until count step 4) {
                    val value = output.get(i).toInt() and 255
                    minimum = minOf(minimum, value)
                    if (value < 190) ++darkPixels
                }
                Log.i("AureaDeviceTest", "Motion Tile scale=$scale x=$x min=$minimum dark=$darkPixels")
                assertEquals("No black gap, including outermost pixels: scale=$scale x=$x min=$minimum", 0, darkPixels)
            }
        }
        stabilityScreenshot("motion-tile-tiny-distant.png")
    }
}
