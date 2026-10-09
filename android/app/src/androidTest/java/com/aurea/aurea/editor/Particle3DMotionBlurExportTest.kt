package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Color
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMetadataRetriever
import android.os.Debug
import android.os.Process
import android.os.SystemClock
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.engine.MotionBlurSettings
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Production GPU and encoders; opt-in and isolated from the user's projects. */
class Particle3DMotionBlurExportTest {
    @get:Rule val compose = createComposeRule()

    @Test fun eightAnimatedObjectsAndParticularExportWithForcedMotionBlur() {
        org.junit.Assume.assumeTrue(InstrumentationRegistry.getArguments().getString("aureaStress") == "true")
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName == "com.aurea.aurea.uitest")
        val folder = File(context.filesDir, "particle-3d-motionblur-export").apply { mkdirs() }
        val report = File(folder, "progress.txt")
        report.writeText("Native ${if (Process.is64Bit()) 64 else 32}-bit process; forced shutter=360; samples=8 adaptive=16; quality=High\n")
        fun note(message: String) {
            val memory = Debug.MemoryInfo().also { Debug.getMemoryInfo(it) }
            report.appendText("$message pssKB=${memory.totalPss}\n")
        }
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(1920, 1080, 30f, "Particles + eight 3D + forced blur") }
        compose.waitUntil(15000) { store.project.title == "Particles + eight 3D + forced blur" && !store.projectOperationBusy }
        val engine = store.engineForStress
        compose.runOnIdle {
            for (index in 0 until 8) {
                val objectId = engine.addShape3d(index % 3, "Animated 3D $index")
                assertTrue(objectId > 0)
                engine.beginCommandBatch()
                val commands = CommandBatch(engine)
                commands.setLayerTimeRange(objectId, 0, 24)
                commands.setScale(objectId, .55f, .55f, .55f)
                commands.setPosition(objectId, 280f + index % 4 * 420f, 280f + index / 4 * 480f, 0f)
                commands.insertKeyframe(objectId, TrackProperty.POSITION_X, -1, 0, 0, 280f + index % 4 * 420f)
                commands.insertKeyframe(objectId, TrackProperty.POSITION_X, -1, 0, 23, 420f + index % 4 * 420f)
                commands.insertKeyframe(objectId, TrackProperty.ROTATION_Z, -1, 0, 0, 0f)
                commands.insertKeyframe(objectId, TrackProperty.ROTATION_Z, -1, 0, 23, 160f)
                assertTrue(engine.submitCommands() > 0)
                assertTrue(engine.setMotionBlur(objectId, true))
            }
            val particles = engine.addParticles(0)
            assertTrue(particles > 0)
            // Native Particular controls, including its own velocity blur.
            for ((parameter, value) in listOf(0 to 600f, 1 to 1.5f, 9 to 350f,
                22 to 2f, 24 to 12f, 38 to 1f, 39 to 360f)) {
                assertTrue(engine.setParticleParam(particles, parameter, value))
            }
            engine.beginCommandBatch()
            val commands = CommandBatch(engine)
            commands.setLayerTimeRange(particles, 0, 24)
            commands.setPosition(particles, 960f, 540f, 0f)
            assertTrue(engine.submitCommands() > 0)
            assertTrue(engine.setMotionBlur(particles, true))
            store.setCompositionDuration(24)
            assertTrue(engine.setMotionBlurSettings(MotionBlurSettings(true, 360f, -180f, 8, 16, 4)))
            store.clearSelection()
        }
        compose.waitUntil(10000) { store.project.durationFrames == 24 }
        assertTrue(engine.queryMotionBlurSettings()!!.enabled)
        val capabilities = checkNotNull(engine.deviceReport())
        note("READY objects=8 Particular=600/sec; encoderLimit=${capabilities.maxExportWidth}x${capabilities.maxExportHeight}")
        for ((height, fps, codec) in listOf(Triple(720, 30.0, 0), Triple(1080, 30.0, 0),
            Triple(1080, 60.0, 0), Triple(1080, 30.0, 1))) {
            val output = File(folder, "stress-${height}p-${fps.toInt()}fps-${if (codec == 0) "avc" else "hevc"}.mp4")
            assertEquals("$height/$fps/$codec must configure at High quality", 0,
                engine.startExport(output.absolutePath, height, fps, codec, 0, quality = 2))
            val progress = ExportProgress()
            val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
            var lastFrame = -1
            var lastProgress = SystemClock.elapsedRealtime()
            val deadline = lastProgress + 900000
            try {
                do {
                    assertTrue(engine.exportProgress(buffer)); progress.readFrom(buffer)
                    if (progress.framesDone != lastFrame) {
                        lastFrame = progress.framesDone; lastProgress = SystemClock.elapsedRealtime()
                        if (lastFrame % 4 == 0) note("EXPORT $height/$fps/$codec frame=$lastFrame/${progress.framesTotal}")
                    }
                    if (progress.finished) break
                    assertTrue("Export GPU/encoder stalled at frame $lastFrame", SystemClock.elapsedRealtime() - lastProgress < 90000)
                    SystemClock.sleep(100)
                } while (SystemClock.elapsedRealtime() < deadline)
                assertTrue("Export deadline exceeded", progress.finished)
                assertEquals(progress.message, 0, progress.result)
                assertFalse("Full quality must not use approximate frame fallback", progress.frameFallback)
                val expectedFrames = (24 * fps / 30).toInt()
                assertEquals(expectedFrames, progress.framesDone)
                val extractor = MediaExtractor()
                try {
                    extractor.setDataSource(output.absolutePath)
                    val track = (0 until extractor.trackCount).first { extractor.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("video/") == true }
                    val format = extractor.getTrackFormat(track)
                    assertEquals(height, format.getInteger(MediaFormat.KEY_HEIGHT))
                    assertEquals(height * 16 / 9, format.getInteger(MediaFormat.KEY_WIDTH))
                    assertEquals(if (codec == 0) "video/avc" else "video/hevc", format.getString(MediaFormat.KEY_MIME))
                    extractor.selectTrack(track)
                    var frames = 0; var previousTime = -1L
                    while (extractor.sampleTime >= 0) {
                        assertTrue("Encoded timestamps must advance", extractor.sampleTime > previousTime)
                        previousTime = extractor.sampleTime; frames++
                        if (!extractor.advance()) break
                    }
                    assertEquals(expectedFrames, frames)
                    assertEquals((expectedFrames - 1) * 1000000.0 / fps, previousTime.toDouble(), 2.0)
                } finally { extractor.release() }
                val reader = MediaMetadataRetriever()
                try {
                    reader.setDataSource(output.absolutePath)
                    for (time in listOf(0L, 400000L, 766000L)) {
                        val bitmap = checkNotNull(reader.getScaledFrameAtTime(time, MediaMetadataRetriever.OPTION_CLOSEST, 320, 180))
                        try {
                            var visible = 0
                            for (y in 0 until bitmap.height step 2) for (x in 0 until bitmap.width step 2) {
                                val pixel = bitmap.getPixel(x, y)
                                if (maxOf(Color.red(pixel), Color.green(pixel), Color.blue(pixel)) > 35) visible++
                            }
                            assertTrue("Encoded 3D/particles must be visible at $time", visible > 100)
                        } finally { bitmap.recycle() }
                    }
                } finally { reader.release() }
                note("PASS $height/$fps/$codec frames=$expectedFrames bytes=${output.length()} fallback=false")
            } finally { if (!progress.finished) engine.cancelExport() }
        }
        note("PASS ALL EXPORTS WITH FORCED BLUR")
    }
}
