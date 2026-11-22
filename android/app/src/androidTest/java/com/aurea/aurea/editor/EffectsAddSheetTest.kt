package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.semantics.getOrNull
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.effects.EffectAddSheet
import com.aurea.aurea.effects.EffectDetailOverlay
import com.aurea.aurea.effects.EffectRef
import com.aurea.aurea.effects.EffectTool
import com.aurea.aurea.effects.effectCardId
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.FxRuler
import com.aurea.aurea.ui.ds.FxRulerFling
import com.aurea.aurea.ui.ds.FxRulerState
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

    @Test fun anotherRulerFlingDoesNotBlockScrollInEitherDirection() {
        val scroll = ScrollState(0)
        val owner = FxRulerState()
        var edits = 0
        compose.setContent {
            AureaTheme {
                Column(Modifier.fillMaxWidth().height(168.dp).testTag("test.rulers").verticalScroll(scroll)) {
                    repeat(5) {
                        FxRuler(
                            value = 0f, step = 1f, min = -1000f, max = 1000f,
                            onStart = { edits++ }, onValue = {}, onEnd = {},
                            modifier = Modifier.fillMaxWidth().height(42.dp),
                        )
                    }
                }
            }
        }
        compose.runOnIdle { FxRulerFling.owner = owner }
        try {
            val list = compose.onNodeWithTag("test.rulers")
            list.performTouchInput { swipeUp(durationMillis = 450) }
            compose.runOnIdle { assertEquals(scroll.maxValue, scroll.value) }
            list.performTouchInput { swipeDown(durationMillis = 450) }
            compose.runOnIdle { assertEquals(0, scroll.value); assertEquals(0, edits) }
        } finally {
            compose.runOnIdle { FxRulerFling.owner = null }
        }
    }

    @Test fun effectDetailsScrollToBothEndsAndAcceptAnActualTap() {
        var show by mutableStateOf(false)
        var added = false
        val vhsId = effectTypeId("aurea.glitch.vhs")
        val vhs = effectCardId(vhsId)
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme {
                // Uma ficha que precisa rolar também em emuladores muito altos.
                Box(Modifier.fillMaxWidth().height(412.dp)) {
                    val entry = store.catalog.firstOrNull { it.typeId == vhsId }
                    if (show && entry != null) {
                        EffectDetailOverlay(
                            store = store, entries = listOf(entry), startIndex = 0, closeSignal = 0,
                            onClosed = { show = false }, variants = true,
                            onAdd = { _, _ -> added = true }, previews = null,
                        )
                    }
                }
            }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady && store.catalog.any { it.typeId == vhsId } }
        compose.runOnIdle { show = true }
        compose.waitUntil(5000) { exists("effects.detail.scroll") }

        // Use real drags at both scroll limits, then tap the header with a pointer.
        val detailScroll = compose.onNodeWithTag("effects.detail.scroll")
        fun scrollPosition(): Pair<Float, Float> {
            val axis = detailScroll.fetchSemanticsNode().config[SemanticsProperties.VerticalScrollAxisRange]
            return compose.runOnIdle { axis.value() to axis.maxValue() }
        }
        repeat(5) {
            val (offset, end) = scrollPosition()
            if (offset < end) detailScroll.performTouchInput { swipeUp(durationMillis = 450) }
        }
        val (offset, end) = scrollPosition()
        assertTrue(end > 0f)
        assertEquals(end, offset, 1f)
        detailScroll.performTouchInput { swipeUp(durationMillis = 350) }
        repeat(5) {
            if (scrollPosition().first > 0f) detailScroll.performTouchInput { swipeDown(durationMillis = 450) }
        }
        assertEquals(0f, scrollPosition().first, 1f)

        detailScroll.performTouchInput { swipeDown(durationMillis = 350) }
        compose.onNodeWithTag("effects.detail.variant.$vhs").performScrollTo().performTouchInput { click() }
        compose.runOnIdle { assertTrue(added) }
        compose.onNodeWithTag("effects.detail.close").performTouchInput { click() }
        compose.waitUntil(5000) { !exists("effects.detail.scroll") }
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
        compose.onNodeWithTag("effects.detail.favorite.$vhs").performScrollTo().performTouchInput { click() }
        compose.runOnIdle { assertTrue(store.effectPrefs.isFavorite(vhsId)) }
        capture("fx-detail-vhs")
        compose.onNodeWithTag("effects.detail.close").performTouchInput { click() }
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

        // Variação: Cor e luz → Correção de cor → detalhe → 1º preset. Adiciona com o
        // preset, grava "efeito/preset" nos recentes e desfaz num passo só.
        val colorGradeId = effectTypeId("aurea.color.grade")
        val colorGrade = effectCardId(colorGradeId)
        compose.runOnIdle { showBrowser = true }
        compose.waitUntil(5000) { exists("effects.close") }
        // O VHS já está na camada: sai dos Recomendados.
        compose.onNodeWithTag("effects.featured.$vhs").assertDoesNotExist()
        compose.onNodeWithTag("effects.home").performScrollToNode(hasTestTag("effects.category.color_light"))
        compose.onNodeWithTag("effects.category.color_light").performClick()
        compose.onNodeWithTag("effects.grid").performScrollToNode(hasTestTag("effects.card.$colorGrade"))
        compose.onNodeWithTag("effects.card.$colorGrade").performClick()
        compose.waitUntil(5000) { exists("effects.detail.variant.$colorGrade.0") }
        capture("fx-detail-variations")
        val before = store.effects.size
        compose.onNodeWithTag("effects.detail.variant.$colorGrade.0").performScrollTo().performTouchInput { click() }
        compose.waitUntil(8000) { store.effects.any { it.typeId == colorGradeId } }
        compose.runOnIdle { assertEquals(EffectRef(colorGradeId, 0), store.effectPrefs.recentRefs.first()) }
        compose.waitForIdle()
        Thread.sleep(1500)   // o preset entra no quadro seguinte do motor e o grupo de desfazer fecha
        compose.runOnIdle { store.undo() }
        compose.waitUntil(8000) { store.effects.none { it.typeId == colorGradeId } && store.effects.size == before }

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
