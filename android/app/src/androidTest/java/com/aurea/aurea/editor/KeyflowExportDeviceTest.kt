package com.aurea.aurea.editor

import android.app.Application
import android.media.MediaMetadataRetriever
import android.os.SystemClock
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
import kotlin.math.PI
import kotlin.math.sin

/** Real renderer, GPU readback, platform encoders and independent decoder. */
class KeyflowExportDeviceTest {
    @get:Rule val compose = createComposeRule()
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private lateinit var store: EditorStore
    private fun initialize() {
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
    }
    private fun project(width: Int, height: Int, fps: Float, title: String) {
        compose.runOnIdle { store.newProject(width, height, fps, title) }
        compose.waitUntil(15000) { store.project.title == title && !store.projectOperationBusy }
    }
    private fun waitResult(): ExportProgress {
        val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        val progress = ExportProgress()
        val deadline = SystemClock.elapsedRealtime() + 300000
        var moved = SystemClock.elapsedRealtime()
        var signature = ""
        do {
            assertTrue(store.engineForStress.exportProgress(buffer)); progress.readFrom(buffer)
            val current = "${progress.framesDone}:${progress.flags}:${progress.message}"
            if (current != signature) { signature = current; moved = SystemClock.elapsedRealtime() }
            if (progress.finished) return progress
            assertTrue("No progress: $signature", SystemClock.elapsedRealtime() - moved < 150000)
            SystemClock.sleep(50)
        } while (SystemClock.elapsedRealtime() < deadline)
        store.engineForStress.cancelExport()
        fail("Export exceeded finite test deadline: $signature")
        return progress
    }
    private fun export(name: String, height: Int, fps: Double, frames: Int): File {
        val output = File(context.filesDir, "keyflow-$name.mp4")
        assertEquals(0, store.engineForStress.startExport(output.absolutePath, height, fps, 0, 8))
        val progress = waitResult()
        File(context.filesDir, "keyflow-$name-progress.txt").writeText(
            "result=${progress.result} frames=${progress.framesDone}/${progress.framesTotal} flags=${progress.flags} ${progress.message}")
        assertEquals(progress.message, 0, progress.result)
        assertEquals(frames, progress.framesDone)
        assertEquals("V2 must be disabled in this comparison build", 0, (progress.flags ushr 8) and 15)
        assertFalse(progress.frameFallback)
        assertTrue(output.length() > 1000)
        val reader = MediaMetadataRetriever()
        try {
            reader.setDataSource(output.absolutePath)
            assertEquals(height.toString(), reader.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT))
            for (time in listOf(0L, ((frames - 1) * 1e6 / fps).toLong())) {
                val bitmap = reader.getFrameAtTime(time, MediaMetadataRetriever.OPTION_CLOSEST)
                assertNotNull("$name missing decoded frame at $time", bitmap)
                bitmap?.recycle()
            }
        } finally { reader.release() }
        return output
    }
    private fun assertVisibleText(output: File) {
        val reader=MediaMetadataRetriever()
        try {
            reader.setDataSource(output.absolutePath)
            val frame=reader.getFrameAtTime(0,MediaMetadataRetriever.OPTION_CLOSEST)!!
            val pixels=IntArray(frame.width*frame.height); frame.getPixels(pixels,0,frame.width,0,0,frame.width,frame.height)
            assertTrue("Plain text must be visibly rendered",pixels.count { (it and 0xff)>160 && ((it ushr 8) and 0xff)>160 && ((it ushr 16) and 0xff)>160 }>20)
            frame.recycle()
        } finally { reader.release() }
    }
    @Test fun plainTextExportsAt1080p() {
        initialize(); project(1920, 1080, 30f, "Aurea plain text")
        compose.runOnIdle {
            assertTrue(store.engineForStress.addText("AUREA EXPORT") > 0)
            store.setCompositionDuration(60)
        }
        compose.waitUntil(15000) { store.project.durationFrames == 60 }
        assertVisibleText(export("simple-text",1080,30.0,60))
    }
    @Test fun plainTextExportsAt360p720p1080pInSameEngine() {
        initialize(); project(1920, 1080, 30f, "Aurea resolutions and reuse")
        compose.runOnIdle {
            assertTrue(store.engineForStress.addText("AUREA EXPORT") > 0)
            store.setCompositionDuration(30)
        }
        compose.waitUntil(15000) { store.project.durationFrames == 30 }
        for ((index,height) in listOf(360, 720, 1080, 360).withIndex()) {
            val repeat = if (index == 3) "-repeat" else ""
            assertVisibleText(export("text-$height$repeat",height,30.0,30))
        }
    }
    @Test fun realVideoWithTextExportsAt360pAnd720pInSameEngine() {
        initialize(); project(1920,1080,30f,"Aurea video and text")
        val source=File(context.filesDir,"aurea-owned-motion-fixture.mp4")
        InstrumentationRegistry.getInstrumentation().context.assets.open("motion-fixture.mp4").use { input ->
            source.outputStream().use { input.copyTo(it) }
        }
        val audio=File(context.filesDir,"aurea-owned-two-second-tone.wav")
        val samples=2*48000
        val wav=ByteBuffer.allocate(44+samples*4).order(ByteOrder.LITTLE_ENDIAN).apply {
            put("RIFF".toByteArray()); putInt(capacity()-8); put("WAVEfmt ".toByteArray())
            putInt(16); putShort(1); putShort(2); putInt(48000); putInt(192000); putShort(4); putShort(16)
            put("data".toByteArray()); putInt(samples*4)
            repeat(samples) { n ->
                val sample=(sin(2*PI*440*n/48000)*12000).toInt().toShort()
                putShort(sample); putShort(sample)
            }
        }
        audio.writeBytes(wav.array())
        compose.runOnIdle {
            assertTrue(store.engineForStress.importVideo(source.absolutePath,"Owned motion fixture") > 0)
            assertTrue(store.engineForStress.addText("AUREA VIDEO") > 0)
            assertTrue(store.engineForStress.importAudio(audio.absolutePath,"Owned two second tone") > 0)
            store.setCompositionDuration(60)
        }
        compose.waitUntil(15000) { store.project.durationFrames == 60 }
        for (height in listOf(360,720)) {
            val output=export("video-text-$height",height,30.0,60)
            val reader=MediaMetadataRetriever()
            try {
                reader.setDataSource(output.absolutePath)
                assertEquals("AAC audio track must be present","yes",reader.extractMetadata(MediaMetadataRetriever.METADATA_KEY_HAS_AUDIO))
                val bitmap=checkNotNull(reader.getFrameAtTime(1_000_000L,MediaMetadataRetriever.OPTION_CLOSEST))
                val colors=HashSet<Int>()
                for(y in 0 until bitmap.height step 8) for(x in 0 until bitmap.width step 8) colors.add(bitmap.getPixel(x,y))
                assertTrue("Imported video must retain decoded image detail",colors.size>20)
                bitmap.recycle()
            } finally { reader.release() }
        }
    }
    @Test fun animatedTextPresetAndMotionBlurExportAt1080p() {
        initialize(); project(1920, 1080, 30f, "Aurea text blur")
        compose.runOnIdle {
            val engine = store.engineForStress
            val text = engine.addText("AUREA MOTION BLUR")
            assertTrue(text > 0)
            assertTrue(engine.applyTextPreset(text, 11))
            assertTrue(engine.setMotionBlur(text, true))
            engine.setCompositionMotionBlur(true)
            store.setCompositionDuration(60)
        }
        compose.waitUntil(15000) { store.project.durationFrames == 60 }
        export("animated-text-1080", 1080, 30.0, 60)
    }
}
