package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import java.io.File

/**
 * Omega 0.3.2, área 08: as jornadas por id (as mesmas do iOS) — a barra de
 * adicionar abre o conteúdo embutido acima dela (`addBar.*` → `add.*`), o
 * Desenho age na hora, o Elemento cria o grupo vazio, e a página "Exportar e
 * compartilhar" (`editor.export` → `export.*`) abre a sub-página de ajustes.
 * Capturas em `files/ui-omega-*.png`.
 */
class OmegaAddExportTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext

    private fun capture(name: String) {
        compose.waitForIdle()
        Thread.sleep(650)
        instrumentation.waitForIdleSync()
        val bitmap = checkNotNull(instrumentation.uiAutomation.takeScreenshot())
        File(context.filesDir, "ui-omega-$name.png").outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        bitmap.recycle()
    }

    private fun launch(title: String) {
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(720, 1280, 30f, title) }
        compose.waitUntil(15000) { store.project.title == title }
    }

    @Test fun addBarOpensEmbeddedContentAndAddsFromEachCategory() {
        launch("AUREA · Omega add")
        // Forma em páginas: o painel encosta na barra, que continua à vista.
        compose.onNodeWithTag("addBar.Shape").performClick()
        compose.onNodeWithTag("add.panel").assertIsDisplayed()
        compose.onNodeWithTag("add.shape.pager").assertIsDisplayed()
        compose.onNodeWithTag("add.shape.dots").assertIsDisplayed()
        compose.onNodeWithTag("editor.addBar").assertIsDisplayed()
        capture("shapes")
        compose.onNodeWithTag("add.shape.0").performClick()
        compose.waitUntil(5000) { store.layers.size == 1 }
        compose.onAllNodesWithTag("add.panel").fetchSemanticsNodes().let { assertTrue("adding closes the panel", it.isEmpty()) }
        compose.runOnIdle { store.clearSelection() }

        // Tocar no botão aceso fecha; trocar de botão troca o conteúdo.
        compose.onNodeWithTag("addBar.Element").performClick()
        compose.onNodeWithTag("add.element.camera").assertIsDisplayed()
        compose.onNodeWithTag("add.element.emptyGroup").assertIsDisplayed()
        capture("element")
        compose.onNodeWithTag("addBar.Vector").performClick()
        compose.onNodeWithTag("add.vector.points").assertIsDisplayed()
        compose.onNodeWithTag("addBar.Vector").performClick()
        compose.waitUntil(3000) { compose.onAllNodesWithTag("add.panel").fetchSemanticsNodes().isEmpty() }

        // Grupo vazio: uma camada de grupo (kind 7 = composição) no cabeçote.
        compose.onNodeWithTag("addBar.Element").performClick()
        compose.onNodeWithTag("add.element.emptyGroup").performClick()
        compose.waitUntil(5000) { store.layers.size == 2 }
        compose.runOnIdle { store.clearSelection() }

        // Áudio: a faixa com "Ver tudo" e "Gravar voz"; presets: a faixa de modelos.
        compose.onNodeWithTag("addBar.Audio").performClick()
        compose.onNodeWithTag("add.audio.viewAll").assertIsDisplayed()
        compose.onNodeWithTag("add.voice").assertIsDisplayed()
        capture("audio")
        compose.onNodeWithTag("addBar.TextPresets").performClick()
        compose.onNodeWithTag("add.presets.viewAll").assertIsDisplayed()
        compose.onNodeWithTag("add.presets.discover").assertIsDisplayed()
        capture("presets")
        compose.onNodeWithTag("add.close").performClick()
        compose.waitUntil(3000) { compose.onAllNodesWithTag("add.panel").fetchSemanticsNodes().isEmpty() }

        // Desenho age na hora: nenhum painel abre.
        compose.onNodeWithTag("addBar.Draw").performClick()
        compose.waitForIdle()
        assertEquals(0, compose.onAllNodesWithTag("add.panel").fetchSemanticsNodes().size)
    }

    @Test fun exportPageListsTheOptionsAndSlidesToTheSettings() {
        launch("AUREA · Omega export")
        compose.runOnIdle { store.addShape(0) }
        compose.waitUntil(5000) { store.layers.size == 1 }
        compose.runOnIdle { store.clearSelection() }
        compose.onNodeWithTag("editor.export").performClick()
        compose.onNodeWithTag("export.page").assertIsDisplayed()
        for (tag in listOf("export.close", "export.estimate.space", "export.estimate.size", "export.option.video",
                "export.option.package", "export.option.frame", "export.option.sequence", "export.option.gif",
                "export.option.video.resolution", "export.go")) {
            compose.onNodeWithTag(tag).assertIsDisplayed()
        }
        capture("export-list")
        compose.onNodeWithTag("export.option.video.settings").performClick()
        compose.onNodeWithTag("export.back").assertIsDisplayed()
        compose.onNodeWithTag("export.settings.fps.low").assertIsDisplayed()
        compose.onNodeWithTag("export.settings.quality").assertIsDisplayed()
        capture("export-video-settings")
        compose.onNodeWithTag("export.settings.fps.low").performClick()
        compose.onNodeWithTag("export.back").performClick()
        compose.onNodeWithTag("export.option.gif").performClick()
        compose.onNodeWithTag("export.option.gif.settings").performClick()
        compose.onNodeWithTag("export.settings.gifWidth.480").assertIsDisplayed()
        compose.onNodeWithTag("export.settings.background").assertIsDisplayed()
        capture("export-gif-settings")
    }
}
