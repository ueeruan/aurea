package com.aurea.aurea.editor

import android.app.Application
import android.os.ParcelFileDescriptor
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.text.TextLayoutResult
import androidx.compose.ui.text.style.TextAlign
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

/**
 * Omega conteúdo (área 06, P0-1): o MODO DE TEXTO substituiu o diálogo de
 * conteúdo. Cada letra vai na hora para a camada (sem ✓), o teclado abre
 * sozinho, ✓ só fecha, um desfazer volta a rajada inteira; a barra de estilo
 * alterna o alinhamento E → C → D e abre os popups de fonte e tamanho.
 */
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
        compose.onNodeWithTag("text.edit.input").assertIsFocused()
        compose.waitUntil(5000) {
            val fd = InstrumentationRegistry.getInstrumentation().uiAutomation.executeShellCommand("dumpsys input_method")
            ParcelFileDescriptor.AutoCloseInputStream(fd).bufferedReader().use { it.readText().contains("mInputShown=true") }
        }
    }

    @Test fun centeredTextModeUsesCenteredParagraphLayout() {
        launch()
        compose.runOnIdle { store.addText(); store.dismissTextContentEditor() }
        compose.waitUntil(5000) { store.textDetail != null }
        compose.runOnIdle { store.setTextAlignment(1); store.setTextContent("Wide first line\nShort") }
        compose.waitUntil(5000) { store.textDetail?.alignment == 1 && store.textDetail?.content == "Wide first line\nShort" }
        compose.runOnIdle { store.openTextContentEditor() }
        val layouts = mutableListOf<TextLayoutResult>()
        compose.onNodeWithTag("text.edit.input").performSemanticsAction(SemanticsActions.GetTextLayoutResult) { it(layouts) }
        assertTrue(layouts.isNotEmpty())
        assertEquals(TextAlign.Center, layouts.first().layoutInput.style.textAlign)
        compose.runOnIdle { assertEquals(1, store.textContentRequest!!.alignment) }
    }

    @Test fun typingGoesLiveWithoutDoneAndOneUndoRevertsTheBurst() {
        launch()
        compose.runOnIdle { assertTrue(store.addText() >= 0) }
        compose.waitForIdle()
        compose.onNodeWithTag("text.edit.mode").assertIsDisplayed()
        assertKeyboardAndFocus()
        val before = store.textDetail!!.content
        compose.onNodeWithTag("text.edit.input").performTextReplacement("Olá")
        // Sem tocar no ✓: a camada já tem o texto.
        compose.waitUntil(5000) { store.textDetail?.content == "Olá" }
        compose.onNodeWithTag("text.style.done").performClick()
        compose.waitUntil(5000) { store.textEditLayer == null }
        compose.onNodeWithTag("text.edit.mode").assertDoesNotExist()
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.textDetail?.content == before }
        // O atalho antigo continua abrindo o modo (teclado e foco).
        compose.runOnIdle { store.setTextSize(96f) }
        compose.waitUntil(5000) { store.textDetail?.size == 96f }
        compose.runOnIdle { store.openTextContentEditor() }
        assertKeyboardAndFocus()
        compose.onNodeWithTag("text.edit.input").performTextReplacement("Novo texto")
        compose.waitUntil(5000) { store.textDetail?.content == "Novo texto" }
        compose.runOnIdle { assertEquals(96f, store.textDetail!!.size, .001f) }
    }

    @Test fun styleBarCyclesAlignmentAndOpensFontAndSizePopups() {
        launch()
        compose.runOnIdle { assertTrue(store.addText() >= 0) }
        compose.waitUntil(5000) { store.textDetail != null }
        val start = store.textDetail!!.alignment
        val seen = mutableListOf<Int>()
        repeat(3) {
            compose.onNodeWithTag("text.style.align").performClick()
            val expected = (start + seen.size + 1) % 3
            compose.waitUntil(5000) { store.textDetail?.alignment == expected }
            seen += expected
        }
        assertEquals(listOf((start + 1) % 3, (start + 2) % 3, start), seen)
        compose.onNodeWithTag("text.style.size").performClick()
        compose.onNodeWithTag("text.size.slider").assertIsDisplayed()
        compose.onNodeWithTag("text.style.font").performClick()
        compose.onNodeWithTag("text.size.slider").assertDoesNotExist()
        compose.onNodeWithTag("text.font.mini").assertIsDisplayed()
        compose.onNodeWithTag("text.font.viewAll").performClick()
        compose.onNodeWithTag("font.browser").assertIsDisplayed()
        compose.onNodeWithTag("font.browser.close").performClick()
        compose.onNodeWithTag("font.browser").assertDoesNotExist()
    }

    @Test fun adding3DTextTypesLiveAndKeepsGeometryAndForbidsEmpty() {
        launch()
        compose.runOnIdle { assertTrue(store.addText3D() >= 0) }
        compose.waitForIdle(); assertKeyboardAndFocus()
        compose.onNodeWithTag("text.edit.input").performTextReplacement("AUREA")
        compose.waitUntil(5000) { store.text3d?.content == "AUREA" }
        compose.runOnIdle { store.setText3D(store.text3d!!.copy(depth = .42f, metallic = .75f)) }
        compose.onNodeWithTag("text.edit.input").performTextReplacement("EDIT")
        compose.waitUntil(5000) { store.text3d?.content == "EDIT" }
        compose.runOnIdle {
            assertEquals(.42f, store.text3d!!.depth, .001f)
            assertEquals(.75f, store.text3d!!.metallic, .001f)
        }
        // Texto 3D vazio continua proibido: o ✓ desabilita e o último texto fica.
        compose.onNodeWithTag("text.edit.input").performTextReplacement("")
        compose.onNodeWithTag("text.style.done").assertIsNotEnabled()
        compose.runOnIdle { assertEquals("EDIT", store.text3d!!.content) }
        compose.onNodeWithText(context.getString(R.string.text_content_3d_required)).assertIsDisplayed()
    }
}
