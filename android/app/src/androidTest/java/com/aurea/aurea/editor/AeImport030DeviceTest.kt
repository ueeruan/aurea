package com.aurea.aurea.editor

import android.app.Application
import android.net.Uri
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File

/**
 * 0.3.0: importar do After Effects no aparelho — um script .jsx (camada de
 * texto, animador, keyframes com easing e uma expressão) pelo caminho da
 * interface; o relatório diz o que entrou, o que ficou de fora (File/system
 * nunca rodam) e um desfazer volta tudo.
 */
class AeImport030DeviceTest {
    @get:Rule val compose = createComposeRule()

    private val script = """
        app.beginUndoGroup("Aurea");
        var comp = app.project.activeItem;
        var t = comp.layers.addText("Olá AE");
        var pos = t.property("ADBE Transform Group").property("ADBE Position");
        pos.setValueAtTime(0, [100, 200]);
        pos.setValueAtTime(1, [400, 200]);
        var ease = new KeyframeEase(0, 75);
        pos.setTemporalEaseAtKey(1, [ease], [ease]);
        t.property("ADBE Transform Group").property("ADBE Opacity").expression = "wiggle(2, 20)";
        var f = new File("/sdcard/segredo.txt");
        f.open("w");
        app.endUndoGroup();
    """.trimIndent()

    @Test fun jsxScriptCreatesAnimatedTextAndReportsBlockedCalls() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(640, 360, 30f, "AE 030") }
        compose.waitUntil(10000) { store.project.title == "AE 030" && !store.projectOperationBusy }
        val file = File(context.cacheDir, "teste-030.jsx").apply { writeText(script) }

        compose.runOnIdle { store.importAeFile(Uri.fromFile(file)) }
        compose.waitUntil(20000) { store.aeImportResult != null }
        val r = store.aeImportResult!!
        assertTrue("o script entrou: ${r.message}", r.ok)
        assertEquals("jsx", r.kind)
        assertEquals(1, r.layers)
        assertTrue("keyframes de posição (${r.keyframes})", r.keyframes >= 2)
        assertTrue("expressão de opacidade (${r.expressions})", r.expressions >= 1)
        // File nunca roda: aparece no relatório com a linha do script.
        assertTrue(r.skipped.any { it.line in 10..11 })
        compose.waitUntil(5000) { store.layers.size == 1 }
        val text = store.layers.single()
        assertTrue(store.keyframes[text.id].orEmpty().size >= 2)

        // Um desfazer volta o projeto como estava.
        compose.runOnIdle { store.dismissAeImport(); store.undo() }
        compose.waitUntil(5000) { store.layers.isEmpty() }
        file.delete()
    }
}
