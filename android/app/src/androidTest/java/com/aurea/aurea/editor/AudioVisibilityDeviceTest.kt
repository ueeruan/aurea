package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
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

/** The measured clock belongs to the real AAudio output, not just the timeline. */
class AudioVisibilityDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun hidingDifferentLayerKindsKeepsTheSpeakerClockAtTheCurrentAudioPosition() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
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
        compose.runOnIdle { store.newProject(320, 180, 30f, "Audio visibility regression") }
        compose.waitUntil(10000) { store.project.title == "Audio visibility regression" }
        val engine = store.engineForStress
        val audio = File(context.filesDir, "visibility-audio.wav")
        val frames = 48000 * 30
        val wav = ByteBuffer.allocate(44 + frames * 4).order(ByteOrder.LITTLE_ENDIAN).apply {
            put("RIFF".toByteArray()); putInt(capacity() - 8); put("WAVEfmt ".toByteArray())
            putInt(16); putShort(1); putShort(2); putInt(48000); putInt(192000); putShort(4); putShort(16)
            put("data".toByteArray()); putInt(frames * 4)
            repeat(frames) { n ->
                val sample = (sin(2 * PI * (220.0 + n / 48000.0 * 20) * n / 48000) * 6000).toInt().toShort()
                putShort(sample); putShort(sample)
            }
        }.array()
        audio.writeBytes(wav)
        compose.runOnIdle {
            assertTrue(engine.importAudio(audio.absolutePath, "Generated audio") > 0)
            store.addShape(1); store.addText(); store.dismissTextContentEditor(); store.addNull(true)
            store.setCompositionDuration(900); store.clearSelection(); store.seek(240)
            // Perf snapshots are published only while the HUD is enabled.
            if (!store.hudVisible) store.toggleHud()
        }
        compose.waitUntil(10000) { store.layers.size == 4 && store.playhead == 240 }
        val layers = store.layers.toList()
        compose.mainClock.autoAdvance = false
        instrumentation.runOnMainSync { store.play() }
        try {
            compose.waitUntil(15000) { store.playing && store.perf.audioOutputOpen && engine.audioPositionNs() > 8_100_000_000L }
        } catch (error: Throwable) {
            throw AssertionError("Audio start: playing=${store.playing} frame=${store.playhead} output=${store.perf.audioOutputOpen} audioNs=${engine.audioPositionNs()}", error)
        }
        val report = StringBuilder()
        for (layer in layers) for (visible in listOf(false, true)) {
            val before = engine.audioPositionNs()
            assertTrue("Actual audio output must already be after eight seconds", before >= 8_000_000_000L)
            instrumentation.runOnMainSync { store.setLayerVisible(layer.id, visible) }
            compose.waitUntil(5000) { store.layers.firstOrNull { it.id == layer.id }?.visible == visible }
            val after = engine.audioPositionNs()
            assertTrue("Layer kind=${layer.kind} visible=$visible restarted speaker clock: $before -> $after", after >= before - 33_000_000L)
            compose.waitUntil(3000) { engine.audioPositionNs() > before + 100_000_000L }
            assertTrue("Visibility paused playback", store.playing)
            report.append("kind=${layer.kind} visible=$visible audioNs=$before->$after frame=${store.playhead}\n")
        }
        instrumentation.runOnMainSync { store.pause() }
        compose.waitUntil(5000) { !store.playing }
        compose.mainClock.autoAdvance = true
        File(context.getExternalFilesDir(null), "audio-visibility-result.txt").writeText(report.toString())
    }
}
