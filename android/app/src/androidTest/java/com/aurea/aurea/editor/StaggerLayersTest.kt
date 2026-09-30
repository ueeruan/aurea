package com.aurea.aurea.editor

import android.app.Application
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
 * "Escalonar" pela barra de lote: três camadas no mesmo início viram uma
 * cascata de 3 quadros na ordem da timeline (a de cima fica), UM desfazer
 * volta; "Keyframes" anda só a animação. E o interruptor "Keyframes de todas
 * as camadas" do menu da timeline.
 */
class StaggerLayersTest {
    @get:Rule val compose = createComposeRule()

    @Test fun staggerRowCascadesLayersInTimelineOrderAndOneUndoRestores() {
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
        compose.runOnIdle { store.newProject(480, 320, 30f, "Stagger") }
        compose.waitUntil(15000) { store.project.title == "Stagger" }
        repeat(3) { n ->
            compose.runOnIdle { store.addShape(1) }
            compose.waitUntil(5000) { store.layers.size == n + 1 }
        }
        val ids = store.layers.map { it.id }
        compose.runOnIdle {
            store.setLayerRanges(ids.toLongArray(), IntArray(3) { 0 }, IntArray(3) { 60 })
            store.selectAll()
        }
        compose.waitUntil(5000) { store.selection.size == 3 && store.layers.all { it.startFrame == 0 } }

        compose.onNodeWithTag("stagger_step").assertTextContains("3", substring = true)
        compose.onNodeWithTag("stagger_layers").performScrollTo().performClick()
        compose.waitUntil(5000) { store.layers.map { it.startFrame }.toSet().size == 3 }
        compose.runOnIdle {
            val starts = ids.map { id -> store.layers.first { it.id == id }.startFrame }
            // Na ordem da timeline (de cima para baixo): 0, 3, 6.
            val shown = store.layers.map { it.startFrame }
            assertEquals(listOf(0, 3, 6), shown)
            assertEquals(setOf(0, 3, 6), starts.toSet())
        }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.layers.all { it.startFrame == 0 } }

        // Só a animação: as barras ficam.
        compose.onNodeWithTag("stagger_keys").performScrollTo().performClick()
        compose.runOnIdle { assertTrue(store.layers.all { it.startFrame == 0 }) }

        // Keyframes só das escolhidas (padrão: todas).
        compose.runOnIdle {
            assertTrue(store.showAllKeyframes)
            store.toggleShowAllKeyframes()
            assertFalse(store.showAllKeyframes)
            store.toggleShowAllKeyframes()
        }
    }
}
