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
import com.aurea.aurea.editor.panels.HumanRow
import androidx.compose.foundation.layout.Box
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

/** Exercises the actual Canvas and drag callbacks, with a six-slot global
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
                        // As linhas de ambiente do objeto usam a linha padrão dos painéis (HumanRow).
                        Box(Modifier.testTag("environment.ruler.object-$index")) {
                            HumanRow(env, "object-$index", value, .01f, -360f, 360f, "", 2, 0f,
                                onStart = {}, onValue = { values[index] = it }, onEnd = {}, onCommit = { values[index] = it })
                        }
                    }
                }
            }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { assertTrue(store.environment.size >= 4) }
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
