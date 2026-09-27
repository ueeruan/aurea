package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

/** "Vincular a novo nulo" pela lista de vínculo da barra de lote: as duas
 * escolhidas passam a seguir um nulo novo, e UM desfazer tira tudo. */
class ParentToNewNullTest {
    @get:Rule val compose = createComposeRule()

    @Test fun newNullRowParentsBothChosenLayersAndOneUndoRemovesIt() {
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
        compose.runOnIdle { store.newProject(480, 320, 30f, "Parent to new null") }
        compose.waitUntil(15000) { store.project.title == "Parent to new null" }
        compose.runOnIdle { store.addShape(1) }
        compose.waitUntil(5000) { store.layers.size == 1 }
        compose.runOnIdle { store.addShape(1) }
        compose.waitUntil(5000) { store.layers.size == 2 }
        val chosen = store.layers.map { it.id }
        compose.runOnIdle {
            store.select(chosen[0])
            store.select(chosen[1], additive = true)
        }
        compose.waitUntil(5000) { store.selection.size == 2 }

        compose.onNodeWithContentDescription(context.getString(R.string.editor_vincular_escolhidas_camada)).performClick()
        compose.onNodeWithTag("link.newNull")
            .assertHeightIsAtLeast(48.dp)
            .assertTextContains(context.getString(R.string.editor_vincular_novo_nulo))
            .performClick()
        compose.waitUntil(5000) { store.layers.size == 3 }
        compose.runOnIdle {
            assertNull(store.errorMessage)
            val nullId = store.primary!!
            assertFalse(nullId in chosen)
            assertEquals(setOf(nullId), store.selection)
            val rows = store.layers
            for (id in chosen) {
                val row = rows.first { it.id == id }
                assertEquals("Camada $id deve seguir o nulo novo", nullId, rows.getOrNull(row.parentIndex)?.id)
            }
            assertFalse(rows.first { it.id == nullId }.hasParent)
        }

        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.layers.size == 2 }
        compose.runOnIdle {
            assertEquals(chosen.toSet(), store.layers.map { it.id }.toSet())
            assertTrue("Um desfazer solta as duas", store.layers.none { it.hasParent })
        }
    }
}
