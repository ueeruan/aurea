package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.Alignment
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.TrackGraph
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

class GraphGesturesTest {
    @get:Rule val compose = createComposeRule()

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
                    if (showGraph.value) Box(Modifier.align(Alignment.BottomCenter).fillMaxWidth().height(280.dp)) {
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
            swipe(Offset(width / 2f, height / 2f), Offset(width * .62f, height * .35f), 350)
        }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().any { it.property == 0 && it.time in 31..59 && it.value > initial.value } }
        compose.runOnIdle {
            assertEquals(before.size, store.keyframes[id].orEmpty().size)
            assertEquals(before.filter { it.property != 0 }, store.keyframes[id].orEmpty().filter { it.property != 0 })
            store.undo()
        }
        try { compose.waitUntil(5000) { store.keyframes[id] == before } }
        catch (error: Throwable) { throw AssertionError("One undo must restore both coordinates. Before=$before After=${store.keyframes[id]}", error) }
    }
}
