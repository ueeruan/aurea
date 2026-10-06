package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import android.os.Looper
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
import java.io.File
import java.nio.ByteBuffer

/** Device counterparts of the three wall/margin regressions in MotionTileChainsGpu.inl. */
class MotionTileMarginsDeviceTest {
    @get:Rule val compose = createComposeRule()
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private lateinit var store: EditorStore
    private lateinit var folder: File
    private lateinit var report: File
    private var layer = 0L
    private val output = ByteBuffer.allocateDirect(160 * 90 * 4)

    private fun launchEditor(name: String) {
        check(context.packageName == "com.aurea.aurea.uitest")
        folder = File(context.filesDir, "motion-tile-margins").apply { mkdirs() }
        report = File(folder, "$name.txt").apply { writeText("Motion Tile margins: $name\n") }
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
    }

    private fun newSource(name: String) {
        compose.runOnIdle { store.newProject(160, 90, 30f, name) }
        compose.waitUntil(20000) { store.project.title == name && !store.projectOperationBusy }
        compose.runOnIdle { store.setCompositionBackground(0f, 0f, 0f, 1f) }
        val source = File(folder, "$name-source.png")
        val bitmap = Bitmap.createBitmap(160, 90, Bitmap.Config.ARGB_8888)
        val rgba = ByteBuffer.allocateDirect(160 * 90 * 4)
        try {
            bitmap.eraseColor(Color.rgb(200, 200, 200))
            source.outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
            bitmap.copyPixelsToBuffer(rgba)
        } finally { bitmap.recycle() }
        rgba.rewind()
        layer = store.engineForStress.importImage(rgba, 160, 90, "Uniform 200 source", source.absolutePath)
        assertTrue("Import uniform source", layer > 0)
        compose.runOnIdle { store.select(layer) }
        compose.waitUntil(10000) { store.primary == layer && store.detail?.id == layer && store.effects.isEmpty() }
        assertUniform("$name-before-effects")
    }

    private fun add(key: String): Int {
        val type = effectTypeId(key)
        compose.runOnIdle { store.addEffect(type, listOf(layer)) }
        compose.waitUntil(10000) { store.effects.any { it.typeId == type } }
        val id = store.effects.single { it.typeId == type }.effectId
        compose.waitUntil(10000) { store.effectParams[id]?.isNotEmpty() == true }
        return id
    }

    private fun set(id: Int, index: Int, value: Float, component: Int = 0) {
        compose.runOnIdle {
            val param = store.effectParams.getValue(id).single { it.index == index }
            store.setEffectParam(id, param, value, component)
        }
        compose.waitUntil(10000) {
            store.effectParams[id]?.singleOrNull { it.index == index }?.value?.get(component) == value
        }
    }

    private fun assertStack(vararg keys: String) {
        compose.runOnIdle {
            assertEquals(keys.map { effectTypeId(it) }, store.effects.map { it.typeId })
        }
    }

    private fun assertUniform(label: String) {
        assertNotEquals("GPU capture must not block main", Looper.getMainLooper(), Looper.myLooper())
        val dimensions = IntArray(2)
        output.clear()
        val count = store.engineForStress.captureFrame(160, output, dimensions)
        assertEquals("$label capture bytes", 160 * 90 * 4, count)
        assertArrayEquals(intArrayOf(160, 90), dimensions)
        var minimum = 255
        var maximum = 0
        var dark = 0
        var bright = 0
        var transparent = 0
        val colors = IntArray(160 * 90)
        for (pixel in colors.indices) {
            val at = pixel * 4
            val r = output.get(at).toInt() and 255
            val g = output.get(at + 1).toInt() and 255
            val b = output.get(at + 2).toInt() and 255
            val a = output.get(at + 3).toInt() and 255
            val low = minOf(r, g, b)
            val high = maxOf(r, g, b)
            minimum = minOf(minimum, low)
            maximum = maxOf(maximum, high)
            if (low < 190) dark++
            if (high > 210) bright++
            if (a < 254) transparent++
            colors[pixel] = Color.argb(a, r, g, b)
        }
        val diagnostic = "$label min=$minimum max=$maximum dark=$dark bright=$bright transparent=$transparent/${colors.size}"
        Log.i("AureaTileMargins", diagnostic)
        report.appendText(diagnostic + "\n")
        val image = Bitmap.createBitmap(colors, 160, 90, Bitmap.Config.ARGB_8888)
        try {
            File(folder, "$label.png").outputStream().use { assertTrue(image.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        } finally { image.recycle() }
        assertEquals("$diagnostic: the wall must cover every edge pixel", 0, dark)
        assertEquals("$diagnostic: gray source must not become a white placeholder", 0, bright)
        assertEquals("$diagnostic: opaque composition must remain opaque", 0, transparent)
        assertTrue("$diagnostic: a uniform source must stay uniform", maximum - minimum <= 10)
    }

    @Test fun defaultTileProvidesBlurMarginWithMirrorOff() {
        launchEditor("default-before-blur")
        newSource("default-before-blur")
        val tile = add("aurea.stylize.motion_tile")
        compose.runOnIdle { assertEquals(0f, store.effectParams.getValue(tile).single { it.index == 5 }.value[0], 0f) }
        val blur = add("aurea.blur.gaussian")
        set(blur, 0, 12f)
        assertStack("aurea.stylize.motion_tile", "aurea.blur.gaussian")
        assertUniform("default-before-blur-12")
    }

    @Test fun defaultTileUsesTheImageAfterAShrinkingTransformWithMirrorOff() {
        launchEditor("transform-before-default")
        newSource("transform-before-default")
        val transform = add("aurea.transform")
        set(transform, 2, 50f, 0)
        set(transform, 2, 50f, 1)
        val tile = add("aurea.stylize.motion_tile")
        compose.runOnIdle { assertEquals(0f, store.effectParams.getValue(tile).single { it.index == 5 }.value[0], 0f) }
        assertStack("aurea.transform", "aurea.stylize.motion_tile")
        assertUniform("transform-50-before-default")
    }

    @Test fun mirroredTilePreservesBlurMarginAcrossScaleAndRotation() {
        launchEditor("mirror-transform-blur")
        for (scale in listOf(10f, 25f)) for (rotation in listOf(0f, 33f)) {
            val label = "mirror-scale-${scale.toInt()}-rotation-${rotation.toInt()}"
            newSource(label)
            val tile = add("aurea.stylize.motion_tile")
            set(tile, 5, 1f)
            val transform = add("aurea.transform")
            set(transform, 2, scale, 0)
            set(transform, 2, scale, 1)
            set(transform, 3, rotation)
            val blur = add("aurea.blur.gaussian")
            set(blur, 0, 24f)
            assertStack("aurea.stylize.motion_tile", "aurea.transform", "aurea.blur.gaussian")
            assertUniform("$label-blur-24")
        }
    }
}
