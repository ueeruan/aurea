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
 * A tela "Adicionar efeito" e a pilha compacta no painel Efeitos de produção:
 * camada sem nada abre a tela cheia; ladrilho abre o grupo; um toque adiciona;
 * as ferramentas-efeito (legendas, máscara) abrem o painel delas e aparecem na
 * pilha quando a camada as usa.
 */
class EffectsAddSheetTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext

    private fun selectTab(id: String) {
        val node = compose.onNodeWithTag("effects.tab.$id")
        val config = context.resources.configuration
        if (config.fontScale > 1.2f || config.screenWidthDp < 360) node.performScrollTo()
        node.performClick()
    }

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

        // Camada sem nada: a tela cheia abre sozinha, com ✕, 🔍, destaques e ladrilhos.
        compose.runOnIdle { showPanel = true }
        compose.waitUntil(5000) { compose.onAllNodesWithTag("effects.close").fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithTag("effects.search").assertIsDisplayed()
        val captions = effectCardId(EffectTool.Captions.typeId)
        compose.onNodeWithTag("effects.search.bar").assertIsDisplayed()
        compose.onNodeWithTag("effects.tab.all").assertIsSelected()
        selectTab("new")
        compose.onNodeWithTag("effects.tab.new").assertIsSelected()
        compose.runOnIdle { assertEquals(8, store.catalog.count { it.isNew }) }
        val pixel = effectCardId(effectTypeId("aurea.stylize.pixel_encoder"))
        compose.runOnIdle {
            val id = effectTypeId("aurea.stylize.pixel_encoder")
            if (store.effectPrefs.isFavorite(id)) store.effectPrefs.toggleFavorite(id)
        }
        compose.onNodeWithTag("effects.home").performScrollToNode(hasTestTag("effects.card.$pixel"))
        compose.onNodeWithTag("effects.card.$pixel").assertIsDisplayed()
        capture("fx-add-new")
        compose.onNodeWithTag("effects.card.$pixel").performTouchInput { longClick() }
        selectTab("favorites")
        compose.onNodeWithTag("effects.home").performScrollToNode(hasTestTag("effects.card.$pixel"))
        compose.onNodeWithTag("effects.card.$pixel").assertIsDisplayed()
        capture("fx-add-favorites")
        selectTab("all")
        compose.onNodeWithTag("effects.categories").performScrollToNode(hasTestTag("effects.category.all"))
        capture("fx-add-home")

        // Ladrilho "Texto": as legendas automáticas moram ali; o toque abre o painel delas.
        compose.onNodeWithTag("effects.categories").performScrollToNode(hasTestTag("effects.category.text"))
        compose.onNodeWithTag("effects.category.text").performClick()
        compose.onNodeWithTag("effects.back").assertIsDisplayed()
        compose.onNodeWithTag("effects.grid").performScrollToNode(hasTestTag("effects.card.$captions"))
        compose.onNodeWithTag("effects.card.$captions").assertIsDisplayed()
        capture("fx-add-text")
        compose.onNodeWithTag("effects.card.$captions").performClick()
        compose.waitUntil(5000) { opened == EditorPanel.Captions }
        compose.onNodeWithTag("effects.close").assertDoesNotExist()
        compose.onNodeWithTag("effects.back").assertDoesNotExist()

        // "+ Adicionar efeito" → Glitch → VHS: um toque adiciona e abre o cartão compacto.
        compose.onNodeWithTag("aurea.effects.stack").performScrollToNode(hasTestTag("aurea.effects.add")); compose.onNodeWithTag("aurea.effects.add").performClick()
        compose.waitUntil(5000) { compose.onAllNodesWithTag("effects.close").fetchSemanticsNodes().isNotEmpty() }
        // O recente (a ferramenta tocada) aparece na grade de Recentes.
        selectTab("recent")
        compose.onNodeWithTag("effects.card.$captions").assertIsDisplayed()
        selectTab("all")
        compose.onNodeWithTag("effects.categories").performScrollToNode(hasTestTag("effects.category.glitch"))
        compose.onNodeWithTag("effects.category.glitch").performClick()
        val vhs = effectCardId(effectTypeId("aurea.glitch.vhs"))
        compose.onNodeWithTag("effects.back").assertIsDisplayed()
        compose.onNodeWithTag("effects.grid").performScrollToNode(hasTestTag("effects.card.$vhs"))
        compose.onNodeWithTag("effects.card.$vhs").assertIsDisplayed()
        capture("fx-add-glitch")
        compose.onNodeWithTag("effects.card.$vhs").performClick()
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
        selectTab("new")
        compose.onNodeWithTag("effects.home").performScrollToNode(hasTestTag("effects.card.$pixel"))
        compose.onNodeWithTag("effects.card.$pixel").performClick()
        compose.waitUntil(5000) { store.effects.size == 2 && store.effects.any { it.typeId == effectTypeId("aurea.stylize.pixel_encoder") } }
        compose.onNodeWithTag("effects.close").assertDoesNotExist()
    }
}
