package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.UI_MODE_COMPOSITION
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.abs

/**
 * Relato 0.2.5 (vídeo de 18/10/2026): DIVIDIR um Nulo que é pai de uma
 * pré-composição mudava a prévia. Aqui o corte sai pelo botão "Dividir" do
 * Dock da Composição, nos dois nulos da cadeia (Nulo 4 → Nulo 5 → pré-comp),
 * e a imagem de cada quadro tem de continuar a mesma.
 */
class SplitParentNullUiTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext

    private fun screenshot(name: String) {
        compose.waitForIdle()
        Thread.sleep(650)
        instrumentation.waitForIdleSync()
        val bitmap = checkNotNull(instrumentation.uiAutomation.takeScreenshot())
        File(context.filesDir, "ui-$name.png").outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        bitmap.recycle()
    }

    /** Duas metades de cor: girar e andar aparecem na imagem. */
    private fun importHalves(): Long {
        val w = 64; val h = 36
        val buf = ByteBuffer.allocateDirect(w * h * 4).order(ByteOrder.nativeOrder())
        for (y in 0 until h) for (x in 0 until w) {
            if (x < w / 2) { buf.put(230.toByte()); buf.put(60.toByte()); buf.put(60.toByte()) }
            else { buf.put(60.toByte()); buf.put(110.toByte()); buf.put(230.toByte()) }
            buf.put(255.toByte())
        }
        buf.rewind()
        return store.engineForStress.importImage(buf, w, h, "metades", "test://metades")
    }

    private fun capture(frame: Int): ByteArray {
        compose.runOnIdle { store.seek(frame) }
        compose.waitUntil(5000) { store.playhead == frame }
        val out = ByteBuffer.allocateDirect(160 * 160 * 4)
        val size = IntArray(2)
        var bytes = 0
        compose.runOnIdle { bytes = store.engineForStress.captureFrame(160, out, size) }
        assertEquals(160 * 90 * 4, bytes)
        return ByteArray(bytes).also { out.position(0); out.get(it) }
    }

    private fun meanDiff(a: ByteArray, b: ByteArray): Double {
        assertEquals(a.size, b.size)
        var sum = 0L
        for (i in a.indices) sum += abs((a[i].toInt() and 0xFF) - (b[i].toInt() and 0xFF))
        return sum.toDouble() / a.size
    }

    private fun splitWithTheDock(layer: Long, frame: Int, expectedLayers: Int) {
        compose.runOnIdle { store.select(layer); store.seek(frame) }
        compose.waitUntil(5000) { store.playhead == frame && store.selection == setOf(layer) }
        compose.waitUntil(5000) { compose.onAllNodesWithTag("timeline.cut.split").fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithTag("timeline.cut.split").performClick()
        compose.waitUntil(5000) { store.layers.size == expectedLayers }
    }

    @Test fun splittingTheParentNullsWithTheDockKeepsThePreview() {
        assertTrue(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(320, 180, 30.0, "Nulo pai", null, UI_MODE_COMPOSITION) }
        compose.waitUntil(15000) { store.project.title == "Nulo pai" && !store.projectOperationBusy }

        var pre = 0L; var n4 = 0L; var n5 = 0L
        compose.runOnIdle {
            val engine = store.engineForStress
            val image = importHalves()
            assertTrue(image > 0)
            pre = engine.precompose(longArrayOf(image))
            n4 = engine.addNull(false)
            n5 = engine.addNull(false)
            assertTrue(pre > 0 && n4 > 0 && n5 > 0)
            engine.beginCommandBatch()
            val c = CommandBatch(engine)
            assertTrue(c.setLayerParent(n5, n4))
            assertTrue(c.setLayerParent(pre, n5))
            assertTrue(c.insertKeyframe(n4, TrackProperty.POSITION_X, -1, 0, 0, 100f))
            assertTrue(c.insertKeyframe(n4, TrackProperty.POSITION_X, -1, 0, 80, 220f))
            assertTrue(c.insertKeyframe(n5, TrackProperty.ROTATION_Z, -1, 0, 0, 0f))
            assertTrue(c.insertKeyframe(n5, TrackProperty.ROTATION_Z, -1, 0, 80, 40f))
            assertEquals(6, engine.submitCommands())
        }
        compose.waitUntil(10000) { store.layers.size == 3 }

        val frames = intArrayOf(10, 39, 40, 41, 70)
        val before = frames.map { capture(it) }
        assertTrue("os nulos andam", meanDiff(before.first(), before.last()) > 2.0)
        screenshot("split-null-before")

        // Os dois nulos, um depois do outro, no mesmo cabeçote (como no vídeo).
        splitWithTheDock(n5, 40, 4)
        splitWithTheDock(n4, 40, 5)
        screenshot("split-null-after")

        frames.forEachIndexed { i, f ->
            val d = meanDiff(before[i], capture(f))
            assertTrue("quadro $f mudou depois de dividir os nulos (diferença $d)", d < 0.5)
        }
        // Os pedaços: o original termina no corte e o novo começa nele.
        compose.runOnIdle {
            val first = store.layers.single { it.id == n5 }
            val second = store.layers.single { it.id !in setOf(pre, n4, n5) && it.startFrame == 40 && it.name.startsWith(first.name) }
            assertEquals(40, first.endFrame)
            assertEquals(40, second.startFrame)
        }
    }
}
