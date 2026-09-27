package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.editor.panels.PanelContent
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

class GraphRegressionTest {
    @get:Rule val compose = createComposeRule()
    @Test fun linked2DScaleCurveChangesBothAxesAndUnlockedCurveStaysIndependent() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var initialized = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            initialized = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { initialized && store.engineReady }
        compose.runOnIdle { store.newProject(320,240,30f,"Scale XY regression") }
        compose.waitUntil(15000) { store.project.title == "Scale XY regression" }
        compose.runOnIdle { store.addNull(false) }
        compose.waitUntil(5000) { store.layers.size == 1 }
        val id = store.layers.single().id
        compose.runOnIdle { store.toggleTransformKeyframe(intArrayOf(3,4)) }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().size == 2 }
        val first = store.keyframes[id]!!.single { it.property == 3 }
        compose.runOnIdle { store.setKeyframeEasing(id,first,2,0f,1f,.1f,1f) }
        compose.runOnIdle {
            val keys = store.keyframes[id]!!
            assertEquals(store.queryKeyframeEasing(id,keys[0])!!.toList(),store.queryKeyframeEasing(id,keys[1])!!.toList())
            store.beginGesture("linked scale")
            store.editGraphKeyframe(id,first,5,2f)
            store.endGesture()
        }
        compose.waitUntil(5000) { store.keyframes[id]!!.all { it.time == 5 && it.value == 2f } }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.keyframes[id]!!.all { it.time == 0 && it.value == 1f } }
        compose.runOnIdle {
            store.scaleAxesLinked = false
            store.setKeyframeEasing(id,first,2,.4f,0f,1f,1f)
        }
        compose.runOnIdle {
            val keys = store.keyframes[id]!!
            assertNotEquals(store.queryKeyframeEasing(id,keys[0])!!.toList(),store.queryKeyframeEasing(id,keys[1])!!.toList())
        }
    }
    @Test fun valueGraphKeepsItsModeAndLinkedXYZWhileDraggingAndUndoing() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var initialized = false
        val show = mutableStateOf(false)
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            initialized = true
            AureaTheme { Box {
                EditorScreen(store)
                if (show.value) Box(Modifier.align(Alignment.BottomCenter).fillMaxWidth().height(300.dp)) {
                    PanelContent(store, EditorPanel.Curve, {}, {}, {})
                }
            } }
        }
        compose.waitUntil(30000) { initialized && store.engineReady }
        compose.runOnIdle { store.newProject(320,240,30f,"Graph XYZ regression") }
        compose.waitUntil(15000) { store.project.title == "Graph XYZ regression" }
        compose.runOnIdle { store.addNull(true) }
        compose.waitUntil(5000) { store.layers.size == 1 }
        val id = store.layers.single().id
        compose.runOnIdle { store.setScale3(floatArrayOf(1f,2f,3f)) }
        compose.waitUntil(5000) { store.detail?.scale == listOf(1f,2f,3f) }
        compose.runOnIdle { store.toggleTransformKeyframe(intArrayOf(3,4,5)) }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().size == 3 }
        compose.runOnIdle { store.seek(30) }
        compose.waitUntil(5000) { store.playhead == 30 }
        compose.runOnIdle { store.setScale3(floatArrayOf(2f,4f,6f)) }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().size == 6 }
        val before = store.keyframes[id]!!
        val first = before.single { it.property == 3 && it.time == 0 }
        compose.runOnIdle {
            store.beginGesture("curve group")
            store.setKeyframeEasing(id,first,2,.2f,.8f,.6f,1f)
            store.endGesture()
        }
        compose.runOnIdle {
            val curves = store.keyframes[id]!!.filter { it.time == 0 }.map { store.queryKeyframeEasing(id,it)!!.toList() }
            assertEquals(curves[0],curves[1]); assertEquals(curves[1],curves[2])
            store.undo()
            store.selectKeyframe(id,first)
            store.curveGraphMode = 1
            show.value = true
        }
        compose.onNodeWithTag("curve.trackGraph").assertExists().performTouchInput {
            swipe(Offset(width * .05357f,height * .90323f),Offset(width * .25f,height * .65f),400)
        }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().any { it.property == 3 && it.time in 1..29 } }
        compose.onNodeWithTag("curve.trackGraph").assertExists()
        compose.runOnIdle {
            assertEquals(1,store.curveGraphMode)
            val changed = store.keyframes[id]!!.filter { it.time != 30 }.sortedBy { it.property }
            assertEquals(3,changed.size)
            assertEquals(1,changed.map { it.time }.distinct().size)
            assertEquals(changed[0].value * 2,changed[1].value,.001f)
            assertEquals(changed[0].value * 3,changed[2].value,.001f)
            store.undo()
        }
        compose.waitUntil(5000) { store.keyframes[id]!!.map { it.time to it.value } == before.map { it.time to it.value } }
        compose.runOnIdle { show.value = false }
        compose.runOnIdle { store.selectKeyframe(id,first); show.value = true }
        compose.onNodeWithTag("curve.trackGraph").assertExists()
    }
}
