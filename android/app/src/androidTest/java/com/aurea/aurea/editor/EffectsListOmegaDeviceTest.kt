package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.effects.effectCardId
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/**
 * Omega 0.3.2, área 07 (P0-2): a LISTA de efeitos da referência no editor real.
 * Camada vazia mostra só "+ Adicionar efeito" (o navegador não abre sozinho); o
 * 1º efeito abre os controles sozinho; o 2º não; fechar pelo cabeçalho volta à
 * lista; toque longo na linha reordena (um passo de desfazer); deslizar para a
 * esquerda apaga e o "Desfazer" volta; ⋮ Duplicar põe a cópia no fim.
 */
class EffectsListOmegaDeviceTest {
    @get:Rule val compose = createComposeRule()
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private lateinit var store: EditorStore
    private val blur = effectTypeId("aurea.blur.gaussian")
    private val glow = effectTypeId("aurea.light.glow")

    private fun open() {
        check(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(720, 720, 30f, "Omega effects list") }
        compose.waitUntil(15000) { store.project.title == "Omega effects list" && !store.projectOperationBusy }
        compose.runOnIdle { store.addShape(1) }
        compose.waitUntil(10000) { store.layers.size == 1 && store.detail != null }
        compose.onNodeWithTag("layer.panel.effects").performClick()
    }

    private fun exists(tag: String) = compose.onAllNodesWithTag(tag).fetchSemanticsNodes().isNotEmpty()

    @Test fun emptyLayerFirstEffectOpensSecondDoesNotAndHeaderCloses() {
        open()
        // Camada vazia: só a linha "+ Adicionar efeito"; nada abre sozinho.
        compose.onNodeWithTag("effects.add.row").assertExists()
        compose.onNodeWithTag("aurea.effects.add").assertExists()
        compose.onNodeWithTag("effects.close").assertDoesNotExist()
        // 1º efeito: os controles abrem sozinhos (referência: autoSelectEffect).
        compose.runOnIdle { store.addEffect(blur) }
        compose.waitUntil(10000) { exists("effects.settings.${effectCardId(blur)}") }
        // Fechar pelo cabeçalho: volta à lista.
        compose.onNodeWithTag("effects.settings.header").performClick()
        compose.waitUntil(5000) { !exists("effects.settings.header") }
        compose.onNodeWithTag("effects.expand.${effectCardId(blur)}").assertExists()
        // 2º efeito: a lista continua fechada.
        compose.runOnIdle { store.addEffect(glow) }
        compose.waitUntil(10000) { store.effects.size == 2 }
        compose.waitForIdle()
        compose.onNodeWithTag("effects.settings.header").assertDoesNotExist()
        compose.onNodeWithTag("effects.expand.${effectCardId(glow)}").assertExists()
    }

    @Test fun longPressReordersSwipeDeletesAndDuplicateGoesLast() {
        open()
        compose.runOnIdle { store.addEffect(blur) }
        compose.waitUntil(10000) { store.effects.size == 1 }
        compose.runOnIdle { store.addEffect(glow) }
        compose.waitUntil(10000) { store.effects.size == 2 }
        if (exists("effects.settings.header")) {
            compose.onNodeWithTag("effects.settings.header").performClick()
            compose.waitUntil(5000) { !exists("effects.settings.header") }
        }
        // Toque longo na linha do 2º e arrastar para cima: um reorder.
        compose.onNodeWithTag("effects.expand.${effectCardId(glow)}").performTouchInput {
            down(center)
            advanceEventTime(viewConfiguration.longPressTimeoutMillis + 200)
            moveBy(androidx.compose.ui.geometry.Offset(0f, -height * 1.6f), 300)
            up()
        }
        compose.waitUntil(5000) { store.effects.firstOrNull()?.typeId == glow }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.effects.firstOrNull()?.typeId == blur }
        // Deslizar para a esquerda apaga; o aviso traz "Desfazer".
        compose.onNodeWithTag("effects.expand.${effectCardId(blur)}").performTouchInput { swipeLeft(startX = right - 4f, endX = left, durationMillis = 300) }
        compose.waitUntil(5000) { store.effects.size == 1 }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.effects.size == 2 }
        // ⋮ Duplicar: a cópia vai para o FIM e os controles fecham.
        compose.onNodeWithTag("effects.expand.${effectCardId(blur)}").performClick()
        compose.waitUntil(5000) { exists("effects.more.${effectCardId(blur)}") }
        compose.onNodeWithTag("effects.more.${effectCardId(blur)}").performClick()
        compose.onNodeWithTag("effects.menu.duplicate").performClick()
        compose.waitUntil(5000) { store.effects.size == 3 }
        compose.runOnIdle { assertEquals(blur, store.effects.last().typeId) }
        compose.waitUntil(5000) { !exists("effects.settings.header") }
    }
}
