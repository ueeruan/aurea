package com.aurea.aurea.editor

import android.app.Application
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.os.SystemClock
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.PI
import kotlin.math.sin
import kotlin.math.sqrt
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

class AudioExportFailureDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun unreadableAudibleSourceFailsAndRestoredSourceExportsRealAac() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var created = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            created = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { created && store.engineReady }
        compose.runOnIdle { store.newProject(320, 180, 30f, "Audio failure recovery") }
        compose.waitUntil(10000) { store.project.title == "Audio failure recovery" && !store.projectOperationBusy }
        compose.runOnIdle { store.addShape(1); store.setCompositionDuration(30) }
        compose.waitUntil(10000) { store.layers.size == 1 && store.project.durationFrames == 30 }
        val directory = File(context.filesDir, "audio-export-failure").apply { mkdirs() }
        val tone = File(directory, "required.wav")
        val wav = ByteBuffer.allocate(44 + 48000 * 4).order(ByteOrder.LITTLE_ENDIAN).apply {
            put("RIFF".toByteArray()); putInt(capacity() - 8); put("WAVEfmt ".toByteArray())
            putInt(16); putShort(1); putShort(2); putInt(48000); putInt(192000); putShort(4); putShort(16)
            put("data".toByteArray()); putInt(48000 * 4)
            repeat(48000) { n -> val sample = (sin(2 * PI * 440 * n / 48000) * 12000).toInt().toShort(); putShort(sample); putShort(sample) }
        }.array()
        tone.writeBytes(wav)
        val engine = store.engineForStress
        assertTrue(engine.importAudio(tone.absolutePath, "Required generated audio") >= 0)
        // This is our generated fixture only. Its source metadata is valid at
        // import, but the export must open a fresh decoder and detect corruption.
        tone.writeText("invalid generated WAV")
        val progress = ExportProgress()
        val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        fun export(file: File) {
            assertEquals(0, engine.startExport(file.absolutePath, 180, 30.0, 0, 2))
            val deadline = SystemClock.elapsedRealtime() + 60000
            do {
                assertTrue(engine.exportProgress(buffer)); progress.readFrom(buffer)
                if (progress.finished) return
                SystemClock.sleep(50)
            } while (SystemClock.elapsedRealtime() < deadline)
            engine.cancelExport()
            fail("Audio export did not reach a terminal result")
        }
        val broken = File(directory, "broken.mp4")
        try {
            export(broken)
            assertEquals(ExportProgress.FAILURE_MEDIA, progress.failure)
            assertNotEquals(0, progress.result)
            assertFalse("Partial failed MP4 survived", broken.exists())
            tone.writeBytes(wav)
            val recovered = File(directory, "recovered.mp4")
            export(recovered)
            assertEquals(progress.message, 0, progress.result)
            assertEquals(30, progress.framesDone)
            val rms = decodedAudioRms(recovered)
            File(directory, "result.txt").writeText("frames=${progress.framesDone} audioRms=$rms missingMediaFailure=5\n")
            assertTrue("Restored AAC must contain the generated tone, RMS=$rms", rms > .05)
        } finally { engine.cancelExport() }
    }

    private fun decodedAudioRms(file: File): Double {
        val extractor = MediaExtractor()
        var decoder: MediaCodec? = null
        try {
            extractor.setDataSource(file.absolutePath)
            val track = (0 until extractor.trackCount).first { extractor.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true }
            val format = extractor.getTrackFormat(track)
            assertEquals("audio/mp4a-latm", format.getString(MediaFormat.KEY_MIME))
            extractor.selectTrack(track)
            val codec = MediaCodec.createDecoderByType("audio/mp4a-latm").also { decoder = it }
            codec.configure(format, null, null, 0); codec.start()
            var inputEnded = false; var outputEnded = false; var floating = false
            var squares = 0.0; var samples = 0L
            val info = MediaCodec.BufferInfo()
            val deadline = SystemClock.elapsedRealtime() + 15000
            while (!outputEnded && SystemClock.elapsedRealtime() < deadline) {
                if (!inputEnded) {
                    val index = codec.dequeueInputBuffer(1000)
                    if (index >= 0) {
                        val input = checkNotNull(codec.getInputBuffer(index)); input.clear()
                        val size = extractor.readSampleData(input, 0)
                        if (size < 0) { codec.queueInputBuffer(index, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM); inputEnded = true }
                        else { codec.queueInputBuffer(index, 0, size, extractor.sampleTime, 0); extractor.advance() }
                    }
                }
                when (val index = codec.dequeueOutputBuffer(info, 1000)) {
                    MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> floating = codec.outputFormat.getInteger(MediaFormat.KEY_PCM_ENCODING, 2) == 4
                    else -> if (index >= 0) {
                        val output = checkNotNull(codec.getOutputBuffer(index)).order(ByteOrder.LITTLE_ENDIAN)
                        output.position(info.offset); output.limit(info.offset + info.size)
                        while (output.remaining() >= if (floating) 4 else 2) {
                            val value = if (floating) output.float.toDouble() else output.short / 32768.0
                            squares += value * value; samples++
                        }
                        outputEnded = info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0
                        codec.releaseOutputBuffer(index, false)
                    }
                }
            }
            assertTrue("AAC decoder did not complete", outputEnded)
            assertTrue("AAC contains too few PCM samples", samples >= 90000)
            return sqrt(squares / samples)
        } finally { decoder?.let { runCatching { it.stop() }; it.release() }; extractor.release() }
    }
}
