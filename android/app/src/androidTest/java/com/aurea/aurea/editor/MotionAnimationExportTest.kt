package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Color
import android.media.MediaMetadataRetriever
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Uses the production editor, project serializer, Android encoder and decoder.
 * Numeric fixture setup uses normal engine/store APIs; touch editing is covered separately. */
class MotionAnimationExportTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore

    @Test fun textColorAndMaskOpacitySurviveReopenAndReachTheEncodedVideo() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(320, 180, 30f, "Animated text and mask") }
        compose.waitUntil(15000) { store.project.title == "Animated text and mask" }
        var mask = -1
        compose.runOnIdle {
            val layer = store.engineForStress.addText("AUREA")
            assertTrue(layer >= 0); store.select(layer)
            store.addTextAnimator(1 shl 9)
            store.setTextAnimParam(0, 6, 1f)
            store.setTextAnimParam(0, 7, 0f)
            store.setTextAnimParam(0, 8, 0f)
            store.toggleTextAnimKey(0, 6); store.toggleTextAnimKey(0, 8)
            val pts = floatArrayOf(-1000f,-1000f,0f,0f,0f,0f, 1000f,-1000f,0f,0f,0f,0f, 1000f,1000f,0f,0f,0f,0f, -1000f,1000f,0f,0f,0f,0f)
            mask = store.engineForStress.addMask(layer, pts, 4, true)
            assertTrue(mask >= 0); store.toggleMaskParamKey(mask, 2)
            store.setLayerRanges(longArrayOf(layer), intArrayOf(0), intArrayOf(30))
            store.setCompositionDuration(30)
            store.seek(29)
        }
        compose.waitUntil(5000) { store.detail?.localPlayhead == 29 && store.project.durationFrames == 30 }
        val project = File(context.filesDir, "motion-animation.aurea")
        compose.runOnIdle {
            store.setTextAnimParam(0, 6, 0f); store.setTextAnimParam(0, 8, 1f)
            store.setMaskParam(mask, 2, 0.35f)
            assertEquals(0, store.engineForStress.saveProject(project.absolutePath))
            store.newProject(320, 180, 30f, "Reopen motion")
        }
        compose.waitUntil(10000) { store.project.title == "Reopen motion" && store.layers.isEmpty() }
        compose.runOnIdle { store.openProject(project.absolutePath) }
        compose.waitUntil(15000) { store.layers.size == 1 && store.project.durationFrames == 30 }
        val output = File(context.filesDir, "motion-animation.mp4")
        val progress = ExportProgress()
        val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        compose.runOnIdle { assertEquals(0, store.engineForStress.startExport(output.absolutePath, 180, 30.0, 0, 4)) }
        try {
            compose.waitUntil(120000) {
                store.engineForStress.exportProgress(buffer) && run { progress.readFrom(buffer); progress.finished }
            }
            assertEquals(progress.message, 0, progress.result)
            assertEquals(30, progress.framesDone)
            assertTrue(output.length() > 1000)
            val media = MediaMetadataRetriever()
            try {
                media.setDataSource(output.absolutePath)
                assertEquals("320", media.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH))
                assertEquals("180", media.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT))
                fun channels(time: Long): LongArray {
                    val bitmap = checkNotNull(media.getFrameAtTime(time, MediaMetadataRetriever.OPTION_CLOSEST))
                    val pixels = IntArray(bitmap.width * bitmap.height)
                    bitmap.getPixels(pixels, 0, bitmap.width, 0, 0, bitmap.width, bitmap.height)
                    File(context.filesDir, "motion-animation-$time.png").outputStream().use {
                        assertTrue(bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it))
                    }
                    bitmap.recycle()
                    val sums = LongArray(3)
                    for (pixel in pixels) { sums[0] += Color.red(pixel); sums[1] += Color.green(pixel); sums[2] += Color.blue(pixel) }
                    return sums
                }
                val first = channels(0); val last = channels(966667)
                assertTrue("First frame must contain red animated text", first[0] > first[2] * 1.5)
                assertTrue("Last frame must contain blue animated text", last[2] > last[0] * 1.5)
                assertTrue("Animated mask must reduce the visible text intensity", last.sum() < first.sum() * 0.8)
            } finally { media.release() }
        } finally { if (!progress.finished) store.engineForStress.cancelExport() }
    }
}
