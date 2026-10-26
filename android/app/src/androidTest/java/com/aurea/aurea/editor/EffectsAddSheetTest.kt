package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.semantics.getOrNull
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.effects.EffectAddSheet
import com.aurea.aurea.effects.EffectRef
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
 * O navegador de efeitos da Omega (área 07, P0-6), com o editor de produção por
 * baixo: grade → DETALHE → a variação adiciona; a ferramenta-efeito (Legendas)
 * abre a ferramenta pelo detalhe; Recentes adiciona com um toque e o toque longo
 * abre o detalhe; a estrela do detalhe favorita; efeito + preset é UM passo de
 * desfazer; a busca tem etiquetas (fichas) e acha em qualquer idioma.
 *
 * O navegador é aberto direto (`EffectAddSheet`), sem passar pelo painel
 * Efeitos, que tem o próprio teste.
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

    private fun exists(tag: String) = compose.onAllNodesWithTag(tag).fetchSemanticsNodes().isNotEmpty()

    private fun tagPrefix(prefix: String) = SemanticsMatcher("testTag começa com $prefix") {
        it.config.getOrNull(SemanticsProperties.TestTag)?.startsWith(prefix) == true
    }

    @Test fun browserDetailVariationsRecentsFavoritesAndSearch() {
        assertTrue(context.packageName.endsWith(".uitest"))
        var showBrowser by mutableStateOf(false)
        var opened by mutableStateOf<EffectTool?>(null)
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme {
                // O editor de produção roda por baixo (é ele que faz o motor aplicar os comandos).
                Box(Modifier.fillMaxSize()) {
                    EditorScreen(store)
                    if (showBrowser) EffectAddSheet(
                        store = store,
                        layerHasAudio = false,
                        onPick = { e -> showBrowser = false; store.addEffect(e.typeId) },
                        onTool = { tool -> showBrowser = false; opened = tool },
                        onDismiss = { showBrowser = false },
                    )
                }
            }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(720, 900, 30f, "AUREA · Efeitos") }
        compose.waitUntil(15000) { store.project.title == "AUREA · Efeitos" && !store.projectOperationBusy }
        compose.runOnIdle { store.select(store.engineForStress.addText("AUREA")) }
        compose.waitUntil(10000) { store.textDetail != null && store.layers.size == 1 }
        compose.runOnIdle {
            store.effectPrefs.clearRecents()
            store.effectPrefs.favoriteRefs.toList().forEach { store.effectPrefs.toggleFavorite(it) }
            store.effectPrefs.updateQuickPage(0)
        }
        val captions = effectCardId(EffectTool.Captions.typeId)
        val vhsId = effectTypeId("aurea.glitch.vhs")
        val vhs = effectCardId(vhsId)

        // Início: ✕ · título · 🔍, Recomendados e categorias; sem recentes nem favoritos a faixa some.
        compose.runOnIdle { showBrowser = true }
        compose.waitUntil(5000) { exists("effects.close") }
        compose.onNodeWithTag("effects.search").assertIsDisplayed()
        compose.onNodeWithTag("effects.featured").assertIsDisplayed()
        compose.onNodeWithTag("effects.quick").assertDoesNotExist()
        capture("fx-add-home")

        // Categoria Texto: dentro dela não há 🔍; o cartão das Legendas abre o DETALHE,
        // e a linha do efeito base abre a ferramenta.
        compose.onNodeWithTag("effects.home").performScrollToNode(hasTestTag("effects.category.text"))
        compose.onNodeWithTag("effects.category.text").performClick()
        compose.onNodeWithTag("effects.back").assertIsDisplayed()
        compose.onNodeWithTag("effects.search").assertDoesNotExist()
        compose.onNodeWithTag("effects.category.prev").assertIsDisplayed()
        compose.onNodeWithTag("effects.grid").performScrollToNode(hasTestTag("effects.card.$captions"))
        compose.onNodeWithTag("effects.card.$captions").performClick()
        compose.waitUntil(5000) { exists("effects.detail.variant.$captions") }
        capture("fx-detail-captions")
        compose.onNodeWithTag("effects.detail.variant.$captions").performScrollTo().performClick()
        compose.waitUntil(5000) { opened == EffectTool.Captions }
        compose.onNodeWithTag("effects.close").assertDoesNotExist()

        // Reaberto: a ferramenta está em Recentes.
        compose.runOnIdle { showBrowser = true }
        compose.waitUntil(5000) { exists("effects.close") }
        compose.onNodeWithTag("effects.recent.$captions").assertIsDisplayed()

        // Glitch → VHS → detalhe → a estrela favorita o efeito base; ✕ fecha o detalhe.
        compose.onNodeWithTag("effects.home").performScrollToNode(hasTestTag("effects.category.glitch"))
        compose.onNodeWithTag("effects.category.glitch").performClick()
        compose.onNodeWithTag("effects.grid").performScrollToNode(hasTestTag("effects.card.$vhs"))
        compose.onNodeWithTag("effects.card.$vhs").performClick()
        compose.waitUntil(5000) { exists("effects.detail.favorite.$vhs") }
        compose.onNodeWithTag("effects.detail.favorite.$vhs").performScrollTo().performClick()
        compose.runOnIdle { assertTrue(store.effectPrefs.isFavorite(vhsId)) }
        capture("fx-detail-vhs")
        compose.onNodeWithTag("effects.detail.close").performClick()
        compose.waitUntil(5000) { !exists("effects.detail") }
        compose.onNodeWithTag("effects.back").performClick()

        // Favoritos: a página depois de Recentes. Toque longo abre o detalhe; toque adiciona e fecha.
        compose.onNodeWithTag("effects.home").performScrollToNode(hasTestTag("effects.quick"))
        if (!exists("effects.favorite.$vhs")) compose.onNodeWithTag("effects.quick").performTouchInput { swipeLeft() }
        compose.waitUntil(5000) { exists("effects.favorite.$vhs") }
        compose.onNodeWithTag("effects.quick.page.1").assertIsSelected()
        compose.onNodeWithTag("effects.favorite.$vhs").performTouchInput { longClick() }
        compose.waitUntil(5000) { exists("effects.detail") }
        compose.onNodeWithTag("effects.detail.close").performClick()
        compose.waitUntil(5000) { !exists("effects.detail") }
        capture("fx-add-favorites")
        compose.onNodeWithTag("effects.favorite.$vhs").performClick()
        compose.waitUntil(5000) { store.effects.any { it.typeId == vhsId } }
        compose.onNodeWithTag("effects.close").assertDoesNotExist()

        // Variação: Movimento → Tremor (Twitch) → detalhe → 1º preset. Adiciona com o
        // preset, grava "efeito/preset" nos recentes e desfaz num passo só.
        val twitchId = effectTypeId("aurea.motion.twitch")
        val twitch = effectCardId(twitchId)
        compose.runOnIdle { showBrowser = true }
        compose.waitUntil(5000) { exists("effects.close") }
        // O VHS já está na camada: sai dos Recomendados.
        compose.onNodeWithTag("effects.featured.$vhs").assertDoesNotExist()
        compose.onNodeWithTag("effects.home").performScrollToNode(hasTestTag("effects.category.motion"))
        compose.onNodeWithTag("effects.category.motion").performClick()
        compose.onNodeWithTag("effects.grid").performScrollToNode(hasTestTag("effects.card.$twitch"))
        compose.onNodeWithTag("effects.card.$twitch").performClick()
        compose.waitUntil(5000) { exists("effects.detail.variant.$twitch.0") }
        capture("fx-detail-variations")
        val before = store.effects.size
        compose.onNodeWithTag("effects.detail.variant.$twitch.0").performScrollTo().performClick()
        compose.waitUntil(8000) { store.effects.any { it.typeId == twitchId } }
        compose.runOnIdle { assertEquals(EffectRef(twitchId, 0), store.effectPrefs.recentRefs.first()) }
        compose.waitForIdle()
        Thread.sleep(1500)   // o preset entra no quadro seguinte do motor e o grupo de desfazer fecha
        compose.runOnIdle { store.undo() }
        compose.waitUntil(8000) { store.effects.none { it.typeId == twitchId } && store.effects.size == before }

        // Busca: vazia mostra as etiquetas; a etiqueta vira ficha e filtra; ✕ limpa tudo.
        compose.runOnIdle { showBrowser = true }
        compose.waitUntil(5000) { exists("effects.close") }
        compose.onNodeWithTag("effects.search").performClick()
        compose.waitUntil(5000) { exists("effects.search.tag.glitch") }
        capture("fx-search-tags")
        compose.onNodeWithTag("effects.search.tag.glitch").performScrollTo().performClick()
        compose.waitUntil(5000) { exists("effects.search.chip.glitch") }
        compose.onNodeWithTag("effects.search.results").performScrollToNode(hasTestTag("effects.result.$vhs"))
        compose.onNodeWithTag("effects.search.clear").performClick()
        compose.waitUntil(5000) { !exists("effects.search.chip.glitch") && exists("effects.search.tag.glitch") }
        // Em qualquer idioma: "subtitles" acha as Legendas automáticas.
        compose.onNodeWithTag("effects.search.field").performTextInput("subtitles")
        compose.waitUntil(5000) { exists("effects.result.$captions") }
        compose.onNodeWithTag("effects.search.field").performTextReplacement("zzzz-no-such-effect")
        compose.waitUntil(5000) { !exists("effects.result.$captions") }
        capture("fx-search")
        compose.onNodeWithTag("effects.search.back").performClick()

        // Recomendado → detalhe; a etiqueta do detalhe abre a busca com a ficha.
        compose.onAllNodes(tagPrefix("effects.featured.")).onFirst().performClick()
        compose.waitUntil(5000) { exists("effects.detail") }
        compose.onAllNodes(tagPrefix("effects.detail.tag.")).onFirst().performClick()
        compose.waitUntil(5000) { compose.onAllNodes(tagPrefix("effects.search.chip.")).fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithTag("effects.detail").assertDoesNotExist()
        compose.onNodeWithTag("effects.search.back").performClick()
        compose.onNodeWithTag("effects.close").performClick()
        compose.waitUntil(5000) { !exists("effects.close") }
        compose.runOnIdle { assertEquals(1, store.effects.count { it.typeId == vhsId }) }
    }
}
