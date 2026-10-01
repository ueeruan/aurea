package com.aurea.aurea.editor

import android.app.Application
import android.os.ParcelFileDescriptor
import androidx.compose.ui.test.*
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.text.TextLayoutResult
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

class TextContentEditorTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext

    private fun launch() {
        assertTrue(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(320, 240, 30f, "Text content test") }
        compose.waitUntil(15000) { store.project.title == "Text content test" }
    }

    private fun assertKeyboardAndFocus() {
        compose.onNodeWithTag("text.content.input").assertIsFocused()
        compose.waitUntil(5000) {
            val fd = InstrumentationRegistry.getInstrumentation().uiAutomation.executeShellCommand("dumpsys input_method")
            ParcelFileDescriptor.AutoCloseInputStream(fd).bufferedReader().use { it.readText().contains("mInputShown=true") }
        }
    }

    @Test fun centeredTextEditorUsesCenteredParagraphLayout() {
        launch()
        compose.runOnIdle { store.addText(); store.dismissTextContentEditor() }
        compose.waitUntil(5000) { store.textDetail != null }
        compose.runOnIdle { store.setTextAlignment(1); store.setTextContent("Wide first line\nShort") }
        compose.waitUntil(5000) { store.textDetail?.alignment == 1 && store.textDetail?.content == "Wide first line\nShort" }
        compose.runOnIdle { store.openTextContentEditor() }
        val layouts = mutableListOf<TextLayoutResult>()
        compose.onNodeWithTag("text.content.input").performSemanticsAction(SemanticsActions.GetTextLayoutResult) { it(layouts) }
        assertTrue(layouts.isNotEmpty())
        val layout = layouts.first()
        assertEquals(TextAlign.Center, layout.layoutInput.style.textAlign)
        assertEquals(layout.size.width / 2f, (layout.getLineLeft(1) + layout.getLineRight(1)) / 2f, 1f)
        compose.runOnIdle { assertEquals(1, store.textContentRequest!!.alignment) }
    }

    @Test fun addingAndReopening2DTextOpensKeyboardAndKeepsSize() {
        launch()
        compose.runOnIdle { assertTrue(store.addText() >= 0) }
        compose.waitForIdle(); assertKeyboardAndFocus()
        compose.onNodeWithTag("text.content.input").performTextReplacement("Primeira linha\nSegunda linha")
        compose.onNodeWithTag("text.content.done").performClick()
        compose.waitUntil(5000) { store.textDetail?.content == "Primeira linha\nSegunda linha" }
        compose.runOnIdle { store.setTextSize(96f) }
        compose.waitUntil(5000) { store.textDetail?.size == 96f }
        compose.onNodeWithContentDescription(context.getString(R.string.sh_dock_edit_text)).performClick()
        assertKeyboardAndFocus()
        compose.onNodeWithTag("text.content.input").performTextReplacement("Novo texto")
        compose.onNodeWithTag("text.content.done").performClick()
        compose.waitUntil(5000) { store.textDetail?.content == "Novo texto" }
        compose.runOnIdle { assertEquals(96f, store.textDetail!!.size, .001f) }
        compose.onNodeWithContentDescription(context.getString(R.string.text_options)).performClick()
        compose.onNodeWithTag("text.content.input").assertDoesNotExist()
        compose.runOnIdle { assertNull(store.textContentRequest) }
    }

    @Test fun adding3DTextOpensKeyboardAndEditingKeepsGeometryAndMaterial() {
        launch()
        compose.runOnIdle { assertTrue(store.addText3D() >= 0) }
        compose.waitForIdle(); assertKeyboardAndFocus()
        compose.onNodeWithTag("text.content.input").performTextReplacement("AUREA")
        compose.onNodeWithTag("text.content.done").performClick()
        compose.waitUntil(5000) { store.text3d?.content == "AUREA" }
        compose.runOnIdle { store.setText3D(store.text3d!!.copy(depth = .42f, metallic = .75f)) }
        compose.onNodeWithContentDescription(context.getString(R.string.sh_dock_edit_text)).performClick()
        assertKeyboardAndFocus()
        compose.onNodeWithTag("text.content.input").performTextReplacement("EDIT")
        compose.onNodeWithTag("text.content.done").performClick()
        compose.waitUntil(5000) { store.text3d?.content == "EDIT" }
        compose.runOnIdle {
            assertEquals(.42f, store.text3d!!.depth, .001f)
            assertEquals(.75f, store.text3d!!.metallic, .001f)
        }
        compose.onNodeWithContentDescription(context.getString(R.string.sh_dock_edit_text)).performClick()
        compose.onNodeWithTag("text.content.input").performTextReplacement("Discard this")
        compose.onNodeWithTag("text.content.cancel").performClick()
        compose.runOnIdle { assertEquals("EDIT", store.text3d!!.content) }
    }
}
