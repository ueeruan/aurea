package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.PI
import kotlin.math.sin

/** Real dock taps must edit native mute/retime state, history and saved project. */
class AudioDockControlsDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun muteAndSpeedShortcutsPreserveVolumeUndoAndReopen() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(320, 180, 30f, "Audio dock controls") }
        compose.waitUntil(15000) { store.project.title == "Audio dock controls" && !store.projectOperationBusy }
        val samples = 48000 * 5
        val fixture = File(context.filesDir, "audio-dock-controls.wav")
        fixture.writeBytes(ByteBuffer.allocate(44 + samples * 2).order(ByteOrder.LITTLE_ENDIAN).apply {
            put("RIFF".toByteArray()); putInt(capacity() - 8); put("WAVEfmt ".toByteArray())
            putInt(16); putShort(1); putShort(1); putInt(48000); putInt(96000); putShort(2); putShort(16)
            put("data".toByteArray()); putInt(samples * 2)
            repeat(samples) { putShort((sin(2 * PI * 220 * it / 48000) * 5000).toInt().toShort()) }
        }.array())
        var layer = 0L
        compose.runOnIdle {
            layer = store.engineForStress.importAudio(fixture.absolutePath, "Generated audio")
            assertTrue(layer > 0)
        }
        compose.waitUntil(10000) { store.layers.any { it.id == layer } }
        compose.runOnIdle { store.select(layer) }
        compose.waitUntil(5000) { store.detail?.id == layer && store.detail?.hasAudio == true }
        compose.runOnIdle { store.setAudioVolume(.65f) }
        compose.waitUntil(5000) { store.detail?.audioVolume == .65f }
        val originalDuration = store.layers.single { it.id == layer }.durationFrames
        val nativeRows = ByteBuffer.allocateDirect(PodLayout.LAYER_ROW_BYTES * 16).order(ByteOrder.nativeOrder())
        val nativeNames = ByteBuffer.allocateDirect(4096)
        fun nativeDuration(): Int? {
            val count = store.engineForStress.queryLayers(nativeRows, 16, nativeNames)
            val index = (0 until count).firstOrNull {
                nativeRows.getLong(it * PodLayout.LAYER_ROW_BYTES + PodLayout.LAYER_OFF_ID) == layer
            } ?: return null
            val offset = index * PodLayout.LAYER_ROW_BYTES
            return nativeRows.getInt(offset + PodLayout.LAYER_OFF_END) - nativeRows.getInt(offset + PodLayout.LAYER_OFF_START)
        }
        assertEquals(originalDuration, nativeDuration())

        compose.onNodeWithTag("dock.tool.Mute").assertIsDisplayed().performClick()
        compose.waitUntil(5000) { store.detail?.audioMuted == true }
        assertEquals(.65f, store.detail!!.audioVolume, 0f)
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.detail?.audioMuted == false }
        assertEquals(.65f, store.detail!!.audioVolume, 0f)
        compose.runOnIdle { store.redo() }
        compose.waitUntil(5000) { store.detail?.audioMuted == true }

        compose.onNodeWithTag("dock.tool.Speed").assertIsDisplayed().performClick()
        compose.onNodeWithText("2x").performScrollTo().performClick()
        compose.waitUntil(5000) { nativeDuration() == originalDuration / 2 }
        assertEquals(originalDuration / 2, nativeDuration())
        compose.waitUntil(5000) {
            store.detail?.speed == 2f && store.layers.single { it.id == layer }.durationFrames == originalDuration / 2
        }
        assertEquals(originalDuration / 2, store.layers.single { it.id == layer }.durationFrames)
        assertTrue(store.detail!!.audioMuted)
        assertEquals(.65f, store.detail!!.audioVolume, 0f)
        compose.runOnIdle { store.undo() }
        // Rows and inspector are separate native queries; a command can land
        // between them. First prove native undo, then require coherent UI state.
        compose.waitUntil(5000) { nativeDuration() == originalDuration }
        assertEquals(originalDuration, nativeDuration())
        compose.waitUntil(5000) {
            store.detail?.speed == 1f && store.layers.single { it.id == layer }.durationFrames == originalDuration
        }
        assertEquals(originalDuration, store.layers.single { it.id == layer }.durationFrames)
        compose.runOnIdle { store.redo() }
        compose.waitUntil(5000) {
            nativeDuration() == originalDuration / 2 && store.detail?.speed == 2f &&
                store.layers.single { it.id == layer }.durationFrames == originalDuration / 2
        }

        val path = store.project.path!!
        var saved = false
        compose.runOnIdle { store.saveProject { saved = true } }
        compose.waitUntil(15000) { saved && !store.projectOperationBusy }
        compose.runOnIdle { store.setAudioMuted(false); store.setLayerSpeed(.5f) }
        compose.waitUntil(5000) {
            store.detail?.audioMuted == false && store.detail?.speed == .5f &&
                nativeDuration() == originalDuration * 2 && store.layers.single { it.id == layer }.durationFrames == originalDuration * 2
        }
        compose.runOnIdle { store.openProject(path) }
        compose.waitUntil(15000) { !store.projectOperationBusy && store.layers.any { it.id == layer } }
        compose.runOnIdle { store.select(layer) }
        compose.waitUntil(5000) {
            store.detail?.id == layer && store.detail?.audioMuted == true && store.detail?.speed == 2f &&
                nativeDuration() == originalDuration / 2 && store.layers.single { it.id == layer }.durationFrames == originalDuration / 2
        }
        assertEquals(.65f, store.detail!!.audioVolume, 0f)
        assertEquals(originalDuration / 2, store.layers.single { it.id == layer }.durationFrames)
    }
}
