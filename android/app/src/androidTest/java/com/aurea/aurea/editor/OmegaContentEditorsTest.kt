package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

/**
 * Omega conteúdo (área 06): o editor de desenho, o Editar pontos, a barra da
 * seção e o toque duplo no palco. Par dos testes de conteúdo do
 * `AureaStageGestureUITests` do iOS (desenho, Editar pontos, toque duplo).
 */
class OmegaContentEditorsTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext

    private fun launch(title: String) {
        assertTrue(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(1080, 1920, 30f, title) }
        compose.waitUntil(15000) { store.project.title == title }
    }

    private fun groups() = store.vectorDoc?.groups?.size ?: 0

    @Test fun freehandOpensTheDrawingEditorAndEachStrokeIsOneUndo() {
        launch("Drawing test")
        compose.runOnIdle { store.chooseVectorTool(2) }
        compose.waitUntil(5000) { store.isDrawingLayer }
        compose.onNodeWithTag("draw.panel").assertIsDisplayed()
        compose.onNodeWithTag("draw.tool.eraser").performClick()
        compose.waitUntil(2000) { ContentPrefs.drawTool == 3 }
        compose.onNodeWithTag("draw.tool.pen").performClick()
        compose.waitUntil(2000) { ContentPrefs.drawTool == 0 }
        val before = groups()
        compose.onNodeWithTag("editor.stage").performTouchInput {
            down(center)
            moveBy(Offset(80f, 40f))
            moveBy(Offset(80f, 60f))
            up()
        }
        compose.waitUntil(5000) { groups() == before + 1 }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { groups() == before }
    }

    @Test fun drawWithPointsOpensEditPointsAndTheTrackpadPlacesPoints() {
        launch("Points test")
        compose.runOnIdle { store.addVectorLayer(0) }
        compose.onNodeWithTag("points.panel").assertIsDisplayed()
        // Contorno vazio: arrastar posiciona o ponto pendente; tocar fixa.
        repeat(2) { n ->
            compose.onNodeWithTag("points.trackpad").performTouchInput { down(center); moveBy(Offset(60f + 40f * n, 20f)); up() }
            compose.onNodeWithTag("points.trackpad").performClick()
            compose.waitUntil(5000) { (store.vectorPathAt?.path?.v?.size ?: 0) == n + 1 }
        }
        // Canto ↔ curva: as alças aparecem e somem.
        compose.runOnIdle { EditPointsState.choosePoint(store, 1) }
        compose.onNodeWithTag("points.type").performClick()
        compose.waitUntil(5000) { store.vectorPathAt?.path?.v?.get(1)?.let { it.inX != 0f || it.outX != 0f } == true }
        compose.onNodeWithTag("points.type").performClick()
        compose.waitUntil(5000) { store.vectorPathAt?.path?.v?.get(1)?.let { it.inX == 0f && it.outX == 0f && it.inY == 0f && it.outY == 0f } == true }
        // O segmento ◆ da barra da seção troca a coluna para keyframes.
        compose.onNodeWithTag("points.mode.keys").performClick()
        compose.onNodeWithTag("points.key").assertIsDisplayed()
        compose.onNodeWithTag("points.mode.points").performClick()
        compose.onNodeWithTag("points.contours").assertIsDisplayed()
    }

    @Test fun doubleTapOnTheSelectedTextOpensTheTextMode() {
        launch("Double tap test")
        compose.runOnIdle { store.addText(); store.dismissTextContentEditor() }
        compose.waitUntil(5000) { store.textDetail != null && store.textEditLayer == null }
        compose.onNodeWithTag("editor.stage").performTouchInput { doubleClick(center) }
        compose.waitUntil(5000) { store.textEditLayer != null }
        compose.onNodeWithTag("text.edit.mode").assertIsDisplayed()
        compose.onNodeWithTag("text.style.done").performClick()
        compose.waitUntil(5000) { store.textEditLayer == null }
    }

    @Test fun shapeBarKeysPersistAndResizeFromTheEdge() {
        launch("Shape bar test")
        compose.runOnIdle { store.addShape(0) }
        compose.waitUntil(5000) { store.detail != null }
        compose.runOnIdle { ContentPrefs.setFromCenter(true); ContentPrefs.setLockAspect(false) }
        compose.runOnIdle { ContentPrefs.load(context) }
        compose.onNodeWithTag("layer.panel.edit").performClick()
        compose.onNodeWithTag("shape.fromCenter").performClick()
        compose.waitUntil(2000) { !ContentPrefs.fromCenter }
        compose.onNodeWithTag("shape.lockAspect").performClick()
        compose.waitUntil(2000) { ContentPrefs.lockAspect }
        val x0 = store.detail!!.position[0]
        compose.runOnIdle { store.resizeShapeHandle(5, 40f, 0f, keepRatio = false, fromCenter = false, continuing = false) }
        compose.waitUntil(5000) { store.detail!!.position[0] != x0 }
        compose.runOnIdle { ContentPrefs.setFromCenter(true); ContentPrefs.setLockAspect(false) }
    }
}
