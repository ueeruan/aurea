package com.aurea.aurea.editor

import android.app.Application
import android.net.Uri
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.PI
import kotlin.math.exp
import kotlin.math.sin

/** Real decoder/project integration plus UI cuts, markers, search, effects and
 * linking. Fixture import and numeric animation setup use the normal store API;
 * this is not a claim that every edit was performed through touch controls. */
class ManualEditingWorkflowTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext

    private fun seek(frame: Int) {
        compose.runOnIdle { store.seek(frame) }
        compose.waitUntil(5000) { store.playhead == frame }
    }
    private fun select(id: Long) {
        compose.runOnIdle { store.select(id) }
        compose.waitUntil(5000) { store.primary == id && store.detail?.id == id }
    }
    private fun command(title: String) {
        compose.onAllNodesWithContentDescription("Buscar ferramentas").filter(hasClickAction())[0].performClick()
        compose.onAllNodesWithContentDescription("Buscar ferramentas").filter(hasSetTextAction())[0].performTextInput(title)
        compose.onNode(hasText(title, substring = false) and !hasSetTextAction()).performClick()
    }

    @Test fun eightClipProjectEditsPlaysAcrossCutsAndReopensWithoutLosingWork() {
        assertTrue(context.packageName.endsWith(".uitest"))
        val video = File(context.filesDir, "manual-motion.mp4")
        instrumentation.context.assets.open("motion-fixture.mp4").use { input -> video.outputStream().use { input.copyTo(it) } }
        val music = File(context.filesDir, "manual-rhythm.wav")
        val rate = 24000
        val pcm = ByteBuffer.allocate(rate * 16 * 2).order(ByteOrder.LITTLE_ENDIAN)
        repeat(rate * 16) { sample ->
            val t = sample.toDouble() / rate
            val beat = t % .5
            pcm.putShort((sin(2 * PI * (70 + 90 * exp(-beat * 25)) * t) * exp(-beat * 12) * 7000).toInt().toShort())
        }
        val header = ByteBuffer.allocate(44).order(ByteOrder.LITTLE_ENDIAN)
        header.put("RIFF".toByteArray()).putInt(36 + pcm.capacity()).put("WAVEfmt ".toByteArray()).putInt(16)
        header.putShort(1).putShort(1).putInt(rate).putInt(rate * 2).putShort(2).putShort(16)
        header.put("data".toByteArray()).putInt(pcm.capacity())
        music.outputStream().use { it.write(header.array()); it.write(pcm.array()) }
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(480, 320, 30f, "Manual editing acceptance") }
        compose.waitUntil(15000) { store.project.title == "Manual editing acceptance" }
        val clips = mutableListOf<Long>()
        repeat(8) { index ->
            compose.runOnIdle { store.importVideo(Uri.fromFile(video)) }
            compose.waitUntil(30000) { store.busyMessage == null && store.layers.count { it.kind == 1 } == index + 1 }
            compose.runOnIdle {
                assertNull(store.errorMessage)
                val id = store.primary!!; clips += id
                store.renameLayer(id, "Clip ${index + 1}")
                store.setLayerRanges(longArrayOf(id), intArrayOf(index * 60), intArrayOf((index + 1) * 60))
            }
        }
        seek(0)
        compose.runOnIdle { store.importAudio(Uri.fromFile(music)) }
        compose.waitUntil(30000) { store.busyMessage == null && store.layers.any { it.kind == 3 } }
        for (frame in 0 until 480 step 30) {
            seek(frame)
            compose.onNodeWithContentDescription(context.getString(R.string.editor_marcar_ou_desmarcar_este_instante)).performClick()
            compose.runOnIdle { assertEquals(frame, store.playhead) }
        }
        compose.waitUntil(5000) { store.markers.size == 16 }
        select(clips[2]); seek(150)
        compose.onNodeWithContentDescription(context.getString(R.string.editor_dividir_cabecote)).performClick()
        compose.waitUntil(5000) { store.layers.count { it.kind == 1 } == 9 }
        command("Adicionar nulo 2D")
        compose.waitUntil(5000) { store.layers.size == 11 }
        val nullId = store.primary!!
        compose.runOnIdle { store.renameLayer(nullId, "AMV controller") }
        for (clip in clips.take(2)) {
            select(clip)
            compose.onNodeWithContentDescription(context.getString(R.string.editor_vincular_outra_camada)).performClick()
            compose.onNodeWithText("AMV controller").performScrollTo().performClick()
            compose.waitUntil(5000) {
                val row = store.layers.first { it.id == clip }
                store.layers.getOrNull(row.parentIndex)?.id == nullId
            }
        }
        select(nullId); seek(0)
        compose.runOnIdle { store.toggleTransformKeyframe(intArrayOf(0)) }
        seek(90)
        compose.runOnIdle { store.setTransform(0, 300f) }
        select(clips[0]); seek(0)
        for (effect in listOf("Gaussian Blur", "Glow", "RGB Split")) {
            val count = store.effects.size
            command(effect)
            compose.waitUntil(5000) { store.effects.size == count + 1 }
        }
        compose.runOnIdle { store.setLayerMotionBlur(clips[0], true) }
        select(clips[3])
        compose.runOnIdle { store.enableManualTimeRemap() }
        compose.runOnIdle { val point = store.remapInsert(30); assertTrue(point >= 0); store.remapMove(point, 30, 20f) }
        compose.runOnIdle { store.clearSelection() }
        seek(0)
        compose.onNodeWithContentDescription(context.getString(R.string.editor_reproduzir_segure_repetir)).performClick()
        compose.waitUntil(20000) { store.playhead > 190 }
        compose.onNodeWithContentDescription(context.getString(R.string.editor_pausar)).performClick()
        compose.waitUntil(5000) { !store.playing }
        compose.runOnIdle { assertNull(store.errorMessage); assertTrue(store.project.durationFrames >= 480) }
        // Runtime handles are remapped when loading. Validate ordered content and
        // parent indices, not transient slot-map IDs.
        val expectedRanges = store.layers.map { Pair(it.name, Triple(it.startFrame, it.endFrame, it.parentIndex)) }
        val path = store.project.path!!
        val saved = AtomicBoolean(false)
        compose.runOnIdle { store.saveProject { saved.set(true) } }
        compose.waitUntil(10000) { saved.get() }
        compose.runOnIdle { store.newProject(320, 240, 30f, "Reopen checkpoint") }
        compose.waitUntil(10000) { store.layers.isEmpty() && store.project.title == "Reopen checkpoint" }
        compose.runOnIdle { store.openProject(path) }
        compose.waitUntil(15000) { store.layers.size == expectedRanges.size }
        compose.runOnIdle {
            assertNull(store.errorMessage)
            assertEquals(expectedRanges, store.layers.map { Pair(it.name, Triple(it.startFrame, it.endFrame, it.parentIndex)) })
            assertEquals(16, store.markers.size)
        }
        select(store.layers.first { it.name == "Clip 1" }.id); compose.runOnIdle { assertEquals(3, store.effects.size) }
        val reopenedNull = store.layers.first { it.name == "AMV controller" }.id
        select(reopenedNull); compose.runOnIdle { assertEquals(2, store.keyframes[reopenedNull].orEmpty().size) }
    }
}
