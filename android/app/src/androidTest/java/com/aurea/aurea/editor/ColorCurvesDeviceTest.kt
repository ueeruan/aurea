package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import android.media.MediaMetadataRetriever
import android.os.SystemClock
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

class ColorCurvesDeviceTest {
    @get:Rule val compose = createComposeRule()
    @Test fun pointsChangePixelsUndoChannelsSaveAndVideoExport() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true; AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(320, 180, 30f, "Color curves") }
        compose.waitUntil(15000) { store.project.title == "Color curves" && !store.projectOperationBusy }
        val bitmap = Bitmap.createBitmap(320, 180, Bitmap.Config.ARGB_8888).apply { eraseColor(Color.rgb(51, 51, 51)) }
        val source = File(context.filesDir, "curves-source.png")
        source.outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
        val input = ByteBuffer.allocateDirect(320 * 180 * 4); bitmap.copyPixelsToBuffer(input); input.rewind(); bitmap.recycle()
        val engine = store.engineForStress
        var layer = 0L
        compose.runOnIdle { layer = engine.importImage(input, 320, 180, "Gray", source.absolutePath); store.select(layer) }
        compose.waitUntil(10000) { store.detail?.id == layer }
        compose.runOnIdle { store.addEffect(effectTypeId("aurea.color.curves")) }
        compose.waitUntil(10000) { store.effects.size == 1 }
        val effect = store.effects.single().effectId
        compose.onNodeWithTag("dock.tool.Effects").performScrollTo().performClick()
        val graph = compose.onNodeWithTag("fx.curve.graph")
        graph.performScrollTo().assertIsDisplayed()
        graph.performTouchInput { click(Offset(width / 2f, height * .25f)) }
        compose.waitUntil(5000) { store.effectCurve(effect, 0, 0).size == 6 }
        val added = store.effectCurve(effect, 0, 0)
        assertEquals(.5f, added[2], .015f); assertTrue(added[3] > .7f)
        graph.performTouchInput {
            down(Offset(width / 2f, height * .25f)); moveTo(Offset(width * .6f, height * .35f)); up()
        }
        val moved = store.effectCurve(effect, 0, 0)
        assertTrue(moved[2] > .55f)
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { kotlin.math.abs(store.effectCurve(effect, 0, 0)[2] - .5f) < .015f }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.effectCurve(effect, 0, 0).size == 4 }
        compose.runOnIdle { store.redo() }
        compose.waitUntil(5000) { store.effectCurve(effect, 0, 0).size == 6 }
        compose.onNodeWithTag("fx.curve.channel.1").performScrollTo().performClick()
        assertEquals(4, store.effectCurve(effect, 0, 1).size)
        compose.onNodeWithTag("fx.curve.reset").performScrollTo().performClick()
        assertEquals(6, store.effectCurve(effect, 0, 0).size) // resetting R must preserve RGB
        val out = ByteBuffer.allocateDirect(320 * 180 * 4); val dimensions = IntArray(2)
        assertEquals(320 * 180 * 4, engine.captureFrame(320, out, dimensions))
        val red = out.get((90 * 320 + 160) * 4).toInt() and 255
        assertTrue("The edited curve must brighten actual pixels: $red", red > 70)
        compose.runOnIdle { store.setCompositionDuration(4) }
        compose.waitUntil(5000) { store.project.durationFrames == 4 }
        val project = File(context.filesDir, "curves-edited.aurea")
        assertEquals(0, engine.saveProject(project.absolutePath)); assertEquals(0, engine.loadProject(project.absolutePath))
        assertEquals(6, engine.effectCurve(layer, effect, 0, 0).size)
        val output = File(context.getExternalFilesDir(null), "curves-edited.mp4")
        assertEquals(0, engine.startExport(output.absolutePath, 360, 30.0, 0, 0, quality = 2))
        val progress = ExportProgress(); val progressBuffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        val deadline = SystemClock.elapsedRealtime() + 60000
        try {
            do {
                assertTrue(engine.exportProgress(progressBuffer)); progress.readFrom(progressBuffer)
                if (progress.finished) break
                SystemClock.sleep(40)
            } while (SystemClock.elapsedRealtime() < deadline)
            assertTrue(progress.finished); assertEquals(progress.message, 0, progress.result); assertEquals(4, progress.framesDone)
            MediaMetadataRetriever().use { decoder ->
                decoder.setDataSource(output.absolutePath)
                val frame = checkNotNull(decoder.getFrameAtTime(0, MediaMetadataRetriever.OPTION_CLOSEST))
                val exported = Color.red(frame.getPixel(frame.width / 2, frame.height / 2)); frame.recycle()
                assertEquals("Export and preview must use the same curve", red.toFloat(), exported.toFloat(), 12f)
            }
        } finally { if (!progress.finished) engine.cancelExport() }
    }
}
