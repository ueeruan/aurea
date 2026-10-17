package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.os.SystemClock
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.editor.panels.paramOf
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer

/**
 * 0.2.1: os 17 padrões novos do Dissolver (mapa 5..21) e a superfície da rede
 * do Particular, no aparelho: monta pela loja do editor, captura a prévia e
 * grava as imagens para conferência.
 */
class DissolvePatternsPlexusSurfaceDeviceTest {
    @get:Rule val compose = createComposeRule()
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private lateinit var store: EditorStore
    private val out by lazy { File(context.filesDir, "fx-021").apply { mkdirs() } }

    private fun initialize() {
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
    }

    private fun project(title: String) {
        compose.runOnIdle { store.newProject(640, 360, 30f, title) }
        compose.waitUntil(15000) { store.project.title == title && !store.projectOperationBusy }
    }

    private fun capture(): Bitmap {
        SystemClock.sleep(300)
        val buffer = ByteBuffer.allocateDirect(480 * 480 * 4)
        val size = IntArray(2)
        var bytes = 0
        val deadline = SystemClock.elapsedRealtime() + 60000
        while (bytes <= 0 && SystemClock.elapsedRealtime() < deadline) {
            buffer.clear()
            bytes = store.engineForStress.capturePreviewFrame(480, buffer, size)
            if (bytes <= 0) SystemClock.sleep(200)
        }
        assertTrue("captura falhou", bytes > 0)
        val bmp = Bitmap.createBitmap(size[0], size[1], Bitmap.Config.ARGB_8888)
        buffer.rewind(); bmp.copyPixelsFromBuffer(buffer)
        return bmp
    }

    private fun save(b: Bitmap, name: String) =
        File(out, "$name.png").outputStream().use { b.compress(Bitmap.CompressFormat.PNG, 100, it) }

    private fun pixels(b: Bitmap) = IntArray(b.width * b.height).also { b.getPixels(it, 0, b.width, 0, 0, b.width, b.height) }

    private fun diff(a: IntArray, b: IntArray): Double {
        var sum = 0L
        for (i in 0 until minOf(a.size, b.size)) {
            val x = a[i]; val y = b[i]
            sum += Math.abs((x shr 16 and 255) - (y shr 16 and 255)) + Math.abs((x shr 8 and 255) - (y shr 8 and 255)) + Math.abs((x and 255) - (y and 255))
        }
        return sum.toDouble() / minOf(a.size, b.size)
    }

    private fun setParam(effect: Int, index: Int, value: Float) {
        compose.runOnIdle { store.setEffectParam(effect, store.paramOf(effect, index)!!, value) }
        compose.waitUntil(5000) { store.paramOf(effect, index)?.value?.firstOrNull() == value }
    }

    @Test fun seventeenDissolvePatternsRenderDistinctFrames() {
        initialize()
        project("Dissolve 021")
        compose.runOnIdle { store.addShape(0); store.addEffect(effectTypeId("aurea.transition.luma_dissolve")) }
        compose.waitUntil(10000) { store.effects.size == 1 }
        val effect = store.effects.single().effectId
        compose.waitUntil(10000) { store.effectParams[effect].orEmpty().size >= 14 }
        setParam(effect, 0, 50f)   // progresso
        setParam(effect, 1, 0f)    // suavidade
        val base = pixels(capture())
        val frames = (5..21).map { kind ->
            setParam(effect, 3, kind.toFloat())
            val img = capture()
            save(img, "dissolve_padrao_${kind - 4}")
            pixels(img)
        }
        val report = StringBuilder("Dissolver: 17 padroes no aparelho (diferenca media RGB)\n")
        frames.forEachIndexed { i, f -> report.append("padrao ${i + 1}: x luminancia ${"%.2f".format(diff(base, f))}\n") }
        var distinct = 0
        for (i in frames.indices) for (j in i + 1 until frames.size) if (diff(frames[i], frames[j]) > 1.0) distinct++
        report.append("pares distintos: $distinct de ${frames.size * (frames.size - 1) / 2}\n")
        File(out, "dissolve.txt").writeText(report.toString())
        assertEquals("todos os pares de padroes devem diferir", frames.size * (frames.size - 1) / 2, distinct)
        // Rotação e detalhe mudam o padrão.
        setParam(effect, 12, 30f)
        setParam(effect, 13, 90f)
        val turned = pixels(capture())
        assertTrue("rotacao/detalhe mudam o padrao", diff(frames.last(), turned) > 1.0)
    }

    @Test fun plexusSurfaceFillsBetweenConnectedPoints() {
        initialize()
        project("Plexus 021")
        var layer = 0L
        compose.runOnIdle {
            layer = store.engineForStress.addParticles(28)   // rede
            assertTrue("particulas", layer > 0)
            store.select(layer)
        }
        compose.waitUntil(15000) { store.effects.isNotEmpty() }
        val effect = store.effects.first().effectId
        compose.waitUntil(10000) { store.effectParams[effect].orEmpty().size >= 48 }
        compose.runOnIdle { store.seek(30) }
        compose.waitUntil(15000) { store.playhead == 30 }
        val lines = capture(); save(lines, "plexus_linhas")
        setParam(effect, 45, 1f)    // superfície da rede
        setParam(effect, 46, 30f)   // opacidade
        val faces = capture(); save(faces, "plexus_superficie")
        val d = diff(pixels(lines), pixels(faces))
        File(out, "plexus.txt").writeText("Superficie da rede: diferenca media RGB linhas x faces = ${"%.2f".format(d)}\n")
        assertTrue("a superficie deve mudar o quadro", d > 1.0)
        setParam(effect, 46, 0f)
        assertTrue("opacidade 0 = so linhas", diff(pixels(lines), pixels(capture())) < 1.0)
    }
}
