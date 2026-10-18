package com.aurea.aurea.editor

import android.app.Application
import android.content.pm.ActivityInfo
import android.content.res.Configuration
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry
import androidx.test.runner.lifecycle.Stage
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.editor.panels.PanelContent
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

/** Native model and real pointer gestures in the compact XYZ transformation rows. */
class TransformVectorRowsDeviceTest {
    @get:Rule val compose = createComposeRule()
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private lateinit var store: EditorStore
    private fun initialize(): Long {
        check(context.packageName.endsWith(".uitest"))
        if (InstrumentationRegistry.getArguments().getString("beta3.orientation") == "landscape") {
            InstrumentationRegistry.getInstrumentation().runOnMainSync {
                ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).single()
                    .requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
            }
            compose.waitUntil(10000) { context.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE }
        }
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            val ui = remember { EditorUi() }
            val panelHeight = if (LocalConfiguration.current.screenHeightDp < 500) 210.dp else 280.dp
            AureaTheme {
                Column(Modifier.fillMaxSize()) {
                    PreviewStage(store, ui, Modifier.fillMaxWidth().weight(1f))
                    PanelContent(store, EditorPanel.Transform, {}, {}, {}, Modifier.height(panelHeight))
                }
            }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(1080, 1080, 30f, "Beta3 XYZ ruler") }
        compose.waitUntil(15000) { store.project.title == "Beta3 XYZ ruler" && !store.projectOperationBusy }
        compose.runOnIdle { store.addShape(1) }
        compose.waitUntil(10000) { store.detail != null }
        compose.runOnIdle { store.enableLayer3D() }
        compose.waitUntil(10000) { store.layers.single().isThreeD }
        return checkNotNull(store.primary)
    }
    private fun face(label: Int) = compose.onNodeWithContentDescription(context.getString(label)).performScrollTo().performClick()
    private fun scrub(tag: String) = compose.onNodeWithTag(tag).performTouchInput {
        down(center)
        moveBy(Offset(-48f, 0f), 16)
        moveBy(Offset(-12f, 0f), 16)
        up()
    }
    private fun screenshot(face: String) {
        compose.waitForIdle()
        val config = context.resources.configuration
        stabilityScreenshot("beta3-xyz-${config.screenWidthDp}x${config.screenHeightDp}dp-$face.png")
    }
    private fun assertFocusedRulerGeometry(tag: String) {
        val control = compose.onNodeWithTag(tag).fetchSemanticsNode().boundsInRoot
        val ruler = compose.onNodeWithTag("$tag.ruler").fetchSemanticsNode().boundsInRoot
        val axes = (0..2).map { compose.onNodeWithTag("$tag.axis.$it").fetchSemanticsNode().boundsInRoot }
        val density = context.resources.displayMetrics.density
        assertTrue("The ruler belongs below the XYZ values: ruler=$ruler axes=$axes", ruler.top >= axes.maxOf { it.bottom } + 4f * density)
        assertTrue("The ruler must use the focused face width: ruler=$ruler control=$control", ruler.width >= control.width * .8f)
        assertTrue("The ruler keeps a usable drag target: $ruler", ruler.height >= 44f * density - 1f)
        compose.onNodeWithTag("$tag.ruler").performScrollTo().assertIsDisplayed()
        compose.onNodeWithTag("$tag.axis.0").performScrollTo().assertIsDisplayed()
    }

    @Test fun rotationAxisDragKeepsOtherAxesAndTurnsAndOneUndo() {
        val layer = initialize()
        compose.runOnIdle {
            store.setTransform(TrackProperty.ROTATION_X, 10f)
            store.setTransform(TrackProperty.ROTATION_Y, 20f)
            store.setTransform(TrackProperty.ROTATION_Z, 750f)
        }
        compose.waitUntil(10000) { store.detail?.rotation?.get(2) == 750f }
        face(R.string.panel_rotacao)
        assertFocusedRulerGeometry("transform.rotation")
        for (axis in 0..2) compose.onNodeWithTag("transform.rotation.axis.$axis").assertIsDisplayed()
        compose.onNodeWithTag("transform.rotation.axis.0").performClick()
        assertEquals(10f, store.detail!!.rotation[0], .001f)
        screenshot("rotation-before")
        scrub("transform.rotation.ruler")
        compose.waitUntil(10000) { store.detail!!.rotation[0] > 15f }
        assertEquals(20f, store.detail!!.rotation[1], .001f)
        assertEquals("Full turns must survive an edit to another axis", 750f, store.detail!!.rotation[2], .001f)
        compose.runOnIdle { store.undo() }
        compose.waitUntil(10000) { kotlin.math.abs(store.detail!!.rotation[0] - 10f) < .001f }
        compose.onNodeWithTag("transform.rotation.axis.0").performClick()
        compose.onNodeWithText("2").performScrollTo().assertIsDisplayed().performClick()
        compose.onNodeWithText("5").performScrollTo().assertIsDisplayed().performClick()
        screenshot("rotation-keypad")
        compose.onNodeWithText("OK").assertIsDisplayed().performClick()
        compose.waitUntil(10000) { kotlin.math.abs(store.detail!!.rotation[0] - 25f) < .001f }
        assertEquals(20f, store.detail!!.rotation[1], .001f)
        assertEquals(750f, store.detail!!.rotation[2], .001f)
        compose.runOnIdle { store.undo() }
        compose.waitUntil(10000) { kotlin.math.abs(store.detail!!.rotation[0] - 10f) < .001f }
        compose.onNodeWithContentDescription(context.getString(R.string.panel_marcar_keyframe_aqui)).performClick()
        compose.waitUntil(10000) { store.keyframes[layer].orEmpty().filter { it.time == 0 }.map { it.property }.toSet().containsAll(listOf(6, 7, 8)) }
        screenshot("rotation-keyed")
        compose.runOnIdle { assertTrue(store.engineForStress.setExpression(layer, listOf(TrackKey(6)), "15")?.ok == true) }
        compose.waitUntil(10000) { store.expressions.any { it.enabled } }
        scrub("transform.rotation.ruler")
        assertTrue("Scrubbing must retain the enabled expression", store.expressions.any { it.enabled && it.key == TrackKey(6) })
        screenshot("rotation-expression")
    }

    @Test fun positionPivotAndLinkedScaleKeepTheAuthoredAxesAndUndo() {
        initialize()
        compose.runOnIdle { store.setTransform(TrackProperty.POSITION_Z, 30f) }
        compose.waitUntil(10000) { store.detail?.position?.get(2) == 30f }
        compose.onNodeWithTag("transform.position.axis.2").performClick()
        assertFocusedRulerGeometry("transform.position")
        val position = store.detail!!.position.toFloatArray()
        scrub("transform.position.ruler")
        compose.waitUntil(10000) { store.detail!!.position[2] > position[2] + 10f }
        assertEquals(position[0], store.detail!!.position[0], .001f)
        assertEquals(position[1], store.detail!!.position[1], .001f)
        screenshot("position")
        compose.runOnIdle { store.undo() }
        compose.waitUntil(10000) { kotlin.math.abs(store.detail!!.position[2] - position[2]) < .001f }

        face(R.string.edt_pivot_rail)
        val anchor = store.detail!!.anchor.toFloatArray()
        compose.onNodeWithTag("transform.pivot.axis.2").performClick()
        scrub("transform.pivot.ruler")
        compose.waitUntil(10000) { store.detail!!.anchor[2] > anchor[2] + 10f }
        assertEquals(anchor[0], store.detail!!.anchor[0], .001f)
        assertEquals(anchor[1], store.detail!!.anchor[1], .001f)
        assertArrayEquals("Numeric pivot edits retain their existing position semantics", position, store.detail!!.position.toFloatArray(), .001f)
        screenshot("pivot")
        compose.runOnIdle { store.undo() }
        compose.waitUntil(10000) { kotlin.math.abs(store.detail!!.anchor[2] - anchor[2]) < .001f }

        face(R.string.panel_escala)
        compose.runOnIdle { store.setScale3(floatArrayOf(2f, 1f, 4f)); store.scaleAxesLinked = true }
        compose.waitUntil(10000) { kotlin.math.abs(store.detail!!.scale[0] - 2f) < .001f }
        scrub("transform.scale.ruler")
        compose.waitUntil(10000) { store.detail!!.scale[0] > 2.05f }
        assertEquals("Linked scale must retain the original X/Y ratio", 2f, store.detail!!.scale[0] / store.detail!!.scale[1], .001f)
        assertEquals("Content depth follows X without multiplying stored Z again", 4f, store.detail!!.scale[2], .001f)
        screenshot("scale-linked")
        compose.runOnIdle { store.undo() }
        compose.waitUntil(10000) { kotlin.math.abs(store.detail!!.scale[0] - 2f) < .001f }
        compose.onNodeWithTag("transform.scale.link").performClick()
        compose.onNodeWithTag("transform.scale.axis.1").performClick()
        scrub("transform.scale.ruler")
        compose.waitUntil(10000) { store.detail!!.scale[1] > 1.05f }
        assertEquals(2f, store.detail!!.scale[0], .001f)
        assertEquals(4f, store.detail!!.scale[2], .001f)
        screenshot("scale-unlinked")
    }

    @Test fun switchingLayerDuringARulerDragClosesTheOldGesture() {
        val first = initialize()
        compose.runOnIdle { store.addShape(1) }
        compose.waitUntil(10000) { store.layers.size == 2 && store.primary != first }
        val second = checkNotNull(store.primary)
        compose.runOnIdle { store.enableLayer3D() }
        compose.waitUntil(10000) { store.layers.single { it.id == second }.isThreeD }
        val secondPosition = store.detail!!.position.toFloatArray()
        compose.runOnIdle { store.select(first) }
        compose.waitUntil(10000) { store.primary == first && store.detail != null }
        val firstX = store.detail!!.position[0]
        compose.onNodeWithTag("transform.position.ruler").performTouchInput {
            down(center)
            moveBy(Offset(-48f, 0f), 16)
        }
        compose.waitUntil(10000) { store.detail!!.position[0] > firstX + 10f }
        compose.runOnIdle { store.select(second) }
        compose.waitUntil(10000) { store.primary == second && store.detail != null }
        compose.onNodeWithTag("transform.position.ruler").performTouchInput {
            moveBy(Offset(-24f, 0f), 16)
            up()
        }
        compose.waitForIdle()
        assertArrayEquals("The old pointer cannot edit the new owner", secondPosition, store.detail!!.position.toFloatArray(), .001f)
        compose.runOnIdle { store.undo() }
        compose.waitUntil(10000) { kotlin.math.abs(store.queryDetail(first)!!.position[0] - firstX) < .001f }
        assertArrayEquals(secondPosition, store.detail!!.position.toFloatArray(), .001f)
        screenshot("owner-switched")
    }

    @Test fun anOldNumericSheetCannotCommitToAnotherLayer() {
        val first = initialize()
        val firstPosition = store.detail!!.position.toFloatArray()
        compose.runOnIdle { store.addShape(1) }
        compose.waitUntil(10000) { store.layers.size == 2 && store.primary != first }
        val second = checkNotNull(store.primary)
        compose.runOnIdle { store.enableLayer3D() }
        compose.waitUntil(10000) { store.layers.single { it.id == second }.isThreeD }
        val secondPosition = store.detail!!.position.toFloatArray()
        compose.runOnIdle { store.select(first) }
        compose.waitUntil(10000) { store.primary == first && store.detail != null }
        compose.onNodeWithTag("transform.position.axis.0").performClick()
        compose.onNodeWithText("9").performScrollTo().assertIsDisplayed().performClick()
        compose.runOnIdle { store.select(second) }
        compose.waitUntil(10000) { store.primary == second }
        compose.waitForIdle()
        screenshot("old-keypad-before-commit")
        compose.onNodeWithText("OK").assertIsDisplayed().performClick()
        compose.waitForIdle()
        assertArrayEquals("A numeric sheet retains the owner that opened it", secondPosition, store.detail!!.position.toFloatArray(), .001f)
        assertArrayEquals(firstPosition, store.queryDetail(first)!!.position.toFloatArray(), .001f)
        screenshot("old-keypad-owner")
    }
}
