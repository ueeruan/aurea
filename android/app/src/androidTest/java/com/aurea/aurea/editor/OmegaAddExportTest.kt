package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
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
        // 0.3.3 barra: o Vetor abre pelo Elemento; a vaga do Elemento continua acesa
        // (com o nome do Vetor) e tocar nela fecha.
        compose.onNodeWithTag("add.element.vector").performScrollTo().performClick()
        compose.onNodeWithTag("add.vector.points").assertIsDisplayed()
        compose.onNodeWithTag("addBar.Element").performClick()
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
        // 0.3.3 barra: os Presets vêm pela grade do "+" (que fecha o conteúdo aberto).
        compose.onNodeWithTag("addBar.more").performClick()
        compose.onNodeWithTag("addBar.grid").assertIsDisplayed()
        compose.onAllNodesWithTag("add.panel").fetchSemanticsNodes().let { assertTrue("the grid closes the open content", it.isEmpty()) }
        compose.onNodeWithTag("addBar.grid.TextPresets").performClick()
        compose.waitUntil(3000) { compose.onAllNodesWithTag("addBar.grid").fetchSemanticsNodes().isEmpty() }
        compose.onNodeWithTag("add.presets.viewAll").assertIsDisplayed()
        compose.onNodeWithTag("add.presets.discover").assertIsDisplayed()
        capture("presets")
        compose.onNodeWithTag("add.close").performClick()
        compose.waitUntil(3000) { compose.onAllNodesWithTag("add.panel").fetchSemanticsNodes().isEmpty() }

        // Desenho age na hora: nenhum painel abre.
        compose.onNodeWithTag("addBar.more").performClick()
        compose.onNodeWithTag("addBar.grid.Draw").performClick()
        compose.waitForIdle()
        assertEquals(0, compose.onAllNodesWithTag("add.panel").fetchSemanticsNodes().size)
    }

    /** 0.3.3 barra: a pílula de 5, o "+" que abre a grade 4 × 2 e o "X" que fecha. */
    @Test fun addBarPillAndPlusGridOpenEachDestination() {
        launch("AUREA · 0.3.3 barra")
        for (tab in listOf("Shape", "Media", "Audio", "Text", "Element")) compose.onNodeWithTag("addBar.$tab").assertIsDisplayed()
        for (tab in listOf("TextPresets", "Model3D", "Draw", "Vector")) {
            assertEquals("$tab is only in the grid", 0, compose.onAllNodesWithTag("addBar.$tab").fetchSemanticsNodes().size)
        }
        // Categoria escolhida: a pílula acesa mostra o conteúdo dela acima da barra.
        compose.onNodeWithTag("addBar.Media").performClick()
        compose.onNodeWithTag("add.panel").assertIsDisplayed()
        capture("bar-media")
        // O "+" fecha o conteúdo e abre a grade; o "X" fecha.
        compose.onNodeWithTag("addBar.more").performClick()
        compose.onNodeWithTag("addBar.grid").assertIsDisplayed()
        for (tab in listOf("Shape", "Media", "Audio", "Text", "TextPresets", "Element", "Model3D", "Draw")) {
            compose.onNodeWithTag("addBar.grid.$tab").assertIsDisplayed()
        }
        compose.onNodeWithTag("addBar.close").assertIsDisplayed()
        capture("bar-grid")
        compose.onNodeWithTag("addBar.close").performClick()
        compose.waitUntil(3000) { compose.onAllNodesWithTag("addBar.grid").fetchSemanticsNodes().isEmpty() }
        // 3D pela grade: abre a grade de objetos 3D.
        compose.onNodeWithTag("addBar.more").performClick()
        compose.onNodeWithTag("addBar.grid.Model3D").performClick()
        compose.onNodeWithTag("add.model3d.shapes").assertIsDisplayed()
        compose.onNodeWithTag("add.close").performClick()
        compose.waitUntil(3000) { compose.onAllNodesWithTag("add.panel").fetchSemanticsNodes().isEmpty() }
        // Forma pela grade adiciona como pela pílula.
        compose.onNodeWithTag("addBar.more").performClick()
        compose.onNodeWithTag("addBar.grid.Shape").performClick()
        compose.onNodeWithTag("add.shape.0").performClick()
        compose.waitUntil(5000) { store.layers.size == 1 }
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

    /**
     * Omega exportação (bug 1 da 0.3.4: "quadro, sequência e ZIP saem como MP4"):
     * cada item da lista, escolhido pelo toque e exportado pelo botão da página,
     * termina no formato dele (o do exportador e a extensão do arquivo).
     */
    @Test fun eachExportOptionProducesItsOwnFileType() {
        launch("AUREA · Omega export kinds")
        compose.runOnIdle { store.addShape(0) }
        compose.waitUntil(5000) { store.layers.size == 1 }
        compose.runOnIdle { store.clearSelection() }
        for ((tag, format) in listOf("frame" to com.aurea.aurea.state.ExportFormat.Frame,
                "sequence" to com.aurea.aurea.state.ExportFormat.Sequence, "gif" to com.aurea.aurea.state.ExportFormat.Gif)) {
            compose.onNodeWithTag("editor.export").performClick()
            compose.onNodeWithTag("export.option.$tag").performScrollTo().performClick()
            compose.onNodeWithTag("export.go").performClick()
            compose.waitUntil(180_000) {
                store.exporter.state.phase.let { it == com.aurea.aurea.state.ExportPhase.Done || it == com.aurea.aurea.state.ExportPhase.Failed }
            }
            val state = store.exporter.state
            assertEquals("$tag: ${state.message}", com.aurea.aurea.state.ExportPhase.Done, state.phase)
            assertEquals(tag, format, state.format)
            assertTrue(tag, store.exporter.suggestedName().endsWith("." + format.extension))
            val mime = state.outputUri?.let { context.contentResolver.getType(it) }
            assertTrue("$tag: $mime", mime == null || mime == format.mime)
            capture("export-done-$tag")
            compose.onNodeWithTag("export.result.close").performClick()
            compose.waitUntil(5000) { compose.onAllNodesWithTag("export.result").fetchSemanticsNodes().isEmpty() }
        }
    }
}
