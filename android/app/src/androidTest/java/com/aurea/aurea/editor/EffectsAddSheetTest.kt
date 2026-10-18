package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.editor.panels.PanelContent
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.effects.EffectTool
import com.aurea.aurea.effects.effectCardId
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import java.io.File

/**
 * O navegador de efeitos e a pilha compacta no painel Efeitos de produção:
 * camada sem nada abre a tela cheia (destaques, recentes/favoritos, ladrilhos);
 * ladrilho abre o grupo; um toque adiciona; segurar favorita;
 * as ferramentas-efeito (legendas, máscara) abrem o painel delas e aparecem na
 * pilha quando a camada as usa.
 */
class EffectsAddSheetTest {
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

    @Test fun addSheetGroupsToolsAndCompactStack() {
        assertTrue(context.packageName.endsWith(".uitest"))
        var showPanel by mutableStateOf(false)
        var opened by mutableStateOf<EditorPanel?>(null)
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme {
                // O editor de produção roda por baixo (é ele que faz o motor aplicar
                // os comandos a cada quadro); o painel Efeitos fica embaixo, por cima.
                androidx.compose.foundation.layout.Box(androidx.compose.ui.Modifier.fillMaxSize()) {
                    EditorScreen(store)
                    if (showPanel) androidx.compose.foundation.layout.Box(
                        androidx.compose.ui.Modifier.align(androidx.compose.ui.Alignment.BottomCenter).fillMaxWidth().fillMaxHeight(0.45f),
                    ) {
                        PanelContent(store, EditorPanel.Effects, onClose = {}, onOpenPanel = { opened = it }, onOpenEffectsBrowser = {})
                    }
                }
            }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(720, 900, 30f, "AUREA · Efeitos") }
        compose.waitUntil(15000) { store.project.title == "AUREA · Efeitos" && !store.projectOperationBusy }
        compose.runOnIdle { store.select(store.engineForStress.addText("AUREA")) }
        compose.waitUntil(10000) { store.textDetail != null && store.layers.size == 1 }

        // Camada sem nada: o navegador abre sozinho, com ✕, 🔍, destaques, recentes e ladrilhos.
        compose.runOnIdle { showPanel = true }
        compose.waitUntil(5000) { compose.onAllNodesWithTag("effects.close").fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithTag("effects.search").assertIsDisplayed()
        compose.onNodeWithTag("effects.featured").assertIsDisplayed()
        compose.onNodeWithTag("effects.quick").assertIsDisplayed()
        compose.onNodeWithTag("effects.quick.recent").assertIsSelected()
        val captions = effectCardId(EffectTool.Captions.typeId)
        val vhsId = effectTypeId("aurea.glitch.vhs")
        val vhs = effectCardId(vhsId)
        compose.runOnIdle { if (store.effectPrefs.isFavorite(vhsId)) store.effectPrefs.toggleFavorite(vhsId) }
        capture("fx-add-home")

        // Ladrilho "Texto": as legendas automáticas moram ali; o toque abre o painel delas.
        compose.onNodeWithTag("effects.home").performScrollToNode(hasTestTag("effects.category.text"))
        compose.onNodeWithTag("effects.category.text").performClick()
        compose.onNodeWithTag("effects.back").assertIsDisplayed()
        compose.onNodeWithTag("effects.grid").performScrollToNode(hasTestTag("effects.card.$captions"))
        compose.onNodeWithTag("effects.card.$captions").assertIsDisplayed()
        capture("fx-add-text")
        compose.onNodeWithTag("effects.card.$captions").performClick()
        compose.waitUntil(5000) { opened == EditorPanel.Captions }
        compose.onNodeWithTag("effects.close").assertDoesNotExist()
        compose.onNodeWithTag("effects.back").assertDoesNotExist()

        // "+ Adicionar efeito" → Glitch → segurar VHS favorita → a estrela mostra; um toque adiciona.
        compose.onNodeWithTag("aurea.effects.stack").performScrollToNode(hasTestTag("aurea.effects.add")); compose.onNodeWithTag("aurea.effects.add").performClick()
        compose.waitUntil(5000) { compose.onAllNodesWithTag("effects.close").fetchSemanticsNodes().isNotEmpty() }
        // O recente (a ferramenta tocada) aparece na página de Recentes.
        compose.onNodeWithTag("effects.recent.$captions").assertIsDisplayed()
        compose.onNodeWithTag("effects.home").performScrollToNode(hasTestTag("effects.category.glitch"))
        compose.onNodeWithTag("effects.category.glitch").performClick()
        compose.onNodeWithTag("effects.back").assertIsDisplayed()
        compose.onNodeWithTag("effects.grid").performScrollToNode(hasTestTag("effects.card.$vhs"))
        compose.onNodeWithTag("effects.card.$vhs").performTouchInput { longClick() }
        compose.runOnIdle { assertTrue(store.effectPrefs.isFavorite(vhsId)) }
        capture("fx-add-glitch")
        compose.onNodeWithTag("effects.back").performClick()
        compose.onNodeWithTag("effects.quick.favorites").performClick()
        compose.waitUntil(5000) { compose.onAllNodesWithTag("effects.favorite.$vhs").fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithTag("effects.quick.favorites").assertIsSelected()
        capture("fx-add-favorites")
        compose.onNodeWithTag("effects.favorite.$vhs").performClick()
        compose.waitUntil(5000) { store.effects.size == 1 }
        compose.waitUntil(5000) { compose.onAllNodesWithTag("effects.more.$vhs").fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithTag("effects.close").assertDoesNotExist()
        compose.onNodeWithTag("effects.remove.$vhs").assertIsDisplayed()

        // Máscara na camada: o cartão da ferramenta aparece no topo da pilha e reabre o painel.
        compose.runOnIdle { store.addMaskPreset(0) }
        compose.waitUntil(5000) { (store.masks?.masks?.size ?: 0) == 1 }
        compose.onNodeWithTag("aurea.effects.stack").performScrollToIndex(0)
        compose.waitUntil(5000) { compose.onAllNodesWithTag("effects.tool.mask").fetchSemanticsNodes().isNotEmpty() }
        capture("fx-stack")
        compose.runOnIdle { opened = null }
        compose.onNodeWithTag("effects.tool.mask").performClick()
        compose.waitUntil(5000) { opened == EditorPanel.Mask }

        // Busca em qualquer idioma acha a ferramenta pelo sinônimo.
        compose.onNodeWithTag("aurea.effects.stack").performScrollToNode(hasTestTag("aurea.effects.add")); compose.onNodeWithTag("aurea.effects.add").performClick()
        compose.onNodeWithTag("effects.search").performClick()
        compose.onNodeWithTag("effects.search.field").performTextInput("subtitles")
        compose.waitUntil(5000) { compose.onAllNodesWithTag("effects.result.$captions").fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithTag("effects.search.field").performTextReplacement("zzzz-no-such-effect")
        compose.waitUntil(5000) { compose.onAllNodesWithTag("effects.result.$captions").fetchSemanticsNodes().isEmpty() }
        compose.onNodeWithTag("effects.search.field").performTextReplacement("subtitles")
        compose.waitUntil(5000) { compose.onAllNodesWithTag("effects.result.$captions").fetchSemanticsNodes().isNotEmpty() }
        capture("fx-search")
        compose.runOnIdle { assertEquals(1, store.effects.size) }
        compose.onNodeWithTag("effects.search.field").performImeAction()
        compose.onNodeWithTag("effects.search.back").performClick()
        // Destaques: o cartão grande também adiciona com um toque.
        val pixelId = effectTypeId("aurea.stylize.pixel_encoder")
        val pixel = effectCardId(pixelId)
        compose.onNodeWithTag("effects.featured").performScrollToNode(hasTestTag("effects.featured.$pixel"))
        compose.onNodeWithTag("effects.featured.$pixel").performClick()
        compose.waitUntil(5000) { store.effects.size == 2 && store.effects.any { it.typeId == pixelId } }
        compose.onNodeWithTag("effects.close").assertDoesNotExist()
    }
}
