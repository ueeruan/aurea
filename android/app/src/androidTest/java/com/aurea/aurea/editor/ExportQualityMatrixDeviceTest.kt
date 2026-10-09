package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Color
import android.media.MediaExtractor
import android.media.MediaFormat
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

/** Real codecs and independently decoded pixels; tiny duration bounds the cost. */
class ExportQualityMatrixDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun highQualityPreservesResolutionFrameRateAndPixelsThroughTheAdvertisedLimit() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        org.junit.Assume.assumeTrue(InstrumentationRegistry.getArguments().getString("aureaExportQuality") == "true")
        val context = instrumentation.targetContext
        check(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(3840, 2160, 60f, "High quality codec matrix") }
        compose.waitUntil(10000) { store.project.title == "High quality codec matrix" }
        compose.runOnIdle { store.addShape(1); store.setCompositionDuration(8); store.clearSelection() }
        compose.waitUntil(10000) { store.layers.size == 1 && store.project.durationFrames == 8 }
        val engine = store.engineForStress
        val directory = File(context.getExternalFilesDir(null), "quality-matrix").apply { mkdirs() }
        val report = File(directory, "result.txt").apply { writeText("Requested quality=High, bitrate=automatic\n") }
        val capabilities = checkNotNull(engine.deviceReport())
        report.appendText("Device export limit=${capabilities.maxExportWidth}x${capabilities.maxExportHeight}\n")
        for ((height, fps, codec) in listOf(Triple(720, 30.0, 0), Triple(1080, 60.0, 0),
            Triple(1440, 60.0, 0), Triple(2160, 60.0, 0), Triple(1080, 30.0, 1))) {
            val file = File(directory, "high-${height}p-${fps.toInt()}fps-${if (codec == 0) "avc" else "hevc"}.mp4")
            val code = engine.startExport(file.absolutePath, height, fps, codec, 0, quality = 2)
            if (capabilities.maxExportHeight > 0 && height > capabilities.maxExportHeight) {
                assertEquals("Unsupported size must fail explicitly, without lowering resolution", 6, code)
                assertFalse("Unsupported export must not publish a smaller video", file.exists())
                report.appendText("LIMIT ${height}p ${fps}fps codec=$codec exceeds advertised device capabilities\n")
                continue
            }
            assertEquals("$height/$fps/$codec failed to configure", 0, code)
            val progress = ExportProgress()
            val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
            val deadline = SystemClock.elapsedRealtime() + 120000
            try {
                do {
                    assertTrue(engine.exportProgress(buffer)); progress.readFrom(buffer)
                    if (progress.finished) break
                    SystemClock.sleep(40)
                } while (SystemClock.elapsedRealtime() < deadline)
                assertTrue("$height/$fps/$codec timed out", progress.finished)
                assertEquals("$height/$fps/$codec: ${progress.message}", 0, progress.result)
                val expectedFrames = if (fps == 30.0) 4 else 8
                assertEquals(expectedFrames, progress.framesDone)
                val extractor = MediaExtractor()
                var samples = 0
                try {
                    extractor.setDataSource(file.absolutePath)
                    val track = (0 until extractor.trackCount).first { extractor.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("video/") == true }
                    val format = extractor.getTrackFormat(track)
                    assertEquals(height, format.getInteger(MediaFormat.KEY_HEIGHT))
                    assertEquals(height * 16 / 9, format.getInteger(MediaFormat.KEY_WIDTH))
                    assertEquals(if (codec == 0) "video/avc" else "video/hevc", format.getString(MediaFormat.KEY_MIME))
                    extractor.selectTrack(track)
                    var previous = -1L
                    while (extractor.sampleTime >= 0) {
                        assertTrue("PTS must advance", extractor.sampleTime > previous)
                        previous = extractor.sampleTime; ++samples
                        if (!extractor.advance()) break
                    }
                    assertEquals(expectedFrames, samples)
                    assertEquals((expectedFrames - 1) * 1000000.0 / fps, previous.toDouble(), 2.0)
                } finally { extractor.release() }
                val reader = MediaMetadataRetriever()
                try {
                    reader.setDataSource(file.absolutePath)
                    val bitmap = checkNotNull(reader.getScaledFrameAtTime(0, MediaMetadataRetriever.OPTION_CLOSEST_SYNC, 320, 180))
                    try { assertTrue("White circle must survive encoding", Color.red(bitmap.getPixel(bitmap.width / 2, bitmap.height / 2)) > 200) }
                    finally { bitmap.recycle() }
                } finally { reader.release() }
                report.appendText("PASS ${height}p ${fps}fps codec=$codec frames=$samples bytes=${file.length()}\n")
            } finally { if (!progress.finished) engine.cancelExport() }
        }
    }
}
