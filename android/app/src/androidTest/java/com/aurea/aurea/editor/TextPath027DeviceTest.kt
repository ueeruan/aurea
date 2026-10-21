package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.*
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer

/**
 * 0.2.7: Texto no caminho com as opções do After Effects pela tela — máscara da
 * própria camada como caminho (sem recortar o texto), keyframe da margem
 * inicial, forçar alinhamento e desfazer. O render é o do motor Vulkan.
 */
class TextPath027DeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun maskBecomesThePathWithoutClippingAndTheMarginsAnimate() {
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
        compose.runOnIdle { store.newProject(640, 360, 30f, "Texto no caminho 027") }
        compose.waitUntil(10000) { store.project.title == "Texto no caminho 027" && !store.projectOperationBusy }
        compose.runOnIdle { store.addText(); store.dismissTextContentEditor() }
        compose.waitUntil(5000) { store.textStyle != null }
        compose.runOnIdle { store.setTextContent("AUREA AUREA"); store.setTextSize(48f); store.setTextColor(1f, 0f, 0f, 1f) }
        val id = checkNotNull(store.primary)
        fun redPixels(): Int {
            compose.waitForIdle()
            val buffer = ByteBuffer.allocateDirect(640 * 360 * 4)
            assertEquals(640 * 360 * 4, store.engineForStress.captureFrame(640, buffer, IntArray(2)))
            var n = 0
            for (i in 0 until 640 * 360) {
                val r = buffer.get(i * 4).toInt() and 255
                val g = buffer.get(i * 4 + 1).toInt() and 255
                if (r > 120 && r > g + 70) ++n
            }
            return n
        }
        val straight = redPixels()
        assertTrue("o texto aparece ($straight px)", straight > 300)
        // Máscara fechada (elipse) na própria camada: recorta o texto…
        compose.runOnIdle { store.addMaskPreset(1); store.maskEdit = null }
        compose.waitUntil(5000) { store.masks?.masks?.isNotEmpty() == true }
        val maskId = store.masks!!.masks.first().id
        // …até virar o caminho dele, pela tela.
        compose.onNodeWithTag("dock.tool.TextOptions").performScrollTo().performClick()
        // A fileira de chips rola na horizontal: primeiro o painel desce até ela.
        compose.onNodeWithTag("text.path.sources").performScrollTo()
        compose.onNodeWithTag("text.path.mask.0").performClick()
        compose.waitUntil(5000) { store.textPathState?.mask == maskId }
        val onPath = redPixels()
        assertTrue("a máscara-caminho não recorta ($onPath de $straight px)", onPath > straight * 6 / 10)
        // Keyframe da margem inicial no instante.
        compose.onNodeWithTag("text.path.first.key").performScrollTo().assertIsEnabled().performClick()
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().any { it.property == TrackProperty.TEXT_PATH_FIRST_MARGIN && it.time == 0 } }
        // Forçar alinhamento e desfazer.
        compose.onNodeWithTag("text.path.force").performScrollTo().performClick()
        compose.waitUntil(5000) { store.textPathState?.forceAlign == true }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.textPathState?.forceAlign == false }
        assertEquals(maskId, store.textPathState?.mask)
        File(context.filesDir, "textpath027-result.txt").writeText(
            "PASS: mascara como caminho ($onPath de $straight px), keyframe da margem inicial, forcar alinhamento e desfazer\n")
    }
}
