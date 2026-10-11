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
import com.aurea.aurea.state.GIZMO_ROTATE
import com.aurea.aurea.state.GIZMO_SCALE
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import kotlin.math.abs
import kotlin.math.hypot

/**
 * Girar/escala pelo gizmo 3D com toque real: cada alça mexe só no SEU eixo,
 * posição nunca muda, o centro da escala é uniforme e cada gesto é um passo
 * de desfazer.
 */
class GizmoToolsTest {
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
            AureaTheme { PreviewStage(store, ui, Modifier.fillMaxWidth().height(320.dp).testTag("gizmo.stage")) }
        }
        compose.waitUntil(30000) { initialized && store.engineReady }
        compose.runOnIdle { store.newProject(320, 240, 30f, title) }
        compose.waitUntil(15000) { store.project.title == title }
        compose.runOnIdle { store.addNull(true) }
        compose.waitUntil(5000) { store.gizmo != null && store.detail != null }
    }

    /** Pontas na tela: [ox, oy, Xx, Xy, Yx, Yy, Zx, Zy]. */
    private fun TouchInjectionScope.tips(): FloatArray {
        val density = InstrumentationRegistry.getInstrumentation().targetContext.resources.displayMetrics.density
        val raw = store.gizmo!!.copyOf()
        val mapper = StageMapper().apply { update(width.toFloat(), height.toFloat(), 0f, 320, 240, false) }
        val screen = FloatArray(8) { i -> if (i % 2 == 0) mapper.sx(raw[i]) else mapper.sy(raw[i]) }
        return GizmoGeometry.tips(screen, density)
    }

    /** Detalhe depois do ultimo passo do gesto: dois refreshes seguidos iguais. */
    private fun settled(): com.aurea.aurea.engine.LayerDetail {
        var last = store.detail!!
        compose.waitUntil(5000) {
                Thread.sleep(150)
                val now = store.detail!!
                val same = now.scale == last.scale && now.rotation == last.rotation && now.position == last.position
                last = now
                same
            }
        return last
    }

    private fun selectTool(tool: Int) {
        compose.runOnIdle { while (store.gizmoTool != tool) store.cycleGizmoTool() }
        compose.waitForIdle()
    }

    @Test fun rotateHandleChangesOnlyItsAxisAndOneUndoRestoresIt() {
        openNull("Gizmo rotate")
        selectTool(GIZMO_ROTATE)
        val before = store.detail!!
        lateinit var orientation: FloatArray
        compose.runOnIdle { orientation = checkNotNull(store.queryTrackball(store.primary!!)) }
        compose.onNodeWithTag("gizmo.stage").performTouchInput {
            val density = InstrumentationRegistry.getInstrumentation().targetContext.resources.displayMetrics.density
            val mapper = StageMapper().apply { update(width.toFloat(), height.toFloat(), 0f, 320, 240, false) }
            val center = Offset(mapper.sx(orientation[0]), mapper.sy(orientation[1]))
            val radius = 62f * density
            // At the initial orientation the X ring projects to a vertical line.
            // Its lower front segment is away from both the Y and Z rings.
            val offset = Offset(0f, radius * .6f)
            assertEquals(0, com.aurea.aurea.engine.AureaEngine.trackballHit(
                orientation.copyOfRange(2, 11), offset.x, offset.y, radius, 14f * density))
            down(center + offset)
            moveBy(Offset(0f, -20f * density), 80)
            moveBy(Offset(0f, -20f * density), 80)
            up()
        }
        compose.waitUntil(5000) { store.detail!!.rotation[0] != before.rotation[0] }
        val after = settled()
        assertEquals(before.rotation[1], after.rotation[1])
        assertEquals(before.rotation[2], after.rotation[2])
        assertEquals("Girar nunca move a camada", before.position, after.position)
        assertEquals(before.scale, after.scale)
        assertTrue("60 px transversais devem girar vários graus", abs(after.rotation[0] - before.rotation[0]) > 5f)
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.detail!!.rotation == before.rotation }
    }

    @Test fun scaleHandlesArePerAxisCenterIsUniformAndTapDoesNothing() {
        openNull("Gizmo scale")
        selectTool(GIZMO_SCALE)
        val before = store.detail!!
        // Toque parado no quadrado central: nenhuma edição.
        compose.onNodeWithTag("gizmo.stage").performTouchInput {
            val t = tips()
            down(Offset(t[0], t[1]))
            up()
        }
        compose.waitForIdle()
        assertEquals(before.scale, store.detail!!.scale)
        // Alça Y, ao longo dela: só Escala Y cresce.
        compose.onNodeWithTag("gizmo.stage").performTouchInput {
            val t = tips()
            val hx = t[4] - t[0]
            val hy = t[5] - t[1]
            val l = hypot(hx, hy)
            down(Offset(t[4], t[5]))
            moveBy(Offset(hx / l * 25f, hy / l * 25f), 60)
            moveBy(Offset(hx / l * 25f, hy / l * 25f), 60)
            up()
        }
        compose.waitUntil(5000) { store.detail!!.scale[1] != before.scale[1] }
        val y = settled()
        assertTrue("Arrastar para fora da alça aumenta", y.scale[1] > before.scale[1])
        assertEquals(before.scale[0], y.scale[0])
        assertEquals(before.scale[2], y.scale[2])
        assertEquals(before.position, y.position)
        // Centro: X, Y e profundidade efetiva pelo mesmo fator; Z gravado é relativo a X.
        compose.onNodeWithTag("gizmo.stage").performTouchInput {
            val t = tips()
            down(Offset(t[0], t[1]))
            moveBy(Offset(20f, -20f), 60)
            moveBy(Offset(20f, -20f), 60)
            up()
        }
        compose.waitUntil(5000) { store.detail!!.scale[0] != y.scale[0] }
        val u = settled()
        val f = u.scale[0] / y.scale[0]
        assertTrue(f > 1f)
        assertEquals(f, u.scale[1] / y.scale[1], 0.001f)
        assertEquals(y.scale[2], u.scale[2], 0.001f)
        assertEquals(f, (u.scale[0] * u.scale[2]) / (y.scale[0] * y.scale[2]), 0.001f)
        assertEquals(before.position, u.position)
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.detail!!.scale == y.scale }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.detail!!.scale == before.scale }
    }
}
