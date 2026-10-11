package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.click
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

class TextOptionsDockTest {
    @get:Rule val compose = createComposeRule()

    @Test fun tappingTextShowsOptionsWithoutScrollingAndOpensThePanel() {
        verifyTextOptions(text3D = false)
    }

    @Test fun tappingText3DShowsOptionsWithoutScrollingAndOpensThePanel() {
        verifyTextOptions(text3D = true)
    }

    private fun verifyTextOptions(text3D: Boolean) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(640, 640, 30f, "Text options regression") }
        compose.waitUntil(10000) { store.project.title == "Text options regression" }
        var id = 0L
        compose.runOnIdle {
            if (text3D) {
                id = store.addText3D(openEditor = false)
                store.text3d?.let { store.setText3D(it.copy(content = "AUREA")) }
            } else {
                id = store.addText(openEditor = false)
                store.setTextContent("AUREA")
                store.setTextSize(96f)
            }
        }
        compose.waitUntil(10000) { store.primary == id &&
            (if (text3D) store.text3d?.content else store.textDetail?.content) == "AUREA" }
        compose.runOnIdle { store.clearSelection() }
        compose.waitUntil(5000) { store.primary == null }
        compose.onNodeWithTag("editor.stage").performTouchInput { click(center) }
        compose.waitUntil(10000) { store.primary == id }
        compose.onNodeWithTag("dock.tool.TextOptions").assertIsDisplayed().performClick()
        compose.onNodeWithTag("dock.tool.TextOptions").assertDoesNotExist()
        compose.runOnIdle { assertEquals(id, store.primary) }
    }
}
