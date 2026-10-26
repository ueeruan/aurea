package com.aurea.aurea.hyper

import android.app.Application
import android.graphics.Bitmap
import android.media.MediaMetadataRetriever
import android.os.SystemClock
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.EditorScreen
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * AureaHyper no aparelho (0.3.2): o job de verdade — start_hyper_job, o
 * caminho Turbo (shaders na GPU do aparelho) e o Lite (rede ncnn) — sobre o
 * clipe de teste (480×320, 30 fps, 2 s), com o MP4 conferido pelo
 * decodificador do sistema: tamanho, quadros e duração.
 */
class HyperJobDeviceTest {
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

    private fun fixture(): File = File(context.filesDir, "hyper-fixture.mp4").also { file ->
        InstrumentationRegistry.getInstrumentation().context.assets.open("motion-fixture.mp4").use { input ->
            file.outputStream().use { input.copyTo(it) }
        }
    }

    private fun run(name: String, recipe: HyperRecipe, limitMs: Long = 600000): Pair<File, ExportProgress> {
        val source = fixture()
        val output = File(context.filesDir, "hyper-$name.mp4").also { it.delete() }
        val started = SystemClock.elapsedRealtime()
        assertEquals(0, store.engineForStress.startHyperJob(source.absolutePath, recipe, output.absolutePath))
        val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        val progress = ExportProgress()
        val deadline = started + limitMs
        var logged = 0L
        var moved = SystemClock.elapsedRealtime()
        var signature = ""
        while (SystemClock.elapsedRealtime() < deadline) {
            assertTrue(store.engineForStress.exportProgress(buffer)); progress.readFrom(buffer)
            val current = "${progress.framesDone}:${progress.flags}:${progress.message}"
            if (current != signature) { signature = current; moved = SystemClock.elapsedRealtime() }
            if (progress.finished) break
            if (SystemClock.elapsedRealtime() - logged > 10000) {
                logged = SystemClock.elapsedRealtime()
                android.util.Log.i("HyperJobDeviceTest", "$name: ${progress.framesDone}/${progress.framesTotal} " +
                    "${(logged - started) / 1000} s ${progress.message}")
            }
            assertTrue("No progress: $signature", SystemClock.elapsedRealtime() - moved < 180000)
            SystemClock.sleep(50)
        }
        if (!progress.finished) { store.engineForStress.cancelExport(); fail("$name exceeded the test deadline: $signature") }
        val seconds = (SystemClock.elapsedRealtime() - started) / 1000.0
        android.util.Log.i("HyperJobDeviceTest", "$name: result=${progress.result} frames=${progress.framesDone}/${progress.framesTotal} " +
            "flags=${progress.flags} ${"%.1f".format(seconds)} s ${progress.message}")
        assertEquals(progress.message, 0, progress.result)
        assertTrue("$name output missing", output.isFile && output.length() > 0)
        return output to progress
    }

    private class Movie(val width: Int, val height: Int, val frames: Int, val durationMs: Long, val audio: Boolean, val detail: Int)

    private fun inspect(file: File): Movie {
        val reader = MediaMetadataRetriever()
        try {
            reader.setDataSource(file.absolutePath)
            val width = reader.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)!!.toInt()
            val height = reader.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)!!.toInt()
            val frames = reader.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_FRAME_COUNT)?.toIntOrNull() ?: -1
            val duration = reader.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)!!.toLong()
            val audio = reader.extractMetadata(MediaMetadataRetriever.METADATA_KEY_HAS_AUDIO) == "yes"
            val bitmap: Bitmap = checkNotNull(reader.getFrameAtTime(duration * 500, MediaMetadataRetriever.OPTION_CLOSEST))
            val colors = HashSet<Int>()
            for (y in 0 until bitmap.height step 16) for (x in 0 until bitmap.width step 16) colors.add(bitmap.getPixel(x, y))
            bitmap.recycle()
            return Movie(width, height, frames, duration, audio, colors.size)
        } finally { reader.release() }
    }

    @Test fun turboUpscale2xAnd60FpsDoublesSizeAndFrames() {
        initialize()
        val (file, _) = run("turbo-2x-60", HyperRecipe(upscale = 2, targetFps = 60.0, path = HyperPath.Turbo))
        val movie = inspect(file)
        android.util.Log.i("HyperJobDeviceTest", "turbo: ${movie.width}x${movie.height} frames=${movie.frames} ${movie.durationMs} ms detail=${movie.detail}")
        assertEquals(960, movie.width); assertEquals(640, movie.height)
        assertTrue("60 fps must double the 60 source frames: ${movie.frames}", movie.frames in 118..122)
        assertTrue("duration stays 2 s: ${movie.durationMs}", movie.durationMs in 1900..2100)
        assertTrue("decoded image detail ${movie.detail}", movie.detail > 20)
    }

    @Test fun turboSlowMotion2xDoublesDurationWithoutSound() {
        initialize()
        val (file, _) = run("turbo-slow2", HyperRecipe(slowMotion = 2, path = HyperPath.Turbo))
        val movie = inspect(file)
        android.util.Log.i("HyperJobDeviceTest", "slow: ${movie.width}x${movie.height} frames=${movie.frames} ${movie.durationMs} ms audio=${movie.audio}")
        assertEquals(480, movie.width); assertEquals(320, movie.height)
        assertTrue("2x slow motion doubles the duration: ${movie.durationMs}", movie.durationMs in 3800..4200)
        assertTrue("slow motion has no sound", !movie.audio)
    }

    @Test fun liteNeuralUpscale2xRunsOnTheDevice() {
        initialize()
        val (file, _) = run("lite-2x", HyperRecipe(upscale = 2, path = HyperPath.Lite), limitMs = 1800000)
        val movie = inspect(file)
        android.util.Log.i("HyperJobDeviceTest", "lite: ${movie.width}x${movie.height} frames=${movie.frames} ${movie.durationMs} ms detail=${movie.detail}")
        assertEquals(960, movie.width); assertEquals(640, movie.height)
        assertTrue("frame count kept: ${movie.frames}", movie.frames in 58..62)
        assertTrue("decoded image detail ${movie.detail}", movie.detail > 20)
    }
}
