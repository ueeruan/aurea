package com.aurea.aurea.editor

import android.app.Application
import android.media.MediaMetadataRetriever
import android.os.SystemClock
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.editor.panels.effectTypeId
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
class ExportV2DeviceTest {
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
        val output = File(context.filesDir, "v2-$name.mp4")
        assertEquals(0, store.engineForStress.startExport(output.absolutePath, height, fps, 0, 8))
        val progress = waitResult()
        File(context.filesDir, "v2-$name-progress.txt").writeText(
            "result=${progress.result} frames=${progress.framesDone}/${progress.framesTotal} flags=${progress.flags} ${progress.message}")
        assertEquals(progress.message, 0, progress.result)
        assertEquals(frames, progress.framesDone)
        assertEquals("V2 must finish independent validation", 6, (progress.flags ushr 8) and 15)
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
    @Test fun simple1080p30And60AndThirtySecondsWithAudio() {
        initialize()
        for (fps in listOf(30, 60)) {
            project(1920, 1080, fps.toFloat(), "V2 $fps fps")
            compose.runOnIdle { store.addShape(1); store.setCompositionDuration(fps) }
            compose.waitUntil(15000) { store.layers.size == 1 && store.project.durationFrames == fps }
            export("1080-$fps", 1080, fps.toDouble(), fps)
        }
        project(320, 180, 30f, "V2 thirty seconds audio")
        val tone = File(context.filesDir, "v2-30s-source.wav")
        val samples = 30 * 48000
        val wav = ByteBuffer.allocate(44 + samples * 4).order(ByteOrder.LITTLE_ENDIAN).apply {
            put("RIFF".toByteArray()); putInt(capacity() - 8); put("WAVEfmt ".toByteArray())
            putInt(16); putShort(1); putShort(2); putInt(48000); putInt(192000); putShort(4); putShort(16)
            put("data".toByteArray()); putInt(samples * 4)
            repeat(samples) { n -> val sample = (sin(2 * PI * (440 + n / 48000) * n / 48000) * 12000).toInt().toShort(); putShort(sample); putShort(sample) }
        }
        tone.writeBytes(wav.array())
        compose.runOnIdle {
            store.addShape(1)
            assertTrue(store.engineForStress.importAudio(tone.absolutePath, "Thirty seconds generated PCM") > 0)
            store.setCompositionDuration(900)
        }
        compose.waitUntil(15000) { store.project.durationFrames == 900 }
        val output = export("30s-audio", 180, 30.0, 900)
        val reader = MediaMetadataRetriever()
        try { reader.setDataSource(output.absolutePath); assertEquals("yes", reader.extractMetadata(MediaMetadataRetriever.METADATA_KEY_HAS_AUDIO)) }
        finally { reader.release() }
    }
    @Test fun twentyFour3DObjectsCameraParticlesEffectsAndMotionBlur() {
        initialize(); project(960, 540, 30f, "V2 full 3D")
        compose.runOnIdle {
            val engine = store.engineForStress
            val models = (0 until 24).map { engine.addShape3d(it % 3, "Mesh $it").also { id -> assertTrue(id > 0) } }
            val camera = engine.addCamera(); assertTrue(camera > 0)
            val particles = engine.addParticles(20); assertTrue(particles > 0)
            val text = engine.addText("AUREA V2"); assertTrue(text > 0)
            engine.beginCommandBatch(); val commands = CommandBatch(engine)
            models.forEachIndexed { n, id ->
                commands.setLayerTimeRange(id, 0, 30)
                commands.setScale(id, .25f, .25f, .25f)
                commands.setPosition(id, 100f + n % 6 * 145f, 75f + n / 6 * 125f, (n % 3 * 25).toFloat())
                commands.insertKeyframe(id, TrackProperty.ROTATION_Y, -1, 0, 0, 0f)
                commands.insertKeyframe(id, TrackProperty.ROTATION_Y, -1, 0, 29, 35f)
            }
            for (id in listOf(camera, particles, text)) commands.setLayerTimeRange(id, 0, 30)
            commands.addEffect(text, effectTypeId("aurea.blur.gaussian"))
            commands.addEffect(text, effectTypeId("aurea.light.glow"))
            assertTrue(engine.submitCommands() > 0)
            for (id in models) assertTrue(engine.setMotionBlur(id, true))
            engine.setCompositionMotionBlur(true)
            store.setCompositionDuration(30)
        }
        compose.waitUntil(15000) { store.project.durationFrames == 30 }
        export("3d-24-camera-particles-blur", 540, 30.0, 30)
    }
    @Test fun cancelPreservesPreviousOutputAndNextExportSucceeds() {
        initialize(); project(1920, 1080, 30f, "V2 cancel")
        compose.runOnIdle { store.addShape(1); store.setCompositionDuration(1800) }
        compose.waitUntil(15000) { store.project.durationFrames == 1800 }
        val previous = File(context.filesDir, "v2-cancel-protected.mp4")
        val original = "previous owned output".toByteArray(); previous.writeBytes(original)
        assertEquals(0, store.engineForStress.startExport(previous.absolutePath, 1080, 30.0, 0, 8))
        SystemClock.sleep(500)
        assertEquals(0, store.engineForStress.cancelExport())
        val result = waitResult(); assertNotEquals(0, result.result)
        assertArrayEquals(original, previous.readBytes())
        compose.runOnIdle { store.setCompositionDuration(30) }
        compose.waitUntil(15000) { store.project.durationFrames == 30 }
        export("after-cancel", 1080, 30.0, 30)
    }
    @Test fun prepareInterruptedExport() {
        initialize(); project(1920, 1080, 30f, "V2 owned crash fixture")
        compose.runOnIdle { store.addShape(1); store.setCompositionDuration(600) }
        compose.waitUntil(15000) { store.project.durationFrames == 600 }
        val output = File(context.filesDir, "v2-interrupted.mp4")
        assertEquals(0, store.engineForStress.startExport(output.absolutePath, 1080, 30.0, 0, 8))
        val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        val progress = ExportProgress()
        val deadline = SystemClock.elapsedRealtime() + 30000
        do {
            assertTrue(store.engineForStress.exportProgress(buffer)); progress.readFrom(buffer)
            if (progress.framesDone >= 5 && !progress.finished) {
                assertTrue(File(output.absolutePath + ".aurea-export").isFile)
                File(context.filesDir, "v2-kill-ready.txt").writeText("ready=${progress.framesDone}")
                SystemClock.sleep(60000) // host intentionally force-stops this owned test process
                fail("Host must interrupt the controlled export")
            }
            SystemClock.sleep(20)
        } while (SystemClock.elapsedRealtime() < deadline)
        fail("No active export reached crash checkpoint")
    }
    @Test fun resumeInterruptedExport() {
        initialize()
        val original = File(context.filesDir, "v2-interrupted.mp4")
        val output = File(context.filesDir, "v2-recovered.mp4")
        assertTrue(File(original.absolutePath + ".aurea-export").isFile)
        assertEquals(0, store.engineForStress.restartExport(original.absolutePath + ".aurea-export", output.absolutePath))
        val progress = waitResult()
        assertEquals(progress.message, 0, progress.result); assertEquals(600, progress.framesDone)
        assertEquals(6, (progress.flags ushr 8) and 15)
        assertTrue(output.length() > 1000)
        val reader = MediaMetadataRetriever()
        try {
            reader.setDataSource(output.absolutePath)
            val end = reader.getFrameAtTime(19_966_667, MediaMetadataRetriever.OPTION_CLOSEST)
            assertNotNull(end); end?.recycle()
        } finally { reader.release() }
    }
}
