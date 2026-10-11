package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.Text3DInfo
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer

/** Real GPU checks of the October fixes, exclusively in the isolated uitest app. */
class OctoberBugReportDeviceTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext

    private fun launch() {
        check(context.packageName.endsWith(".uitest"))
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ::store.isInitialized && store.engineReady }
        compose.runOnIdle { store.newProject(320, 240, 30f, "October bug report") }
        compose.waitUntil(15000) { store.project.title == "October bug report" }
    }

    private fun capture(name: String): ByteArray {
        val buffer = ByteBuffer.allocateDirect(320 * 240 * 4)
        val size = IntArray(2)
        val count = store.engineForStress.captureFrame(320, buffer, size)
        assertEquals(320 * 240 * 4, count)
        val pixels = ByteArray(count).also { buffer.rewind(); buffer.get(it) }
        val bitmap = Bitmap.createBitmap(320, 240, Bitmap.Config.ARGB_8888)
        bitmap.setPixels(IntArray(320 * 240) { i ->
            val p = i * 4
            Color.argb(pixels[p + 3].toInt() and 255, pixels[p].toInt() and 255,
                pixels[p + 1].toInt() and 255, pixels[p + 2].toInt() and 255)
        }, 0, 320, 0, 0, 320, 240)
        try {
            val folder = File(context.getExternalFilesDir(null), "stability-screenshots").apply { mkdirs() }
            File(folder, "$name.png").outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        } finally { bitmap.recycle() }
        return pixels
    }

    @Test fun japaneseGlyphsDifferIn2DAnd3DAndImportedFontAcceptsGlow() {
        launch()
        val engine = store.engineForStress
        val first = "こんにちは世界"
        val second = "あいうえお日本"
        fun changed(a: ByteArray, b: ByteArray) = a.indices.count { a[it] != b[it] }
        fun ink(a: ByteArray) = (a.indices step 4).count {
            (a[it].toInt() and 255) > 20 || (a[it + 1].toInt() and 255) > 20 || (a[it + 2].toInt() and 255) > 20
        }
        compose.runOnIdle { store.addText(openEditor = false); store.setTextSize(36f); store.setTextContent(first) }
        compose.waitUntil(10000) { store.layers.isNotEmpty() }
        val a = capture("october-japanese-2d-first")
        compose.runOnIdle { store.setTextContent(second) }
        val b = capture("october-japanese-2d-second")
        assertTrue("Japanese 2D text must render", ink(a) > 200)
        assertTrue("Different Japanese characters rendered identical missing-glyph boxes", changed(a, b) > 100)
        compose.runOnIdle { store.newProject(320, 240, 30f, "October Japanese 3D") }
        compose.waitUntil(15000) { store.project.title == "October Japanese 3D" }
        var layer = 0L
        compose.runOnIdle { layer = store.addText3D(openEditor = false) }
        val font = File(context.filesDir, "october-imported-font.ttf")
        context.assets.open("Roboto-Regular.ttf").use { input -> font.outputStream().use { input.copyTo(it) } }
        assertNotNull("Custom font must register in the shared font manager", engine.importFont(font.absolutePath))
        val info = Text3DInfo(first, .25f, 1, floatArrayOf(1f, 1f, 1f, 1f), fontPath = font.absolutePath)
        compose.runOnIdle { store.select(layer); store.setText3D(info) }
        assertTrue("3D text must keep its imported font", engine.queryText3dFont(layer).endsWith("october-imported-font.ttf"))
        val c = capture("october-japanese-3d-first")
        compose.runOnIdle { store.setText3D(info.copy(content = second)) }
        val d = capture("october-japanese-3d-second")
        assertTrue("Japanese 3D outlines must render", ink(c) > 200)
        assertTrue("3D Japanese outlines must differ", changed(c, d) > 100)
        compose.runOnIdle { store.addEffect(effectTypeId("aurea.light.glow"), listOf(layer)) }
        compose.waitUntil(10000) { store.effects.any { it.typeId == effectTypeId("aurea.light.glow") } }
        val glow = capture("october-japanese-3d-glow")
        assertTrue("Glow must change the rendered 3D text", changed(d, glow) > 100)
    }

    @Test fun rotoBrushSelectsTheTouchedObjectAfterRotationAndPerspective() {
        launch()
        val engine = store.engineForStress
        val bitmap = Bitmap.createBitmap(160, 120, Bitmap.Config.ARGB_8888)
        bitmap.setPixels(IntArray(160 * 120) { i ->
            val x = i % 160; val y = i / 160
            if ((x - 80) * (x - 80) + (y - 60) * (y - 60) < 24 * 24) Color.rgb(30, 230, 30)
            else Color.rgb(30 + (x * 7 + y * 11) % 12, 35, 140)
        }, 0, 160, 0, 0, 160, 120)
        val source = File(context.filesDir, "october-roto.png")
        source.outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        val rgba = ByteBuffer.allocateDirect(160 * 120 * 4)
        bitmap.copyPixelsToBuffer(rgba); rgba.rewind(); bitmap.recycle()
        var layer = 0L
        compose.runOnIdle {
            layer = engine.importImage(rgba, 160, 120, "Touched object", source.absolutePath)
            assertTrue(layer > 0); store.select(layer)
        }
        compose.waitUntil(10000) { store.layers.any { it.id == layer } }
        compose.runOnIdle {
            store.addEffect(effectTypeId("aurea.key.rotobrush"), listOf(layer))
        }
        compose.waitUntil(10000) { store.effects.any { it.typeId == effectTypeId("aurea.key.rotobrush") } }
        val effect = store.effects.first { it.typeId == effectTypeId("aurea.key.rotobrush") }.effectId
        assertTrue(engine.layoutTransform(layer, 3, 1.2f))
        assertTrue(engine.layoutTransform(layer, 4, .9f))
        assertTrue(engine.layoutTransform(layer, 8, 17f))
        for (perspective in listOf(false, true)) {
            assertTrue(engine.layoutTransform(layer, 6, if (perspective) 22f else 0f))
            assertTrue(engine.layoutTransform(layer, 7, if (perspective) 38f else 0f))
            // Layout writes are queued. Read geometry only after the production
            // status loop has observed all submitted transforms.
            compose.waitUntil(10000) {
                store.detail?.let { it.id == layer && it.rotation[2] == 17f &&
                    it.rotation[0] == (if (perspective) 22f else 0f) &&
                    it.rotation[1] == (if (perspective) 38f else 0f) && it.scale[0] == 1.2f && it.scale[1] == .9f } == true
            }
            val d = store.detail!!
            assertEquals("The fixture must exercise the requested projection", perspective, d.perspective)
            val c = FloatArray(8)
            assertTrue(LayerGeometry.corners(d, c))
            // Diagonals intersect at the projected source center, including perspective.
            val ax = c[4] - c[0]; val ay = c[5] - c[1]
            val bx = c[6] - c[2]; val by = c[7] - c[3]
            val determinant = ax * by - ay * bx
            assertTrue(kotlin.math.abs(determinant) > .001f)
            val t = ((c[2] - c[0]) * by - (c[3] - c[1]) * bx) / determinant
            val center = floatArrayOf(c[0] + t * ax, c[1] + t * ay)
            if (perspective) assertTrue(engine.rotoUndoStroke(layer, effect))
            assertTrue(engine.rotoAddStroke(layer, effect, false, 7f, center))
            assertTrue(engine.rotoSetView(layer, effect, 0))
            val image = capture("october-roto-${if (perspective) "3d" else "2d"}")
            val x = center[0].toInt().coerceIn(0, 319); val y = center[1].toInt().coerceIn(0, 239)
            val g = image[(y * 320 + x) * 4 + 1].toInt() and 255
            assertTrue("Roto lost the touched object (perspective=$perspective, green=$g)", g > 160)
            val backgroundX = (c[0] * .85f + center[0] * .15f).toInt().coerceIn(0, 319)
            val backgroundY = (c[1] * .85f + center[1] * .15f).toInt().coerceIn(0, 239)
            val blue = image[(backgroundY * 320 + backgroundX) * 4 + 2].toInt() and 255
            assertTrue("Roto kept the distant background (blue=$blue)", blue < 20)
            assertTrue(engine.rotoSetView(layer, effect, 2))
            val overlay = capture("october-roto-overlay-${if (perspective) "3d" else "2d"}")
            val p = (backgroundY * 320 + backgroundX) * 4
            val red = overlay[p].toInt() and 255
            val overlayBlue = overlay[p + 2].toInt() and 255
            assertTrue("Red overlay did not follow the transformed background: perspective=$perspective xy=$backgroundX,$backgroundY rgb=$red,${overlay[p + 1].toInt() and 255},$overlayBlue corners=${c.contentToString()}", red > overlayBlue)
        }
        assertTrue(engine.rotoSetView(layer, effect, 0))
    }
}
