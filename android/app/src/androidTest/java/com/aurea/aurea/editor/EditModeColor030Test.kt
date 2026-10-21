package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipeLeft
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.Grade
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.state.EditorStore
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
 * 0.3.0: Efeitos, Filtros e Ajustar do modo de edição têm cada um a sua tela
 * (antes os três abriam o mesmo editor de efeitos). Filtros e Ajustar valem
 * para o clipe escolhido ou, no nível de cima, para a camada de ajuste.
 */
class EditModeColor030Test {
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

    private fun importSolid(name: String, r: Int, g: Int, b: Int): Long {
        val w = 64; val h = 36
        val buf = ByteBuffer.allocateDirect(w * h * 4).order(ByteOrder.nativeOrder())
        repeat(w * h) { buf.put(r.toByte()); buf.put(g.toByte()); buf.put(b.toByte()); buf.put(255.toByte()) }
        buf.rewind()
        return store.engineForStress.importImage(buf, w, h, name, "test://$name")
    }

    /** O ‹ do cabeçalho do painel (o modo de edição mostra o cabeçalho próprio). */
    private fun closePanel() {
        compose.onNodeWithContentDescription(context.getString(R.string.pn_back_to_layer_tools)).performClick()
        compose.waitForIdle()
    }

    /** O valor do parâmetro [index] da Correção de cor de papel [role] na camada escolhida. */
    private fun gradeValue(role: Int, index: Int): Float? {
        val grade = effectTypeId(Grade.KEY)
        val fx = store.effects.filter { it.typeId == grade }.firstOrNull { e ->
            store.effectParams[e.effectId]?.firstOrNull { it.index == Grade.ROLE }?.value?.getOrNull(0)?.toInt() == role
        } ?: return null
        return store.effectParams[fx.effectId]?.firstOrNull { it.index == index }?.value?.getOrNull(0)
    }

    @Test fun filtersAndAdjustHaveTheirOwnScreensForTheProjectAndTheClip() {
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(720, 1280, 30.0, "AUREA · Cor", null, UI_MODE_EDIT) }
        compose.waitUntil(15000) { store.project.title == "AUREA · Cor" && !store.projectOperationBusy }
        compose.waitUntil(10000) { store.uiMode == UI_MODE_EDIT }
        compose.runOnIdle { assertTrue(importSolid("A", 220, 60, 60) >= 0) }
        compose.waitUntil(10000) { store.layers.size == 1 && store.mainTrackAnchor != 0L }
        compose.runOnIdle { store.seek(10) }

        // FILTROS do projeto: a camada de ajuste e a tela dos looks (não o editor de efeitos).
        compose.onNodeWithTag("edit.dock.items").performScrollToNode(hasTestTag("edit.dock.filters"))
        compose.onNodeWithTag("edit.dock.filters").performClick()
        compose.waitUntil(8000) { compose.onAllNodesWithTag("grade.filters").fetchSemanticsNodes().isNotEmpty() }
        compose.runOnIdle { assertTrue(store.layers.any { it.adjustment && it.id == store.primary }) }
        compose.onNodeWithTag("grade.filters").performScrollToNode(hasTestTag("grade.look.4"))
        compose.onNodeWithTag("grade.look.4").performClick()   // P&B
        compose.waitUntil(8000) { gradeValue(Grade.ROLE_FILTER, Grade.LOOK) == 4f }
        compose.runOnIdle { assertEquals(0f, gradeValue(Grade.ROLE_FILTER, Grade.SATURATION)) }
        compose.waitUntil(8000) { compose.onAllNodesWithTag("grade.filter.intensity").fetchSemanticsNodes().isNotEmpty() }
        capture("color030-filters-project")
        closePanel()

        // AJUSTAR do projeto: a MESMA camada de ajuste, outra instância (o filtro fica).
        compose.waitUntil(5000) { compose.onAllNodesWithTag("edit.dock.items").fetchSemanticsNodes().isNotEmpty() }
        compose.runOnIdle { store.clearSelection() }
        compose.waitUntil(5000) { compose.onAllNodesWithTag("edit.dock.adjust").fetchSemanticsNodes().isNotEmpty() ||
            compose.onAllNodesWithTag("edit.dock.items").fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithTag("edit.dock.items").performScrollToNode(hasTestTag("edit.dock.adjust"))
        compose.onNodeWithTag("edit.dock.adjust").performClick()
        compose.waitUntil(8000) { compose.onAllNodesWithTag("grade.tools").fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithTag("grade.tools").performScrollToNode(hasTestTag("grade.tool.${Grade.TEMPERATURE}"))
        compose.onNodeWithTag("grade.tool.${Grade.TEMPERATURE}").performClick()
        compose.waitUntil(5000) { compose.onAllNodesWithTag("grade.slider.${Grade.TEMPERATURE}").fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithTag("grade.slider.${Grade.TEMPERATURE}").performTouchInput { swipeLeft() }
        compose.waitUntil(8000) { (gradeValue(Grade.ROLE_ADJUST, Grade.TEMPERATURE) ?: 0f) < -5f }
        compose.runOnIdle { assertEquals(4f, gradeValue(Grade.ROLE_FILTER, Grade.LOOK)) }
        // As abas: HSL, Rodas e scopes abrem.
        compose.onNodeWithText(context.getString(R.string.grade_tab_hsl)).performClick()
        compose.waitUntil(5000) { compose.onAllNodesWithTag("grade.band.0").fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithText(context.getString(R.string.grade_tab_wheels)).performClick()
        compose.waitUntil(5000) { compose.onAllNodesWithTag("grade.wheel.${Grade.LIFT}").fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithTag("grade.scopes").performClick()
        compose.waitUntil(5000) { compose.onAllNodesWithTag("grade.scope.view").fetchSemanticsNodes().isNotEmpty() }
        capture("color030-adjust-project")
        closePanel()

        // No CLIPE: Filtros do dock do corte valem só para ele.
        compose.runOnIdle { store.select(store.layers.first { it.name == "A" }.id) }
        // O Dock do corte (o ‹ só existe fora do nível de cima); "Dividir" fica depois de
        // Filtros/Ajustar e pode estar fora da tela da fileira.
        compose.waitUntil(5000) { compose.onAllNodesWithTag("edit.dock.back").fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithTag("edit.dock.items").performScrollToNode(hasTestTag("edit.dock.filters"))
        compose.onNodeWithTag("edit.dock.filters").performClick()
        compose.waitUntil(8000) { compose.onAllNodesWithTag("grade.look.1").fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithTag("grade.look.1").performClick()   // Quente
        compose.waitUntil(8000) { gradeValue(Grade.ROLE_FILTER, Grade.LOOK) == 1f }
        compose.runOnIdle {
            val clip = store.layers.first { it.name == "A" }
            assertEquals(clip.id, store.primary)
            assertTrue((gradeValue(Grade.ROLE_FILTER, Grade.TEMPERATURE) ?: 0f) > 10f)
        }
        capture("color030-filters-clip")
        closePanel()

        // EFEITOS do projeto: o navegador abre direto.
        compose.runOnIdle { store.clearSelection() }
        compose.waitUntil(5000) { compose.onAllNodesWithTag("edit.dock.items").fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithTag("edit.dock.items").performScrollToNode(hasTestTag("edit.dock.effects"))
        compose.onNodeWithTag("edit.dock.effects").performClick()
        compose.waitUntil(8000) { compose.onAllNodesWithTag("effects.home").fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithTag("effects.home").assertIsDisplayed()
        capture("color030-effects-browser")
    }
}
