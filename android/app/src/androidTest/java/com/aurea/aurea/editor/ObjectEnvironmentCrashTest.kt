package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.foundation.layout.Column
import androidx.compose.runtime.*
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.PanelEnv
import com.aurea.aurea.editor.panels.EnvRuler
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

/** Exercises the actual Canvas and drag callbacks, with a four-slot global
 * environment and different per-object values (exposure is object slot 4). */
class ObjectEnvironmentCrashTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore
    @Test fun objectRulersDrawAndDragTheirOwnValues() {
        val app = InstrumentationRegistry.getInstrumentation().targetContext.applicationContext as Application
        val values = mutableStateListOf(1.25f, 72f, 2.5f)
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(app))
            val env = PanelEnv(store, {}, {}, {}, {}, {}, { null })
            AureaTheme {
                Column {
                    values.forEachIndexed { index, value ->
                        EnvRuler(env, "object-$index", .01f, -360f, 360f, value, value.toString()) { values[index] = it }
                    }
                }
            }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { assertEquals(4, store.environment.size) }
        val global = store.environment.toList()
        for (index in 0..2) {
            val before = values[index]
            compose.onNodeWithTag("environment.ruler.object-$index").assertIsDisplayed().performTouchInput {
                down(center); moveBy(androidx.compose.ui.geometry.Offset(-35f, 0f), 80); up()
            }
            compose.waitForIdle()
            compose.runOnIdle {
                assertTrue("drag preserves object baseline", kotlin.math.abs(values[index] - before) < 2f)
                assertTrue("ruler edits value", kotlin.math.abs(values[index] - before) > .001f)
                assertEquals(global, store.environment)
            }
        }
    }
}
