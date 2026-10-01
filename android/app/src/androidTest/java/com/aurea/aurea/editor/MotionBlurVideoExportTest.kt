package com.aurea.aurea.editor

import android.app.Application
import android.media.MediaMetadataRetriever
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Production video decode, optical flow, transform blur and MP4 encoding. */
class MotionBlurVideoExportTest {
    @get:Rule val compose = createComposeRule()

    @Test fun vectorAndTransformBlurFinishAndProduceReadableVideo() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        val video = File(context.filesDir, "blur-vfr-input.mp4")
        instrumentation.context.assets.open("preview-vfr.mp4").use { input ->
            video.outputStream().use { input.copyTo(it) }
        }
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        for (mode in 1..3) {
            compose.runOnIdle { store.newProject(1920, 1080, 30f, "Blur export $mode") }
            compose.waitUntil(15000) { store.project.title == "Blur export $mode" }
            compose.runOnIdle {
                val engine = store.engineForStress
                val layer = engine.importVideo(video.absolutePath, "VFR fixture")
                assertTrue(layer >= 0)
                store.select(layer)
                store.setLayerRanges(longArrayOf(layer), intArrayOf(0), intArrayOf(30))
                store.setCompositionDuration(30)
                engine.beginCommandBatch()
                val commands = CommandBatch(engine)
                commands.insertKeyframe(layer, TrackProperty.POSITION_X, -1, 0, 0, 700f)
                commands.insertKeyframe(layer, TrackProperty.POSITION_X, -1, 0, 29, 1200f)
                assertEquals(2, engine.submitCommands())
                assertTrue(engine.setVectorBlur(layer, if (mode and 1 != 0) 1f else 0f))
                assertTrue(engine.setMotionBlur(layer, mode and 2 != 0))
                engine.setCompositionMotionBlur(true)
                assertTrue(engine.motionBlurState() > 0)
            }
            compose.waitUntil(10000) { store.project.durationFrames == 30 }
            val output = File(context.filesDir, "motion-blur-$mode.mp4")
            val progress = ExportProgress()
            val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
            compose.runOnIdle {
                assertEquals(0, store.engineForStress.startExport(output.absolutePath, 1080, 30.0, 0, 12))
            }
            try {
                compose.waitUntil(300000) {
                    store.engineForStress.exportProgress(buffer) && run { progress.readFrom(buffer); progress.finished }
                }
                assertEquals("mode=$mode ${progress.message}", 0, progress.result)
                assertEquals(30, progress.framesDone)
                assertFalse("mode=$mode exported approximate frames", progress.frameFallback)
                assertTrue(output.length() > 1000)
                val media = MediaMetadataRetriever()
                try {
                    media.setDataSource(output.absolutePath)
                    assertEquals("1920", media.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH))
                    assertEquals("1080", media.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT))
                    for (time in listOf(0L, 500000L, 966667L)) {
                        val frame = media.getFrameAtTime(time, MediaMetadataRetriever.OPTION_CLOSEST)
                        assertNotNull("mode=$mode frame=$time", frame)
                        frame?.recycle()
                    }
                } finally { media.release() }
            } finally {
                File(context.filesDir, "motion-blur-$mode-progress.txt").writeText(
                    "done=${progress.framesDone}/${progress.framesTotal} finished=${progress.finished} result=${progress.result} flags=${progress.flags} message=${progress.message}"
                )
                if (!progress.finished) store.engineForStress.cancelExport()
            }
        }
    }
}
