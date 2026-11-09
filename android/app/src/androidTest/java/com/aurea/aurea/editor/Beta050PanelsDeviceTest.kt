package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onFirst
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTextReplacement
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.CurvePanel
import com.aurea.aurea.editor.panels.PanelEnv
import com.aurea.aurea.editor.panels.TextAnimSection
import com.aurea.aurea.editor.panels.easeOf
import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import java.io.File
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

/** 0.5.0: a seção de agrupamento e o diálogo de velocidade e influência, tocados na tela. */
class Beta050PanelsDeviceTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore

    private fun waitTag(tag: String) = compose.waitUntil(8000) { compose.onAllNodesWithTag(tag, useUnmergedTree = true).fetchSemanticsNodes().isNotEmpty() }

    private fun shot(name: String) {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        compose.waitForIdle()
        Thread.sleep(300)
        val folder = File(instrumentation.targetContext.getExternalFilesDir(null), "ui050").apply { mkdirs() }
        val bitmap = instrumentation.uiAutomation.takeScreenshot()
        File(folder, name).outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        bitmap.recycle()
    }

    private fun key(id: Long, frame: Int, value: Float) {
        compose.runOnIdle { store.pause(); store.seek(frame) }
        compose.waitUntil(5000) { store.playhead == frame }
        Thread.sleep(250)
        compose.runOnIdle {
            assertTrue(store.engineForStress.toggleTextAnimKey(id, 0, 0))
            assertTrue(store.engineForStress.setTextAnimParam(id, 0, 0, value))
        }
    }

    @Test fun groupingSectionAndSpeedInfluenceDialogWorkByTouch() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        var ready = false
        var screen by mutableIntStateOf(0)
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            val env = remember(store) { PanelEnv(store, {}, {}, {}, {}, {}, { null }) }
            AureaTheme {
                Box(Modifier.fillMaxSize()) {
                    if (screen == 0) Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) { TextAnimSection(env) }
                    else CurvePanel(env)
                }
            }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(640, 360, 30f, "Paineis 050") }
        compose.waitUntil(10000) { store.project.title == "Paineis 050" && !store.projectOperationBusy }
        val engine = store.engineForStress
        val id = engine.addText("Painel")
        assertTrue(id != 0L)
        compose.runOnIdle { store.select(id, openOptions = false) }
        compose.waitUntil(5000) { store.primary == id }
        compose.runOnIdle { store.addTextAnimator(2) }

        // Agrupamento: abre a seção, troca para Tudo e confere os dois campos de alinhamento.
        waitTag("anim.item.0")
        compose.onNodeWithTag("anim.item.0", useUnmergedTree = true).performClick()
        waitTag("anim.0.sec.grouping")
        compose.onNodeWithTag("anim.0.sec.grouping", useUnmergedTree = true).performScrollTo().performClick()
        waitTag("anim.0.key.56")
        waitTag("anim.0.key.57")
        compose.onAllNodesWithText(context.getString(R.string.edt_cmd_all)).onFirst().performScrollTo().performClick()
        compose.waitUntil(5000) { engine.textAnimatorFields(id, 0)!![3] == 3f }
        shot("agrupamento.png")

        // Velocidade e influência: influência de saída em 50 % precisa virar x1 = 0,5.
        key(id, 0, 0f)
        key(id, 30, 100f)
        val start = KeyframeRow(TrackProperty.TEXT_ANIM_PARAM, 0, 0, 0f, 2, 0)
        compose.runOnIdle {
            store.setKeyframeEasing(id, start, 2, 0.3333f, 0f, 0.6667f, 1f, 1, linkAxes = true)
            store.selectKeyframe(id, start)
            screen = 1
        }
        waitTag("curve.menu")
        compose.onNodeWithTag("curve.menu", useUnmergedTree = true).performClick()
        waitTag("curve.temporal")
        compose.onNodeWithTag("curve.temporal", useUnmergedTree = true).performScrollTo().performClick()
        Thread.sleep(1200)
        shot("dialogo-aberto.png")
        compose.waitUntil(8000) { compose.onAllNodes(hasSetTextAction(), useUnmergedTree = true).fetchSemanticsNodes().size >= 4 }
        shot("velocidade-influencia.png")
        compose.onAllNodes(hasSetTextAction(), useUnmergedTree = true)[1].performTextReplacement("50")
        waitTag("dialog.confirm")
        compose.onNodeWithTag("dialog.confirm", useUnmergedTree = true).performClick()
        compose.waitUntil(5000) { kotlin.math.abs(easeOf(store, id, start).x1 - 0.5f) < 0.001f }
        val ease = easeOf(store, id, start)
        assertEquals(0f, ease.y1, 1e-3f)
        assertEquals(0.6667f, ease.x2, 1e-3f)
        shot("curva-aplicada.png")
    }
}
