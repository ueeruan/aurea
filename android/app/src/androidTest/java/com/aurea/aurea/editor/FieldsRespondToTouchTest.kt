package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsNodeInteraction
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.editor.panels.PanelContent
import com.aurea.aurea.home.NewProjectDefaults
import com.aurea.aurea.home.NewProjectSheet
import com.aurea.aurea.home.NewProjectSpec
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import kotlin.math.abs
import kotlin.math.min

/**
 * Bug 6b (0.3.5): "alguns campos não respondem ao toque (rotação, X/Y/Z, fps e
 * resolução ao criar uma composição)". Toques de DEDO de verdade (o dedo fica
 * 160–180 ms na tela, como um toque comum; o `performClick` dura um quadro e não
 * pegava o defeito da roda, que só aceitava toques de até 100 ms):
 *  - folha Novo projeto: a caixa de fps e a de resolução abrem, a opção tocada
 *    vira o valor da caixa e chega ao projeto criado;
 *  - Ajustes do projeto: fps e resolução mudam a composição no motor;
 *  - Transformar: o campo X abre o teclado e grava; a roda de rotação abre o
 *    teclado com um toque parado e grava o ângulo.
 * Par dos testes `testNewProjectFpsDropdownOpensOverTheSheetAndChangesTheValue`,
 * `testProjectSettingsFpsDropdownOpensAndChangesTheCompositionRate` e
 * `testTransformXFieldAndRotationDialRespondToAFingerTap` do iOS.
 */
class FieldsRespondToTouchTest {
    @get:Rule val compose = createComposeRule()
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private lateinit var store: EditorStore

    /** Um toque de dedo: desce, fica [holdMs] parado e sobe. */
    private fun SemanticsNodeInteraction.fingerTap(holdMs: Long = 160) = performTouchInput {
        down(center)
        advanceEventTime(holdMs)
        up()
    }

    private fun state(tag: String): String? = runCatching {
        compose.onNodeWithTag(tag).fetchSemanticsNode().config.getOrNull(SemanticsProperties.StateDescription)
    }.getOrNull()

    private fun waitForTag(tag: String, timeoutMs: Long = 5_000) =
        compose.waitUntil(timeoutMs) { compose.onAllNodesWithTag(tag).fetchSemanticsNodes().isNotEmpty() }

    private fun waitGone(tag: String, timeoutMs: Long = 5_000) =
        compose.waitUntil(timeoutMs) { compose.onAllNodesWithTag(tag).fetchSemanticsNodes().isEmpty() }

    /** Abre a caixa [box] com um toque de dedo e toca na opção [option]. */
    private fun pick(box: String, option: String) {
        compose.onNodeWithTag(box).performScrollTo().fingerTap()
        waitForTag("$box.menu")
        waitForTag(option)
        compose.onNodeWithTag(option).fingerTap(120)
        waitGone("$box.menu")
    }

    @Test fun newProjectFpsAndResolutionBoxesOpenAndChangeTheSheet() {
        check(context.packageName.endsWith(".uitest"))
        var created: NewProjectSpec? = null
        compose.setContent {
            AureaTheme {
                NewProjectSheet(
                    suggestedName = "Bug 6b",
                    defaults = NewProjectDefaults(
                        aspectKey = "16:9", resolution = 1080, fps = 30.0, free = false,
                        freeWidth = 1080, freeHeight = 1350, keepRatio = true, background = null,
                    ),
                    onCreate = { created = it },
                    onDismiss = {},
                )
            }
        }
        waitForTag("newProject.fps")
        assertEquals("30 fps", state("newProject.fps"))
        pick("newProject.fps", "newProject.fps.24")
        compose.waitUntil(3_000) { state("newProject.fps") == "24 fps" }

        // A caixa não morre depois do primeiro uso.
        pick("newProject.fps", "newProject.fps.60")
        compose.waitUntil(3_000) { state("newProject.fps") == "60 fps" }

        assertEquals("1080p", state("newProject.resolution"))
        pick("newProject.resolution", "newProject.resolution.720")
        compose.waitUntil(3_000) { state("newProject.resolution") == "720p" }

        compose.onNodeWithTag("newProject.create").performScrollTo().fingerTap()
        compose.waitUntil(5_000) { created != null }
        val spec = checkNotNull(created)
        assertEquals(60.0, spec.fps, 1e-6)
        assertEquals(720, spec.resolution)
        assertEquals(720, min(spec.width, spec.height))
    }

    @Test fun projectSettingsFpsAndResolutionBoxesChangeTheComposition() {
        check(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            val ui = remember { EditorUi() }
            AureaTheme { ProjectSettingsSheet(store, ui, onDismiss = {}) }
        }
        compose.waitUntil(30_000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(1920, 1080, 30f, "Bug 6b settings") }
        compose.waitUntil(15_000) { store.project.title == "Bug 6b settings" && !store.projectOperationBusy && store.composition != null }
        waitForTag("projectSettings.fps")

        pick("projectSettings.fps", "projectSettings.fps.24")
        compose.waitUntil(5_000) { abs((store.composition?.fps ?: 0.0) - 24.0) < 1e-3 }
        compose.waitUntil(3_000) { state("projectSettings.fps") == "24 fps" }

        pick("projectSettings.fps", "projectSettings.fps.60")
        compose.waitUntil(5_000) { abs((store.composition?.fps ?: 0.0) - 60.0) < 1e-3 }

        pick("projectSettings.resolution", "projectSettings.resolution.720")
        compose.waitUntil(5_000) { store.composition?.let { min(it.width, it.height) } == 720 }
        val c = store.composition!!
        assertEquals("16:9 stays 16:9", 1280, maxOf(c.width, c.height))
    }

    @Test fun transformXFieldAndRotationDialOpenTheKeypadWithAFingerTap() {
        check(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme {
                Column(Modifier.fillMaxSize()) {
                    PanelContent(store, EditorPanel.Transform, {}, {}, {}, Modifier.fillMaxWidth().height(300.dp))
                }
            }
        }
        compose.waitUntil(30_000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(1080, 1080, 30f, "Bug 6b transform") }
        compose.waitUntil(15_000) { store.project.title == "Bug 6b transform" && !store.projectOperationBusy }
        compose.runOnIdle { store.addShape(1) }
        compose.waitUntil(10_000) { store.detail != null }

        // X: um toque abre o teclado; "5", "0" e OK gravam 50 px.
        waitForTag("transform.position.x")
        compose.onNodeWithTag("transform.position.x").fingerTap()
        waitForTag("numeric.keypad.confirm")
        compose.onNodeWithText("5").fingerTap(80)
        compose.onNodeWithText("0").fingerTap(80)
        compose.onNodeWithTag("numeric.keypad.confirm").assertIsDisplayed().fingerTap(80)
        compose.waitUntil(5_000) { abs((store.detail?.position?.get(0) ?: 0f) - 50f) < 1e-3f }
        waitGone("numeric.keypad.confirm")

        // Girar (2ª aba) e a roda: o toque parado de 180 ms (antes perdido: só valia até 100 ms).
        compose.onNodeWithTag("panel.tab.1").fingerTap(80)
        waitForTag("transform.rotation.dial")
        compose.onNodeWithTag("transform.rotation.dial").fingerTap(180)
        waitForTag("numeric.keypad.confirm")
        compose.onNodeWithText("4").fingerTap(80)
        compose.onNodeWithText("5").fingerTap(80)
        compose.onNodeWithTag("numeric.keypad.confirm").fingerTap(80)
        compose.waitUntil(5_000) { abs((store.detail?.rotation?.get(2) ?: 0f) - 45f) < 1e-3f }
    }
}
