package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import kotlin.math.abs

/**
 * Pontos do vetor 3D no palco (EngineVector3D.cpp): numa camada vetorial 3D
 * girada, o ponto arrastado em px da composição anda no PLANO da camada (fica
 * sob o dedo em perspectiva), o caminho muda e um desfazer volta tudo. Tags
 * iguais às do iOS (EditPointsView.swift): points.trackpad, points.plane3d.
 */
class Vector3DPointsDeviceTest {
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

    /** Camada vetorial com 4 pontos (o 2º com alças), 3D e girada em X/Y. */
    private fun rotatedShape(): Long {
        var id = -1L
        compose.runOnIdle {
            id = store.addVectorLayer(0)
            assertTrue(id > 0)
            for (p in listOf(floatArrayOf(400f, 800f, 0f), floatArrayOf(700f, 800f, 0f), floatArrayOf(700f, 1100f, 0f), floatArrayOf(400f, 1100f, 0f))) {
                assertTrue(store.editVectorPath(PathOps.APPEND, p, false) >= 0)
            }
            store.editVectorPath(PathOps.SET_TYPE, floatArrayOf(1f, 1f), false)
            store.changeLayer3D(true)
            store.setTransform(TrackProperty.ROTATION_Y, 40f, id)
            store.setTransform(TrackProperty.ROTATION_X, -20f, id)
        }
        compose.waitUntil(10000) {
            val d = store.detail
            d != null && abs(d.rotation[1] - 40f) < 0.01f && abs(d.rotation[0] + 20f) < 0.01f
        }
        compose.runOnIdle { store.selectVectorPath(0, 0) }
        compose.waitUntil(10000) { store.vectorPathAt?.perspective == true && (store.vectorPathAt?.path?.v?.size ?: 0) == 4 }
        return id
    }

    /** O ponto [i] no palco (px da composição) e se ele está na frente da câmera. */
    private fun stagePoint(i: Int): Triple<Float, Float, Boolean> {
        val s = store.vectorStagePoints()!!
        assertEquals(1f, s[1], 0f)   // 3D
        val o = 3 + i * 9
        return Triple(s[o], s[o + 1], s[o + 2] > 0.5f)
    }

    @Test fun dragMovesTheVertexOnTheRotatedPlaneAndUndoRestoresIt() {
        launch("Vector 3D points")
        rotatedShape()
        var before = 0f to 0f
        var target = 0f to 0f
        compose.runOnIdle {
            val v0 = store.vectorPathAt!!.path.v[0]
            before = v0.x to v0.y
            val (x0, y0, visible) = stagePoint(0)
            assertTrue(visible)
            target = (x0 + 60f) to (y0 + 35f)
            // Um arrasto em dois passos: o 2º continua o mesmo passo de desfazer.
            assertTrue(store.dragVectorPathOnStage(0, intArrayOf(0), x0, y0, x0 + 25f, y0 + 10f, 0, false) >= 0)
            assertTrue(store.dragVectorPathOnStage(0, intArrayOf(0), x0 + 25f, y0 + 10f, target.first, target.second, 0, true) >= 0)
        }
        compose.waitUntil(5000) { store.vectorPathAt!!.path.v[0].let { abs(it.x - before.first) > 1f || abs(it.y - before.second) > 1f } }
        compose.runOnIdle {
            // O ponto ficou sob o "dedo", em perspectiva; o vizinho não andou.
            val (x, y, _) = stagePoint(0)
            assertEquals(target.first, x, 0.5f)
            assertEquals(target.second, y, 0.5f)
            val v = store.vectorPathAt!!.path.v
            assertEquals(700f, v[1].x, 0.01f)
            assertEquals(800f, v[1].y, 0.01f)
            // A homografia que o palco desenha bate com a conta do motor.
            val q = FloatArray(2)
            store.vectorPathAt!!.toComp(v[0].x, v[0].y, q)
            assertEquals(target.first, q[0], 0.5f)
            assertEquals(target.second, q[1], 0.5f)
            store.undo()
        }
        compose.waitUntil(5000) { store.vectorPathAt!!.path.v[0].let { abs(it.x - before.first) < 0.01f && abs(it.y - before.second) < 0.01f } }
    }

    @Test fun handleDragKeepsTheAngleLockOnThePlane() {
        launch("Vector 3D handle")
        rotatedShape()
        compose.runOnIdle {
            val s = store.vectorStagePoints()!!
            val o = 3 + 1 * 9 + 6   // alça de saída do ponto 1
            assertTrue(s[o + 2] > 0.5f)
            assertTrue(store.dragVectorPathOnStage(2, intArrayOf(1), s[o], s[o + 1], s[o] + 30f, s[o + 1] - 20f, 1, false) >= 0)
            val p = store.vectorPathAt!!.path.v[1]
            // Trava de ângulo: a alça de entrada continua na mesma reta, do outro lado.
            assertEquals(0f, p.inX * p.outY - p.inY * p.outX, 0.05f * kotlin.math.hypot(p.inX, p.inY) * kotlin.math.hypot(p.outX, p.outY) + 1e-3f)
            assertTrue(p.inX * p.outX + p.inY * p.outY < 0f)
        }
    }

    @Test fun pointsPanelShowsThePlaneHintOnA3DLayer() {
        launch("Vector 3D hint")
        rotatedShape()
        // A camada "Desenho vetorial" abre o Editar pontos; no 3D o trackpad avisa do plano.
        compose.waitUntil(5000) { compose.onAllNodesWithTagCompat("points.trackpad").isNotEmpty() }
        compose.waitUntil(5000) { compose.onAllNodesWithTagCompat("points.plane3d").isNotEmpty() }
    }

    private fun androidx.compose.ui.test.junit4.ComposeContentTestRule.onAllNodesWithTagCompat(tag: String) =
        onAllNodes(androidx.compose.ui.test.hasTestTag(tag), useUnmergedTree = true).fetchSemanticsNodes()
}
