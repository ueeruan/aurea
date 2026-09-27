package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.mutableStateOf
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
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

class GizmoCancellationTest {
    @get:Rule val compose = createComposeRule()

    @Test fun removingStageDuringAxisDragClosesUndoGroup() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var initialized = false
        val show = mutableStateOf(true)
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            initialized = true
            val ui = remember { EditorUi() }
            AureaTheme {
                if (show.value) PreviewStage(store, ui, Modifier.fillMaxWidth().height(320.dp).testTag("gizmo.stage"))
            }
        }
        compose.waitUntil(30000) { initialized && store.engineReady }
        compose.runOnIdle { store.newProject(320,240,30f,"Gizmo cancellation") }
        compose.waitUntil(15000) { store.project.title == "Gizmo cancellation" }
        compose.runOnIdle { store.addNull(true) }
        compose.waitUntil(5000) { store.gizmo != null && store.detail != null }
        val original = store.detail!!.position.toList()
        val raw = store.gizmo!!.copyOf()
        compose.onNodeWithTag("gizmo.stage").performTouchInput {
            val mapper = StageMapper().apply { update(width.toFloat(),height.toFloat(),0f,320,240,false) }
            val screen = FloatArray(8) { i -> if(i%2==0) mapper.sx(raw[i]) else mapper.sy(raw[i]) }
            val tips = GizmoGeometry.tips(screen,context.resources.displayMetrics.density)
            down(Offset(tips[2],tips[3]))
            moveBy(Offset(30f,20f),100)
        }
        compose.waitUntil(5000) { store.detail!!.position[0] != original[0] }
        val moved = store.detail!!.position.toList()
        assertEquals(original[1],moved[1]); assertEquals(original[2],moved[2])
        compose.runOnIdle { show.value = false }
        compose.waitForIdle()
        compose.runOnIdle { show.value = true }
        compose.waitForIdle()
        // This later edit must remain a separate undo step after cancellation.
        compose.runOnIdle { store.setTransform(1,original[1]+40f) }
        compose.waitUntil(5000) { store.detail!!.position[1] == original[1]+40f }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.detail!!.position[1] == original[1] }
        compose.runOnIdle { assertEquals("Only the later Y edit should be undone", moved,store.detail!!.position.toList()) }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.detail!!.position.toList() == original }
    }
}
