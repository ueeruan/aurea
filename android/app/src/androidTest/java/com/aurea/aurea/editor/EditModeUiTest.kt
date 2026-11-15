package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipe
import androidx.compose.ui.geometry.Offset
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.UI_MODE_COMPOSITION
import com.aurea.aurea.state.UI_MODE_EDIT
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * A interface de Edição (0.2.5): projeto novo em Edição mostra a linha do tempo
 * com a trilha principal e o Dock; "Editar" escolhe o corte do cabeçote; o
 * Dock do corte divide e manda para camada; voltar à Composição troca a tela.
 */
class EditModeUiTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext

    private fun capture(name: String) {
        compose.waitForIdle()
        Thread.sleep(650)
        instrumentation.waitForIdleSync()
        val bitmap = checkNotNull(instrumentation.uiAutomation.takeScreenshot())
        File(context.filesDir, "ui-$name.png").outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        bitmap.recycle()
    }

    /** Uma imagem sólida de 64 × 36 direto no motor (o seletor do sistema não entra no teste). */
    private fun importSolid(name: String, r: Int, g: Int, b: Int): Long {
        val w = 64; val h = 36
        val buf = ByteBuffer.allocateDirect(w * h * 4).order(ByteOrder.nativeOrder())
        repeat(w * h) { buf.put(r.toByte()); buf.put(g.toByte()); buf.put(b.toByte()); buf.put(255.toByte()) }
        buf.rewind()
        return store.engineForStress.importImage(buf, w, h, name, "test://$name")
    }

    @Test fun expandedGroupChildUsesInspectorAndKeepsOuterComposition() {
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(720, 1280, 30.0, "Inline inspector", null, UI_MODE_COMPOSITION) }
        compose.waitUntil(15000) { store.project.title == "Inline inspector" && !store.projectOperationBusy }
        var group = 0L
        compose.runOnIdle {
            val image = importSolid("Child", 120, 180, 90)
            assertTrue(image > 0)
            group = store.engineForStress.precompose(longArrayOf(image))
            assertTrue(group > 0)
        }
        compose.waitUntil(10000) { store.layers.size == 1 && store.layers.first().id == group }
        compose.runOnIdle { store.toggleInlineGroup(group) }
        compose.waitUntil(10000) { store.groupInnerRows[group]?.size == 1 }
        var child = 0L
        compose.runOnIdle {
            child = store.groupInnerRows.getValue(group).first().row.id
            store.selectGroupInner(group, child)
        }
        compose.waitUntil(10000) { store.primary == child && store.detail?.id == child }
        compose.onNodeWithTag("layer.panel").assertIsDisplayed()
        compose.onNodeWithTag("layer.panel.moveTransform").performClick()
        compose.runOnIdle { store.setTransform(0, 123f) }
        compose.waitUntil(10000) { store.detail?.position?.firstOrNull() == 123f }
        compose.runOnIdle {
            assertEquals(0, store.engineForStress.precompDepth())
            assertEquals(group to child, store.groupInnerSelection)
            store.duplicateLayers()
        }
        compose.waitUntil(10000) { store.groupInnerRows[group]?.size == 2 && store.primary != child }
        compose.runOnIdle {
            assertEquals(1, store.layers.size)
            assertEquals(0, store.engineForStress.precompDepth())
        }
        capture("inline-group-inspector")
        compose.runOnIdle {
            store.copyLayersWithStyle()
            store.pasteLayers()
        }
        compose.waitUntil(10000) { store.groupInnerRows[group]?.size == 3 }
        compose.runOnIdle {
            assertEquals(1, store.layers.size)
            assertEquals(0, store.engineForStress.precompDepth())
            store.deleteLayers()
        }
        compose.waitUntil(10000) { store.groupInnerRows[group]?.size == 2 }
        compose.runOnIdle { store.selectGroupInner(group, store.groupInnerRows.getValue(group).first { it.row.id != child }.row.id) }
        compose.runOnIdle { store.deleteLayers() }
        compose.waitUntil(10000) { store.groupInnerRows[group]?.size == 1 }
        compose.runOnIdle {
            assertEquals(group, store.layers.single().id)
            assertEquals(0, store.engineForStress.precompDepth())
        }
    }

    @Test fun timelineScrollingStopsAtBothSequenceEdgesWithPinnedHeaders() {
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(720, 1280, 30.0, "Bounded timeline", null, UI_MODE_EDIT) }
        compose.waitUntil(15000) { store.project.title == "Bounded timeline" && !store.projectOperationBusy }
        var clip = 0L
        compose.runOnIdle {
            clip = importSolid("Timeline source", 150, 90, 70)
            assertTrue(clip > 0)
            store.setCompositionDuration(300)
            val engine = store.engineForStress
            engine.beginCommandBatch()
            CommandBatch(engine).setLayerTimeRange(clip, 0, 300)
            assertTrue(engine.submitCommands() > 0)
            store.clearSelection()
        }
        compose.waitUntil(10000) { store.layers.any { it.id == clip && it.endFrame == 300 } }
        // The direct image fixture bypasses the media browser's track focus.
        // Focus its actual lane before measuring clipped semantics in landscape.
        compose.waitUntil(10000) { store.editTimeline?.tracks?.isNotEmpty() == true }
        var trackLabel = ""
        compose.runOnIdle {
            val track = store.layers.first { it.id == clip }.trackId
            val tracks = checkNotNull(store.editTimeline).tracks
            trackLabel = tracks.first { it.id == track }.label
            tracks.filter { !it.audio && it.target && it.id != track }.forEach { store.setEditTrackFlag(it.id, 3, false) }
            store.setEditTrackFlag(track, 3, true)
        }
        val timeline = compose.onNodeWithTag("edit.timeline").fetchSemanticsNode().boundsInRoot
        val headerTag = "edit.track.$trackLabel.target"
        val header = compose.onNodeWithTag(headerTag).fetchSemanticsNode().boundsInRoot
        val density = context.resources.displayMetrics.density
        val origin = header.right
        val initialClip = compose.onNodeWithTag("edit.clip.$clip").fetchSemanticsNode().boundsInRoot
        val endBoundary = minOf(initialClip.right, timeline.right)
        assertEquals(origin, initialClip.left, 2f)
        repeat(8) {
            compose.onNodeWithTag("edit.timeline").performTouchInput {
                val y = 92f * density
                swipe(Offset(width * .9f, y), Offset(width * .4f, y), 250)
            }
        }
        compose.onNodeWithTag("edit.clip.$clip").assertIsDisplayed()
        assertEquals(endBoundary, compose.onNodeWithTag("edit.clip.$clip").fetchSemanticsNode().boundsInRoot.right, 2f)
        assertEquals(header, compose.onNodeWithTag(headerTag).fetchSemanticsNode().boundsInRoot)
        repeat(8) {
            compose.onNodeWithTag("edit.timeline").performTouchInput {
                val y = 92f * density
                swipe(Offset(width * .4f, y), Offset(width * .9f, y), 250)
            }
        }
        assertEquals(origin, compose.onNodeWithTag("edit.clip.$clip").fetchSemanticsNode().boundsInRoot.left, 2f)
        assertEquals(header, compose.onNodeWithTag(headerTag).fetchSemanticsNode().boundsInRoot)
        capture("edit-timeline-bounded")
    }

    @Test fun editInterfaceDocksSplitsAndSwitchesBack() {
        assertTrue(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(720, 1280, 30.0, "AUREA · Edição", null, UI_MODE_EDIT) }
        compose.waitUntil(15000) { store.project.title == "AUREA · Edição" && !store.projectOperationBusy }
        compose.waitUntil(10000) { store.uiMode == UI_MODE_EDIT }
        compose.onNodeWithTag("edit.timeline").assertIsDisplayed()
        compose.onNodeWithTag("edit.dock").assertIsDisplayed()
        compose.onNodeWithTag("edit.add").assertIsDisplayed()
        compose.runOnIdle { assertTrue(store.editMode) }
        capture("edit-empty")

        // Duas imagens entram em sequência na trilha principal (regra do motor no modo Edição).
        compose.runOnIdle {
            assertTrue(importSolid("A", 220, 60, 60) >= 0)
            assertTrue(importSolid("B", 60, 120, 220) >= 0)
        }
        compose.waitUntil(10000) { store.layers.size == 2 && store.mainTrackAnchor != 0L }
        compose.runOnIdle {
            val a = store.layers.first { it.name == "A" }
            val b = store.layers.first { it.name == "B" }
            assertEquals(a.trackId, b.trackId)
            assertEquals(a.endFrame, b.startFrame)
            store.seek((a.startFrame + a.endFrame) / 2)
        }
        capture("edit-two-clips")

        // "Editar" escolhe o corte no cabeçote e o Dock vira o do corte.
        compose.onNodeWithTag("edit.dock.edit").performClick()
        compose.waitUntil(5000) { store.selection.size == 1 }
        compose.waitUntil(5000) { compose.onAllNodesWithTag("edit.dock.split").fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithTag("edit.dock.back").assertIsDisplayed()
        capture("edit-clip-dock")
        compose.onNodeWithTag("edit.dock.split").performClick()
        compose.waitUntil(5000) { store.layers.size == 3 }

        // "Para camada": o corte escolhido sai da trilha principal.
        compose.waitUntil(5000) { store.selection.size == 1 }
        val moved = store.primary!!
        compose.runOnIdle { store.select(moved) }
        compose.waitUntil(5000) { compose.onAllNodesWithTag("edit.dock.split").fetchSemanticsNodes().isNotEmpty() }
        // O Dock é uma fileira preguiçosa: "Para camada" fica no fim e só existe depois de rolar.
        compose.onNodeWithTag("edit.dock.items").performScrollToNode(hasTestTag("edit.dock.toOverlay"))
        compose.onNodeWithTag("edit.dock.toOverlay").performClick()
        compose.waitUntil(5000) {
            val anchor = store.layers.firstOrNull { it.id == store.mainTrackAnchor }
            val m = store.layers.firstOrNull { it.id == moved }
            anchor != null && m != null && m.trackId != anchor.trackId
        }
        capture("edit-overlay")

        // Voltar à Composição troca a tela inteira; o projeto guarda a escolha.
        compose.runOnIdle { store.setUiMode(UI_MODE_COMPOSITION) }
        compose.waitUntil(5000) { compose.onAllNodesWithTag("edit.timeline").fetchSemanticsNodes().isEmpty() }
        compose.runOnIdle { assertEquals(UI_MODE_COMPOSITION, store.uiMode) }
    }

    /** WAV mono de `seconds` s com um tom de 220 Hz (som de verdade para a onda). */
    private fun tone(seconds: Int): File {
        val samples = 48000 * seconds
        val file = File(context.filesDir, "edit-wave-$seconds.wav")
        file.writeBytes(ByteBuffer.allocate(44 + samples * 2).order(ByteOrder.LITTLE_ENDIAN).apply {
            put("RIFF".toByteArray()); putInt(capacity() - 8); put("WAVEfmt ".toByteArray())
            putInt(16); putShort(1); putShort(1); putInt(48000); putInt(96000); putShort(2); putShort(16)
            put("data".toByteArray()); putInt(samples * 2)
            repeat(samples) { putShort((kotlin.math.sin(2 * Math.PI * 220 * it / 48000) * 9000 * (it % 24000) / 24000).toInt().toShort()) }
        }.array())
        return file
    }

    /** A onda do motor tem algum balde aceso (ela chega aos pedaços, em segundo plano). */
    private fun hasWave(layer: Long, frames: Int): Boolean {
        val out = ByteBuffer.allocateDirect(256)
        val got = store.queryWaveform(layer, 0.0, frames / 64.0, 64, out)
        return got > 0 && (0 until got).any { out.get(it).toInt() != 0 }
    }

    @Test fun editClipsShowTheAudioWave() {
        assertTrue(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(720, 1280, 30.0, "AUREA · Onda", null, UI_MODE_EDIT) }
        compose.waitUntil(15000) { store.project.title == "AUREA · Onda" && !store.projectOperationBusy }

        // Vídeo com som (gerado para o teste: testsrc2 + tom AAC) na trilha principal.
        val video = File(context.filesDir, "video-with-tone.mp4")
        instrumentation.context.assets.open("video-with-tone.mp4").use { input -> video.outputStream().use { input.copyTo(it) } }
        compose.runOnIdle { store.importVideo(android.net.Uri.fromFile(video)) }
        compose.waitUntil(20000) { store.layers.any { it.kind == 1 } }
        val videoId = store.layers.first { it.kind == 1 }.id
        // Áudio solto: vai para a linha de áudio, embaixo.
        compose.runOnIdle { store.importAudioSource(tone(3).absolutePath, "Tom") }
        compose.waitUntil(10000) { store.layers.any { it.kind == 3 } && store.busyMessage == null }
        val audioId = store.layers.single { it.kind == 3 }.id
        compose.runOnIdle { store.seek(15) }

        compose.waitUntil(20000) { hasWave(videoId, 90) && hasWave(audioId, 90) }
        // O tom do vídeo de teste tem pico perto de -2 dBFS: a onda sai cheia (raiz do pico ≈ 0,88).
        compose.runOnIdle {
            val out = ByteBuffer.allocateDirect(256)
            val got = store.queryWaveform(videoId, 0.0, 1.4, 64, out)
            assertTrue((0 until got).maxOf { out.get(it).toInt() and 0xFF } > 180)
            // Na Edição o áudio solto vai para uma linha só dele, embaixo da trilha principal.
            val main = store.layers.first { it.id == store.mainTrackAnchor }
            assertTrue(store.layers.first { it.id == audioId }.trackId != main.trackId)
        }
        compose.runOnIdle { store.select(videoId) }
        compose.waitUntil(5000) {
            compose.onAllNodesWithTag("edit.clip.$videoId").fetchSemanticsNodes().isNotEmpty()
        }
        compose.onNodeWithTag("edit.clip.$videoId").assertIsDisplayed()
        capture("edit-wave-video")
        compose.runOnIdle { store.select(audioId) }
        compose.waitUntil(5000) {
            compose.onAllNodesWithTag("edit.clip.$audioId").fetchSemanticsNodes().isNotEmpty()
        }
        compose.onNodeWithTag("edit.clip.$audioId").assertIsDisplayed()
        capture("edit-wave")
    }
}
