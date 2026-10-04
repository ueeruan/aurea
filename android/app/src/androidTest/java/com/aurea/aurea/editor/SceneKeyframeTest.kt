package com.aurea.aurea.editor

import android.app.Application
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

/**
 * Semântica do After Effects na cena 3D: uma track ANIMADA (stopwatch ligado)
 * recebe keyframe no cabeçote quando o objeto é arrastado com o dedo — a cena
 * "seca" não pode virar "deslocar a animação inteira". Antes `sceneEditor`
 * forçava `layoutTransform` (sem key) e a pose do quadro 0 mudava junto;
 * agora a regra é `transformWrite` (state/TransformWrite.kt).
 */
class SceneKeyframeTest {
    @get:Rule val compose = createComposeRule()

    private lateinit var store: EditorStore

    private fun openNull(title: String) {
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
    }

    private fun seek(frame: Int) {
        compose.runOnIdle { store.seek(frame) }
        compose.waitUntil(5000) { store.playhead == frame && store.detail?.localPlayhead == frame }
    }

    /** Origem do gizmo (a âncora do nulo) em px da tela do palco. */
    private fun TouchInjectionScope.origin(): Offset {
        val g = store.gizmo!!
        val mapper = StageMapper().apply { update(width.toFloat(), height.toFloat(), 0f, 320, 240, false) }
        return Offset(mapper.sx(g[0]), mapper.sy(g[1]))
    }

    @Test fun draggingAnAnimatedNullInTheSceneKeysThePlayheadAndKeepsTheFirstPose() {
        openNull("Cena keyframes")
        val id = store.primary!!
        // Losango de Posição no quadro 0: X, Y e Z ficam animados.
        compose.runOnIdle { store.toggleTransformKeyframe(intArrayOf(0, 1, 2)) }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().count { it.time == 0 } == 3 }
        val first = store.keyframes[id]!!.filter { it.property in 0..2 && it.time == 0 }
            .associate { it.property to it.value }
        seek(30)
        compose.runOnIdle { store.enterSceneEditor() }
        compose.waitUntil(5000) { store.sceneEditor && store.gizmo != null }
        // O gizmo é reprojetado pela câmera de navegação no próximo quadro do laço.
        Thread.sleep(400)
        compose.waitForIdle()
        val before = store.detail!!.position
        compose.onNodeWithTag("scene.stage").performTouchInput {
            val o = origin()
            // One finger rotates; two fingers with constant span translate the object.
            down(0, o - Offset(0f, 40f)); down(1, o + Offset(0f, 40f))
            repeat(4) { index ->
                val center = o + Offset((index + 1) * 30f, 0f)
                updatePointerTo(0, center - Offset(0f, 40f))
                updatePointerTo(1, center + Offset(0f, 40f)); move(60)
            }
            up(0); up(1)
        }
        compose.waitUntil(5000) { store.detail!!.position != before }
        // This horizontal camera-space drag changes X/Z. Auto-Key writes changed
        // axes at frame 30; onlyIfChanged must not add a redundant Y key.
        try {
            compose.waitUntil(5000) {
                store.keyframes[id].orEmpty().filter { it.time == 30 }.map { it.property }.toSet() == setOf(0, 2)
            }
        } catch (error: Throwable) {
            throw AssertionError("Arrastar o nulo animado na cena não marcou keyframe no quadro 30: ${store.keyframes[id]}", error)
        }
        // …e a pose do quadro 0 fica como estava (não é "deslocar a curva inteira").
        compose.runOnIdle {
            val keys = store.keyframes[id]!!
            assertNotEquals("O arrasto deve mover X", before[0], store.detail!!.position[0], 0.01f)
            assertNotEquals("O arrasto deve mover Z", before[2], store.detail!!.position[2], 0.01f)
            assertEquals("O arrasto horizontal deve preservar Y", before[1], store.detail!!.position[1], 0.001f)
            assertTrue("Y sem alteração não deve receber chave redundante", keys.none { it.property == 1 && it.time == 30 })
            for (property in 0..2) {
                assertEquals("A pose inicial não pode mudar no eixo $property", first.getValue(property),
                    keys.single { it.property == property && it.time == 0 }.value, 0.001f)
            }
        }
        // Um arrasto = um passo de desfazer.
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().none { it.time == 30 } }
    }
}
