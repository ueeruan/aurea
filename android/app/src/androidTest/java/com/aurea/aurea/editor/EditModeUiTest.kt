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
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.UI_MODE_COMPOSITION
import com.aurea.aurea.state.UI_MODE_EDIT
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
}
