package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Color
import android.graphics.Bitmap
import android.util.Log
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import java.io.File
import java.nio.ByteBuffer
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

/** Real Android GPU captures at exact timeline cuts; independent uitest data. */
class SceneCutDeviceTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore

    private fun project(title: String) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(320, 180, 30f, title) }
        compose.waitUntil(15000) { store.project.title == title }
    }

    private fun capture(frame: Int): ByteArray {
        compose.runOnIdle { store.seek(frame) }
        compose.waitUntil(10000) { store.playhead == frame }
        val pixels = ByteBuffer.allocateDirect(320 * 180 * 4)
        val size = IntArray(2)
        val count = store.engineForStress.captureFrame(320, pixels, size)
        assertEquals(320 * 180 * 4, count)
        return ByteArray(count).also { pixels.rewind(); pixels.get(it) }
    }

    private fun difference(a: ByteArray, b: ByteArray): Int = a.indices.maxOf {
        kotlin.math.abs((a[it].toInt() and 255) - (b[it].toInt() and 255))
    }

    private fun lit(a: ByteArray): Int = (a.indices step 4).count {
        (a[it].toInt() and 255) > 10 || (a[it + 1].toInt() and 255) > 10 || (a[it + 2].toInt() and 255) > 10
    }

    private fun saveCapture(name: String, pixels: ByteArray) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val directory = File(context.getExternalFilesDir(null), "stability-screenshots").apply { mkdirs() }
        val bitmap = Bitmap.createBitmap(320, 180, Bitmap.Config.ARGB_8888)
        try {
            val colors = IntArray(320 * 180) { index ->
                val i = index * 4
                Color.argb(pixels[i + 3].toInt() and 255, pixels[i].toInt() and 255,
                    pixels[i + 1].toInt() and 255, pixels[i + 2].toInt() and 255)
            }
            bitmap.setPixels(colors, 0, 320, 0, 0, 320, 180)
            File(directory, name).outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        } finally { bitmap.recycle() }
    }

    @Test fun cameraStopsAtItsExclusiveEndAndReverseSeekingDoesNotKeepItsView() {
        project("Camera cut device")
        val engine = store.engineForStress
        val cube = engine.addShape3d(0, "Cut cube")
        assertTrue(cube >= 0)
        val normal = capture(15)
        assertTrue("3D object must render", lit(normal) > 300)
        val camera = engine.addCamera()
        assertTrue(camera >= 0)
        assertTrue(engine.layoutTransform(camera, 0, 210f))
        assertTrue(engine.editClipTime(camera, 1, 10))
        assertTrue("Active camera must change the view", difference(normal, capture(9)) > 30)
        repeat(3) {
            for (frame in listOf(10, 15, 9, 10)) {
                val image = capture(frame)
                if (frame >= 10) assertTrue("Camera leaked past its cut at $frame", difference(normal, image) <= 3)
            }
        }
    }

    @Test fun panoramaHasIndependentStartEndAndSurvivesSaving() {
        project("Panorama cut device")
        val engine = store.engineForStress
        assertTrue(engine.setSceneSetting(0, 3f))
        assertTrue(engine.setEnvironmentBackground(true))
        assertTrue(engine.setEnvironmentBackgroundRange(5, 10))
        assertFalse(engine.setEnvironmentBackgroundRange(10, 5))
        assertTrue("Panorama appeared before its start", lit(capture(4)) == 0)
        assertTrue("Panorama missing inside its interval", lit(capture(5)) > 20000)
        assertTrue("Panorama leaked past its cut", lit(capture(10)) == 0)
        assertTrue("Reverse seek must restore the panorama", lit(capture(9)) > 20000)
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val saved = File(context.filesDir, "scene-cut-regression.aurea")
        assertEquals(0, engine.saveProject(saved.absolutePath))
        assertEquals(0, engine.loadProject(saved.absolutePath))
        val environment = FloatArray(6)
        assertTrue(engine.queryEnvironment(environment))
        assertEquals(5f, environment[4], 0f)
        assertEquals(10f, environment[5], 0f)
        assertTrue("Reloaded panorama leaked past its cut", lit(capture(10)) == 0)
    }

    @Test fun fullMaskPreservesTheEdgesOfAScaledAndProjectedPrecomp() {
        project("Precomp mask edges device")
        val engine = store.engineForStress
        val shape = engine.addShape(10)
        assertTrue(shape >= 0)
        assertTrue(engine.layoutTransform(shape, 8, 17f))
        val group = engine.precompose(longArrayOf(shape))
        assertTrue(group >= 0)
        assertTrue(engine.layoutTransform(group, 3, .37f))
        assertTrue(engine.layoutTransform(group, 4, .37f))
        assertTrue(engine.layoutTransform(group, 8, 11f))
        fun point(x: Float, y: Float) = floatArrayOf(x, y, 0f, 0f, 0f, 0f)
        val entireLayer = point(-100f, -100f) + point(420f, -100f) + point(420f, 280f) + point(-100f, 280f)
        for (projected in listOf(false, true)) {
            assertTrue(engine.layoutTransform(group, 6, if (projected) 24f else 0f))
            assertTrue(engine.layoutTransform(group, 7, if (projected) 47f else 0f))
            val before = capture(0)
            assertTrue("Projected shape must be visible", lit(before) > 40)
            val mask = engine.addMask(group, entireLayer, 4, true)
            assertTrue(mask >= 0)
            val after = capture(0)
            val maximum = difference(before, after)
            Log.i("AureaDeviceTest", "Precomp neutral mask projected=$projected maxDiff=$maximum")
            saveCapture("precomp-mask-${if (projected) "3d" else "2d"}-before.png", before)
            saveCapture("precomp-mask-${if (projected) "3d" else "2d"}-after.png", after)
            assertTrue("A full mask changed shape edges (projected=$projected, maxDiff=$maximum)", maximum <= 1)
            assertTrue(engine.removeMask(group, mask))
        }
    }

    @Test fun groupedLightsKeepTheSamePixelsAcrossRepeatedFrames() {
        project("Precomp light device")
        val engine = store.engineForStress
        assertTrue(engine.setSceneSetting(0, 1f))
        val cube = engine.addShape3d(0, "Lit cube")
        val light = engine.addLight(1)
        assertTrue(cube >= 0 && light >= 0)
        val before = capture(0)
        assertTrue(lit(before) > 300)
        val group = engine.precompose(longArrayOf(cube, light))
        assertTrue(group >= 0)
        val grouped = capture(0)
        saveCapture("precomp-before.png", before)
        saveCapture("precomp-after.png", grouped)
        val groupedDifference = difference(before, grouped)
        Log.i("AureaDeviceTest", "Precomp before lit=${lit(before)} after=${lit(grouped)} maxDiff=$groupedDifference")
        assertTrue("Grouping changed the scene illumination (maxDiff=$groupedDifference)", groupedDifference <= 3)
        // A different parent panorama exercises two HDRI sets in the same frame.
        assertTrue(engine.setSceneSetting(0, 3f))
        assertTrue(engine.setEnvironmentBackground(true))
        val reference = capture(0)
        for (frame in listOf(1, 5, 10, 3, 0)) {
            assertTrue("Precomp lighting changed at frame $frame", difference(reference, capture(frame)) <= 3)
        }
        // Exercise the actual preview/render loop as well. captureFrame uses
        // final quality and alone cannot prove asynchronous IBL stays stable.
        compose.runOnIdle { store.seek(0); store.play() }
        compose.waitUntil(15000) { store.playing && !store.preview.buffering }
        Thread.sleep(500)
        val bounds = compose.onNodeWithTag("editor.stage").fetchSemanticsNode().boundsInRoot
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val luminance = mutableListOf<Double>()
        try {
            repeat(10) {
                val screenshot = checkNotNull(instrumentation.uiAutomation.takeScreenshot())
                try {
                    val left = (bounds.left + bounds.width * .3f).toInt().coerceIn(0, screenshot.width - 1)
                    val right = (bounds.right - bounds.width * .3f).toInt().coerceIn(left + 1, screenshot.width)
                    val top = (bounds.top + bounds.height * .3f).toInt().coerceIn(0, screenshot.height - 1)
                    val bottom = (bounds.bottom - bounds.height * .3f).toInt().coerceIn(top + 1, screenshot.height)
                    var sum = 0.0
                    var count = 0
                    for (y in top until bottom step 2) for (x in left until right step 2) {
                        val pixel = screenshot.getPixel(x, y)
                        sum += .2126 * Color.red(pixel) + .7152 * Color.green(pixel) + .0722 * Color.blue(pixel)
                        ++count
                    }
                    luminance += sum / count
                } finally { screenshot.recycle() }
                Thread.sleep(100)
            }
            stabilityScreenshot("precomp-light-preview.png")
            val low = luminance.minOrNull()!!
            val high = luminance.maxOrNull()!!
            Log.i("AureaDeviceTest", "Precomp preview luminance=$luminance")
            assertTrue("Preview must contain the lit scene", high > 5)
            assertTrue("Grouped lights flickered during live playback: $luminance", high - low <= maxOf(3.0, high * .08))
        } finally { compose.runOnIdle { store.pause() } }
    }
}
