package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.*
import com.aurea.aurea.editor.timeline.focusedKeys
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Panels read/write actual native tracks; no mocked keyframe or property rows. */
class ShapePuppetPanelDeviceTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore
    private var mode by mutableIntStateOf(0)

    private fun openShape(title: String) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            val ui = remember { EditorUi() }
            val env = remember(store) { PanelEnv(store, {}, {}, {}, {}, {}, { null }) }
            AureaTheme {
                Column(Modifier.fillMaxSize()) {
                    PreviewStage(store, ui, Modifier.fillMaxWidth().weight(1f))
                    Box(Modifier.fillMaxWidth().height(360.dp)) {
                        when (mode) {
                            1 -> AppearancePanel(env)
                            2 -> EffectsPanel(env)
                            else -> ShapeEditPanel(env)
                        }
                    }
                }
            }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(320,240,30f,title) }
        compose.waitUntil(15000) { store.project.title == title && !store.projectOperationBusy }
        compose.runOnIdle { ShapeEditState.param = 5; ShapeEditState.linked = false; store.addShape(0) }
        compose.waitUntil(5000) { store.primary != null && store.detail?.kind == 5 }
        compose.runOnIdle { store.seek(0) }
        compose.waitUntil(5000) { store.playhead == 0 && store.detail?.localPlayhead == 0 }
    }
    private fun visibleKeys() = store.primary?.let { focusedKeys(store.keyframes[it].orEmpty(), store.timelineFocus.orEmpty()) }.orEmpty()
    private fun nativeKeys(layer: Long): List<KeyframeRow> {
        var result = emptyList<KeyframeRow>()
        compose.runOnIdle {
            var capacity = 32
            while (true) {
                val rows = ByteBuffer.allocateDirect(capacity * PodLayout.KEYFRAME_ROW_BYTES).order(ByteOrder.nativeOrder())
                val count = store.engineForStress.queryKeyframes(layer, rows, capacity)
                if (count < capacity) { result = List(count) { KeyframeRow.read(rows, it) }; break }
                capacity *= 2
            }
        }
        return result
    }
    private fun waitForKeys(layer: Long, label: String, condition: () -> Boolean) {
        try { compose.waitUntil(5000, condition) }
        catch (failure: Exception) {
            throw AssertionError("$label: native=${nativeKeys(layer)}; published=${store.keyframes[layer]}; " +
                "selected=${ShapeEditState.param}, local=${store.detail?.localPlayhead}", failure)
        }
    }

    @Test fun shapeSizeStaysEvaluatedAndVisibilityDeletesOnlyOpacity() {
        openShape("Shape focus regression")
        val layer = store.primary!!
        compose.runOnIdle {
            store.toggleTransformKeyframe(intArrayOf(0,1,2,12))
            store.toggleShapeParamKey(5)
            store.setShapeParam(5,333f)
        }
        compose.waitUntil(5000) { store.detail?.sourceWidth == 333 && store.keyframes[layer].orEmpty().any { it.property == 35 && it.value == 333f } }
        compose.waitUntil(5000) { store.timelineFocus == listOf(TrackKey(35,0,5),TrackKey(35,0,6)) }
        assertTrue(visibleKeys().all { it.property == 35 && it.effectIndex == 0 })
        compose.onNodeWithTag("shape.size.ruler").performTouchInput {
            val p = center; down(p); moveTo(p + Offset(70f,0f),120); up()
        }
        compose.waitUntil(5000) { store.shapeParams?.get(5) != 333f }
        val size = store.shapeParams!![5]
        compose.waitUntil(5000) { store.detail?.sourceWidth == size.toInt() }
        compose.runOnIdle { mode = 1 }
        compose.waitUntil(5000) { store.timelineFocus == listOf(TrackKey(12)) }
        assertTrue(visibleKeys().isNotEmpty()); assertTrue(visibleKeys().all { it.property == 12 })
        val remove = InstrumentationRegistry.getInstrumentation().targetContext.getString(R.string.panel_tirar_keyframe_daqui)
        compose.onNodeWithContentDescription(remove).performClick()
        compose.waitUntil(5000) { store.keyframes[layer].orEmpty().none { it.property == 12 } }
        assertTrue(store.keyframes[layer].orEmpty().any { it.property == 35 && it.value == size })
        compose.runOnIdle { mode = 0 }
        compose.waitUntil(5000) { store.timelineFocus == listOf(TrackKey(35,0,5),TrackKey(35,0,6)) }
        assertEquals(5,ShapeEditState.param)
        val sizeKeys = nativeKeys(layer).filter { it.property == 35 }
        assertEquals("Fixture must contain just the width key at this frame: $sizeKeys",1,sizeKeys.size)
        assertEquals(5,sizeKeys.single().paramIndex); assertEquals(0,sizeKeys.single().time)
        assertEquals(size,sizeKeys.single().value,0f)
        compose.onNodeWithContentDescription(remove).performClick()
        waitForKeys(layer,"Shape remove must delete the existing native width key") { store.keyframes[layer].orEmpty().none { it.property == 35 } }
        assertTrue(nativeKeys(layer).none { it.property == 35 })
        assertEquals(size,store.shapeParams!![5],0f)
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.keyframes[layer].orEmpty().any { it.property == 35 && it.value == size } }
        assertEquals(size,store.shapeParams!![5],0f)
    }

    @Test fun puppetTimelineFollowsActiveSelectedPinsOfTheOpenInstance() {
        openShape("Puppet focus regression")
        val layer = store.primary!!
        compose.runOnIdle { store.addEffectAndFocus(effectTypeId("aurea.distort.puppet")) }
        compose.waitUntil(5000) { store.effects.size == 1 }
        compose.runOnIdle { store.addEffectAndFocus(effectTypeId("aurea.distort.puppet")) }
        compose.waitUntil(5000) { store.effects.size == 2 }
        val first = store.effects.first().effectId; val second = store.effects.last().effectId
        compose.runOnIdle {
            assertEquals(0,store.puppetAddPin(layer,first,.2f,.3f))
            assertEquals(0,store.puppetAddPin(layer,second,.2f,.3f))
            assertEquals(1,store.puppetAddPin(layer,second,.7f,.6f))
            store.seek(30)
        }
        compose.waitUntil(5000) { store.playhead == 30 }
        compose.runOnIdle {
            store.autoKeyTransforms = true
            store.puppetMovePin(layer,first,0,.4f,.5f,false)
            store.puppetMovePin(layer,second,0,.4f,.5f,false)
            store.puppetMovePin(layer,second,1,.8f,.7f,false)
            store.rigGestureEnd(); mode = 2
        }
        compose.waitUntil(5000) { store.keyframes[layer].orEmpty().size == 12 && PuppetStage.effect == second }
        compose.runOnIdle { PuppetStage.editing = true; PuppetStage.selected = 1 }
        compose.waitUntil(5000) { store.timelineFocus == listOf(TrackKey(31,second,32),TrackKey(31,second,33)) }
        assertEquals(4,visibleKeys().size)
        assertTrue(visibleKeys().all { it.effectIndex == second && it.paramIndex in 32..33 })
        val expectedPinKeys = nativeKeys(layer).filter {
            it.property == 31 && it.effectIndex == second && it.paramIndex in 32..33
        }.toSet()
        assertEquals("Selected pin must have four actual native keys before removal",4,expectedPinKeys.size)
        compose.runOnIdle { store.puppetRemovePin(layer,second,1) }
        compose.waitUntil(5000) { store.timelineFocus == listOf(TrackKey(31,second,20),TrackKey(31,second,21)) }
        assertTrue(visibleKeys().all { it.effectIndex == second && it.paramIndex in 20..21 })
        compose.runOnIdle { store.undo() }
        // Focus follows the native active pins before publish() necessarily
        // replaces the asynchronously refreshed keyframe map. Require the
        // same four native rows and the same four UI rows together; a missing
        // native undo or wrong effect/pin still fails with both diagnostics.
        waitForKeys(layer,"Puppet undo must restore the selected pin's native and visible keys") {
            val native = nativeKeys(layer).filter {
                it.property == 31 && it.effectIndex == second && it.paramIndex in 32..33
            }
            store.primary == layer && PuppetStage.layer == layer && PuppetStage.effect == second &&
                PuppetStage.selected == 1 &&
                store.timelineFocus == listOf(TrackKey(31,second,32),TrackKey(31,second,33)) &&
                native.size == 4 && native.toSet() == expectedPinKeys &&
                visibleKeys().size == 4 && visibleKeys().toSet() == expectedPinKeys
        }
        compose.runOnIdle {
            assertEquals(4,visibleKeys().size)
            assertEquals(expectedPinKeys,visibleKeys().toSet())
            assertTrue(visibleKeys().all { it.effectIndex == second && it.paramIndex in 32..33 })
            val pins = store.puppetPins(layer,second)
            assertEquals(listOf(0,1),List(pins.size / 4) { pins[it * 4].toInt() })
        }
        assertEquals(expectedPinKeys,nativeKeys(layer).filter {
            it.property == 31 && it.effectIndex == second && it.paramIndex in 32..33
        }.toSet())
    }
}
