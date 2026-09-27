package com.aurea.aurea.editor

import android.app.Application
import android.util.Log
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import kotlin.math.sqrt

/**
 * O arrasto na cena 3D manda um alvo ABSOLUTO a cada evento de toque, lido do
 * modelo do motor (`gizmoMoveLocal`) SEM os comandos ainda na fila. Dois
 * eventos no mesmo quadro do motor partem da mesma base e o segundo
 * sobrescreve o primeiro: o passo se perde. O mesmo arrasto (8 × 15 px) é
 * feito devagar (60 ms por evento: um por quadro do motor) e depressa (2 ms
 * por evento: vários por quadro). O deslocamento tem de ser o mesmo.
 */
class SceneDragRateTest {
    @get:Rule val compose = createComposeRule()

    private lateinit var store: EditorStore

    private fun openAnimatedNullAt30(title: String): Long {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        var initialized = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            initialized = true
            val ui = remember { EditorUi() }
            AureaTheme { PreviewStage(store, ui, Modifier.fillMaxWidth().height(320.dp).testTag("scene.stage")) }
        }
        compose.waitUntil(30000) { initialized && store.engineReady }
        compose.runOnIdle { store.newProject(320, 240, 30f, title) }
        compose.waitUntil(15000) { store.project.title == title }
        compose.runOnIdle { store.addNull(true) }
        compose.waitUntil(5000) { store.gizmo != null && store.detail != null }
        val id = store.primary!!
        // Losango de Posição no quadro 0: X, Y e Z ficam animados.
        compose.runOnIdle { store.toggleTransformKeyframe(intArrayOf(0, 1, 2)) }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().count { it.time == 0 } == 3 }
        compose.runOnIdle { store.seek(30) }
        compose.waitUntil(5000) { store.playhead == 30 && store.detail?.localPlayhead == 30 }
        compose.runOnIdle { store.enterSceneEditor() }
        compose.waitUntil(5000) { store.sceneEditor && store.gizmo != null }
        // O gizmo é reprojetado pela câmera de navegação no próximo quadro do laço.
        Thread.sleep(400)
        compose.waitForIdle()
        return id
    }

    /** Origem do gizmo (a âncora do nulo) em px da tela do palco. */
    private fun TouchInjectionScope.origin(): Offset {
        val g = store.gizmo!!
        val mapper = StageMapper().apply { update(width.toFloat(), height.toFloat(), 0f, 320, 240, false) }
        return Offset(mapper.sx(g[0]), mapper.sy(g[1]))
    }

    /** Δ (X, Y, Z) do keyframe de Posição no quadro 30 em relação ao do quadro 0. */
    private fun keyDelta(id: Long): FloatArray {
        val keys = store.keyframes[id].orEmpty()
        return FloatArray(3) { p ->
            val v0 = keys.single { it.property == p && it.time == 0 }.value
            val v30 = keys.firstOrNull { it.property == p && it.time == 30 }?.value ?: v0
            v30 - v0
        }
    }

    private fun FloatArray.length() = sqrt(this[0] * this[0] + this[1] * this[1] + this[2] * this[2])

    /** Espera o fim do gesto (key de X no quadro 30) e o laço de status assentar. */
    private fun settle(id: Long): FloatArray {
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().any { it.property == 0 && it.time == 30 } }
        Thread.sleep(600)
        compose.waitForIdle()
        return keyDelta(id)
    }

    private fun undoDrag(id: Long) {
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().none { it.time == 30 } }
        Thread.sleep(400)
        compose.waitForIdle()
    }

    /**
     * Um evento de toque por quadro do motor: cada `moveBy` vai num
     * `performTouchInput` próprio e a thread de teste espera 500 ms de verdade
     * (o `delayMillis` do `moveBy` só avança o carimbo de tempo; um bloco
     * inteiro sai de uma vez, sem esperar).
     */
    private fun dragOnePerFrame(steps: Int, stepPx: Float) {
        val stage = compose.onNodeWithTag("scene.stage")
        stage.performTouchInput { down(origin()) }
        repeat(steps) {
            stage.performTouchInput { moveBy(Offset(stepPx, 0f)) }
            Thread.sleep(500)
        }
        stage.performTouchInput { up() }
    }

    /** Rajada: os eventos saem num bloco só, vários antes de o motor drenar a fila. */
    private fun dragBurst(steps: Int, stepPx: Float) {
        compose.onNodeWithTag("scene.stage").performTouchInput {
            down(origin())
            repeat(steps) { moveBy(Offset(stepPx, 0f), 8) }
            up()
        }
    }

    @Test fun touchEventsFasterThanTheEngineFrameStillMoveTheWholeDistance() {
        val id = openAnimatedNullAt30("Cena ritmo do toque")
        dragOnePerFrame(8, 15f)
        val reference = settle(id)
        undoDrag(id)
        dragBurst(8, 15f)
        val burst = settle(id)
        val message = "8 × 15 px — um evento por quadro: ${reference.toList()} |${reference.length()}|; " +
            "rajada: ${burst.toList()} |${burst.length()}|"
        Log.i("SceneDragRate", message)
        assertTrue("A referência não moveu: $message", reference.length() > 1f)
        assertTrue("Eventos na mesma janela do motor perderam passos: $message", burst.length() >= reference.length() * 0.85f)
    }

    /** O mesmo, sem toque: oito passos do store na MESMA volta da thread principal. */
    @Test fun eightStoreStepsInOneUiTurnAddUp() {
        val id = openAnimatedNullAt30("Cena passos na mesma volta")
        compose.runOnIdle { store.beginGesture("mover na cena") }
        repeat(8) { compose.runOnIdle { store.sceneDragObject(4f, 0f) }; Thread.sleep(60) }
        compose.runOnIdle { store.endGesture() }
        val slow = settle(id)
        undoDrag(id)
        compose.runOnIdle {
            store.beginGesture("mover na cena")
            repeat(8) { store.sceneDragObject(4f, 0f) }
            store.endGesture()
        }
        val fast = settle(id)
        val message = "8 × 4 px da composição — um por quadro: ${slow.toList()} |${slow.length()}|; na mesma volta: ${fast.toList()} |${fast.length()}|"
        Log.i("SceneDragRate", message)
        assertTrue("Passos lentos não moveram: $message", slow.length() > 1f)
        assertTrue("Passos na mesma volta se sobrescreveram: $message", fast.length() >= slow.length() * 0.85f)
    }
}
