package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.Alignment
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.TrackGraph
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

class GraphGesturesTest {
    @get:Rule val compose = createComposeRule()

    @Test fun speedHandleEditsOnlyItsIntervalAndUndoRestoresCurve() {
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
                if(show.value) Box(Modifier.align(Alignment.BottomCenter).fillMaxWidth().height(280.dp)) {
                    val id=store.layers.single().id
                    TrackGraph(store,id,store.keyframes[id].orEmpty().filter { it.property==0 },true)
                }
            } }
        }
        compose.waitUntil(30000) { initialized && store.engineReady }
        compose.runOnIdle { store.newProject(320,240,30f,"Speed handles") }
        compose.waitUntil(15000) { store.project.title=="Speed handles" }
        compose.runOnIdle { store.addNull(false) }
        compose.waitUntil(5000) { store.layers.size==1 }
        val id=store.layers.single().id
        for(frame in listOf(0,30)) {
            compose.runOnIdle { store.seek(frame) }
            compose.waitUntil(5000) { store.playhead==frame }
            compose.runOnIdle { store.toggleTransformKeyframe(intArrayOf(0,1)) }
        }
        compose.runOnIdle { store.setTransform(0,260f) }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().any { it.property == 0 && it.time == 30 && it.value == 260f } }
        val before=store.keyframes[id].orEmpty()
        val first=before.first { it.property==0 && it.time==0 }
        val original=store.queryKeyframeEasing(id,first)!!.toList()
        compose.runOnIdle { show.value=true }
        compose.onNodeWithTag("curve.trackGraph").performTouchInput {
            swipe(Offset(width*.3512f,height*.0968f),Offset(width*.43f,height*.4f),350)
        }
        compose.waitUntil(5000) { store.queryKeyframeEasing(id,first)?.toList()!=original }
        compose.runOnIdle {
            assertEquals(before.filter { it.property==1 },store.keyframes[id].orEmpty().filter { it.property==1 })
            assertEquals(before.map { it.time to it.value },store.keyframes[id].orEmpty().map { it.time to it.value })
            val h=store.queryKeyframeEasing(id,first)!!
            assertTrue(h[1]/h[0] < .9f)
            store.undo()
        }
        compose.waitUntil(5000) { store.queryKeyframeEasing(id,first)?.toList()==original }
    }

    @Test fun editingRotationXKeysOnlyItsXYZGroup() {
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
        compose.runOnIdle { store.newProject(320, 240, 30f, "Independent rotation") }
        compose.waitUntil(15000) { store.project.title == "Independent rotation" }
        compose.runOnIdle { store.addNull(true) }
        compose.waitUntil(5000) { store.layers.size == 1 }
        val id = store.layers.single().id
        compose.runOnIdle { store.toggleTransformKeyframe(intArrayOf(6)) }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().size == 1 }
        compose.runOnIdle { store.seek(15) }
        compose.waitUntil(5000) { store.playhead == 15 }
        compose.onNodeWithTag("stage.autokey").performClick()
        compose.runOnIdle { store.setTransform(6, 30f) }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().singleOrNull()?.value == 30f }
        compose.runOnIdle { assertEquals(0, store.keyframes[id].orEmpty().single().time); store.undo() }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().singleOrNull()?.value == 0f }
        compose.onNodeWithTag("stage.autokey").performClick()
        compose.runOnIdle { store.setTransform(6, 45f) }
        // Auto-Key sends the XYZ group, but the shared onlyIfChanged contract
        // skips redundant keys on unchanged axes and leaves their pose intact.
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().any { it.property == 6 && it.time == 15 } }
        compose.runOnIdle {
            val keys = store.keyframes[id].orEmpty()
            assertTrue(keys.all { it.property == 6 })
            assertEquals(45f, keys.single { it.time == 15 && it.property == 6 }.value)
            assertEquals(0f, store.detail!!.rotation[1], .001f)
            assertEquals(0f, store.detail!!.rotation[2], .001f)
            store.undo()
        }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().size == 1 }
        compose.runOnIdle { store.seek(0) }
        compose.waitUntil(5000) { store.playhead == 0 }
        compose.runOnIdle { store.toggleTransformKeyframe(intArrayOf(0)) }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().any { it.property == 0 && it.time == 0 } }
        compose.runOnIdle { store.seek(15) }
        compose.waitUntil(5000) { store.playhead == 15 }
        val beforeGizmo = store.keyframes[id].orEmpty()
        val positionBefore = store.detail!!.position.toList()
        compose.runOnIdle { store.gizmoDrag(0, 20f) }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().any { it.property == 0 && it.time == 15 } }
        compose.runOnIdle {
            // Only the changed X axis needs a key; Y/Z and rotation stay intact.
            val keys = store.keyframes[id].orEmpty()
            assertEquals(beforeGizmo.size + 1, keys.size)
            assertEquals(positionBefore[1], store.detail!!.position[1], .001f)
            assertEquals(positionBefore[2], store.detail!!.position[2], .001f)
            assertEquals(beforeGizmo.filter { it.property in 6..8 }, keys.filter { it.property in 6..8 })
            store.undo()
        }
        compose.waitUntil(5000) { store.keyframes[id] == beforeGizmo }
    }

    @Test fun graphDragMovesTimeAndValuePreservesOtherTracksAndUndoesOnce() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var initialized = false
        val showGraph = mutableStateOf(false)
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            initialized = true
            AureaTheme {
                Box {
                    EditorScreen(store)
                    if (showGraph.value) Box(Modifier.align(Alignment.BottomCenter).fillMaxWidth().height(280.dp).testTag("test.graphPanel")) {
                        val id = store.layers.single().id
                        TrackGraph(store, id, store.keyframes[id].orEmpty().filter { it.property == 0 }, false)
                    }
                }
            }
        }
        compose.waitUntil(30000) { initialized && store.engineReady }
        compose.runOnIdle { store.newProject(320, 240, 30f, "Graph touch") }
        compose.waitUntil(15000) { store.project.title == "Graph touch" }
        compose.runOnIdle { store.addNull(false) }
        compose.waitUntil(5000) { store.layers.size == 1 }
        val id = store.layers.single().id
        for (frame in listOf(0, 30, 60)) {
            compose.runOnIdle { store.seek(frame) }
            compose.waitUntil(5000) { store.playhead == frame }
            compose.runOnIdle { store.toggleTransformKeyframe(intArrayOf(0, 1, 2)) }
            compose.waitUntil(5000) { store.keyframes[id].orEmpty().count { it.time == frame } == 3 }
        }
        val before = store.keyframes[id].orEmpty()
        val initial = before.single { it.property == 0 && it.time == 30 }
        compose.runOnIdle { showGraph.value = true }
        compose.onNodeWithTag("curve.trackGraph").performTouchInput {
            // The graph fits X/Y/Z together (0..160), so X=160 is near its top.
            swipe(Offset(width / 2f, height * .0968f), Offset(width * .62f, height * .02f), 350)
        }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().any { it.property == 0 && it.time in 31..59 && it.value > initial.value } }
        compose.runOnIdle {
            assertEquals(before.size, store.keyframes[id].orEmpty().size)
            assertEquals(before.filter { it.property != 0 }, store.keyframes[id].orEmpty().filter { it.property != 0 })
            store.undo()
        }
        try { compose.waitUntil(5000) { store.keyframes[id] == before } }
        catch (error: Throwable) { throw AssertionError("One undo must restore both coordinates. Before=$before After=${store.keyframes[id]}", error) }

        compose.onNodeWithTag("curve.multi").performClick()
        graphAction(R.string.common_all)
        compose.onNodeWithTag("curve.trackGraph").performTouchInput {
            swipe(Offset(width / 2f, height * .0968f), Offset(width * .62f, height * .0968f), 350)
        }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().filter { it.property == 0 }.minOf { it.time } > 0 }
        compose.runOnIdle {
            val moved = store.keyframes[id].orEmpty().filter { it.property == 0 }.sortedBy { it.time }
            assertEquals(listOf(30, 30), moved.zipWithNext { a, b -> b.time - a.time })
            assertEquals(before.filter { it.property != 0 }, store.keyframes[id].orEmpty().filter { it.property != 0 })
            store.undo()
        }
        compose.waitUntil(5000) { store.keyframes[id] == before }
        graphAction(R.string.common_all)
        graphAction(R.string.common_copy)
        compose.runOnIdle { store.seek(90) }
        compose.waitUntil(5000) { store.playhead == 90 }
        graphAction(R.string.common_paste)
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().count { it.property == 0 } == 6 }
        compose.runOnIdle {
            assertEquals(listOf(0, 30, 60, 90, 120, 150), store.keyframes[id].orEmpty().filter { it.property == 0 }.map { it.time }.sorted())
        }
        graphAction(R.string.common_all)
        graphAction(R.string.common_duplicate)
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().count { it.property == 0 } == 12 }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().count { it.property == 0 } == 6 }
        graphAction(R.string.common_all)
        graphAction(R.string.common_delete)
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().none { it.property == 0 } }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().count { it.property == 0 } == 6 }
    }
    private fun graphAction(id: Int) = compose.onNode(hasText(text(id)) and hasAnyAncestor(hasTestTag("test.graphPanel")))
        .performScrollTo().performClick()
    private fun text(id: Int): String = InstrumentationRegistry.getInstrumentation().targetContext.getString(id)
}
