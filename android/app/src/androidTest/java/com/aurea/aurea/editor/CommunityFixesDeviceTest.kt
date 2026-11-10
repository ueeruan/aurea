package com.aurea.aurea.editor

import android.app.Application
import android.util.Log
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.nio.ByteBuffer

/** Community reports of 0.5.0, through the store and the device GPU. */
class CommunityFixesDeviceTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore
    private val width = 320
    private val height = 180

    private fun openProject(title: String) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(width, height, 30f, title) }
        compose.waitUntil(15000) { store.project.title == title && !store.projectOperationBusy }
    }

    private fun capture(): ByteArray {
        val pixels = ByteBuffer.allocateDirect(width * height * 4)
        val size = IntArray(2)
        assertEquals(width * height * 4, store.engineForStress.captureFrame(width, pixels, size))
        return ByteArray(pixels.capacity()).also { pixels.rewind(); pixels.get(it) }
    }

    private fun ByteArray.channel(x: Int, y: Int, c: Int) = this[(y * width + x) * 4 + c].toInt() and 255

    private fun lit(frame: ByteArray): Int {
        var count = 0
        for (y in 0 until height) for (x in 0 until width)
            if (maxOf(frame.channel(x, y, 0), frame.channel(x, y, 1), frame.channel(x, y, 2)) > 24) ++count
        return count
    }

    private fun select(layer: Long) {
        compose.runOnIdle { assertTrue(layer > 0); store.select(layer) }
        compose.waitUntil(10000) { store.primary == layer && store.detail?.id == layer }
    }

    /** Commands reach the engine asynchronously: poll until the frame shows the edit. */
    private fun captureUntil(what: String, done: (ByteArray) -> Boolean): ByteArray {
        val deadline = System.currentTimeMillis() + 10000
        var frame = capture()
        while (!done(frame) && System.currentTimeMillis() < deadline) {
            compose.waitForIdle()
            Thread.sleep(100)
            frame = capture()
        }
        assertTrue(what, done(frame))
        return frame
    }

    @Test fun transparentFillWithStrokeLeavesTheGlyphCoreEmpty() {
        openProject("Traco vazado")
        val layer = store.engineForStress.addText("O")
        select(layer)
        compose.runOnIdle { store.setTextSize(170f); store.setTextColor(1f, 1f, 1f, 1f) }
        val plain = captureUntil("texto branco grande") { lit(it) > 3000 }
        val core = ArrayList<Pair<Int, Int>>()
        val empty = ArrayList<Pair<Int, Int>>()
        for (y in 5 until height - 5) for (x in 5 until width - 5) {
            var inside = true
            var outside = true
            for (dy in -4..4) for (dx in -4..4) {
                val g = plain.channel(x + dx, y + dy, 1)
                inside = inside && g >= 250
                outside = outside && g <= 2
            }
            if (inside) core += x to y
            if (outside) empty += x to y
        }
        assertTrue("miolo do glifo: ${core.size}", core.size > 100)
        compose.runOnIdle {
            store.setTextStrokeWidth(8f)
            store.setTextStrokeColor(1f, 0f, 0f, 1f)
            store.setTextColor(1f, 1f, 1f, 0f)
        }
        val leak: (ByteArray) -> Int = { f -> core.count { (x, y) -> f.channel(x, y, 0) > 12 } }
        val ring: (ByteArray) -> Int = { f -> empty.count { (x, y) -> f.channel(x, y, 0) > 200 && f.channel(x, y, 1) < 40 } }
        val hollow = captureUntil("so o anel do contorno") { ring(it) > 100 && leak(it) == 0 }
        Log.i("CommunityFixes", "traco: miolo ${core.size}, vazamento ${leak(hollow)}, anel ${ring(hollow)}")
    }

    @Test fun animatedTextWithBlurAndEffectStillPresentsFrames() {
        openProject("Texto borrado com efeito")
        val engine = store.engineForStress
        val layer = engine.addText("Texto Aurea")
        select(layer)
        compose.runOnIdle {
            assertTrue(engine.setMotionBlur(layer, true))
            assertTrue(engine.applyTextPreset(layer, 8))
            store.seek(15)
        }
        val before = captureUntil("texto no meio da animacao") { lit(it) > 50 }
        compose.runOnIdle { store.addEffect(effectTypeId("aurea.blur.gaussian"), listOf(layer)) }
        compose.waitUntil(10000) { store.effects.isNotEmpty() }
        val times = LongArray(5)
        var frame = before
        for (i in times.indices) {
            val start = System.nanoTime()
            frame = capture()
            times[i] = (System.nanoTime() - start) / 1_000_000
        }
        Log.i("CommunityFixes", "texto+blur+efeito: captura ms ${times.joinToString()}, antes ${lit(before)}, depois ${lit(frame)}")
        assertTrue("o quadro com efeito precisa ter o texto", lit(frame) > 50)
        assertTrue("captura demorou: ${times.joinToString()}", times.max() < 15000)
    }

    @Test fun cubeWithEffectKeepsRenderingNextToAnotherCube() {
        openProject("Cubo com efeito")
        val engine = store.engineForStress
        val floor = engine.addShape3d(0, "Piso")
        val cube = engine.addShape3d(0, "Cubo")
        select(cube)
        val before = captureUntil("cena 3D") { lit(it) > 200 }
        compose.runOnIdle { store.addEffect(effectTypeId("aurea.color.saturation"), listOf(cube)) }
        compose.waitUntil(10000) { store.effects.isNotEmpty() }
        val after = captureUntil("cena 3D com efeito") { lit(it) > 200 }
        Log.i("CommunityFixes", "cubo: piso $floor, antes ${lit(before)}, depois ${lit(after)}")
    }
}
