package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Color
import android.net.Uri
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer

class PreviewStabilityRegressionTest {
    @get:Rule val compose = createComposeRule()

    private fun editor(): EditorStore {
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
        return store
    }

    @Test fun thickTextOutlinePreservesTheFilledLetters() {
        val store = editor()
        compose.runOnIdle { store.newProject(640, 360, 30f, "Outline regression") }
        compose.waitUntil(10000) { store.project.title == "Outline regression" }
        compose.runOnIdle { store.addText(); store.dismissTextContentEditor() }
        compose.waitUntil(5000) { store.textDetail != null }
        compose.runOnIdle {
            store.setTextContent("AVAVA"); store.setTextSize(100f)
            store.setTextColor(1f, 1f, 1f, 1f)
            store.setTextStrokeColor(1f, 0f, 0f, 1f)
        }
        compose.waitUntil(5000) { store.textDetail?.content == "AVAVA" && store.textDetail?.size == 100f }
        fun pixels(): ByteArray {
            val buffer = ByteBuffer.allocateDirect(640 * 360 * 4)
            val dimensions = IntArray(2)
            val count = store.engineForStress.captureFrame(640, buffer, dimensions)
            assertEquals(640 * 360 * 4, count)
            return ByteArray(count).also { buffer.rewind(); buffer.get(it) }
        }
        fun white(image: ByteArray) = (image.indices step 4).count {
            (image[it].toInt() and 255) > 240 && (image[it + 1].toInt() and 255) > 240 && (image[it + 2].toInt() and 255) > 240
        }
        val before = white(pixels())
        compose.runOnIdle { store.setTextStrokeWidth(18f) }
        compose.waitUntil(5000) { store.textDetail?.strokeWidth == 18f }
        val outlined = pixels()
        val after = white(outlined)
        val red = (outlined.indices step 4).count { (outlined[it].toInt() and 255) > 200 && (outlined[it + 1].toInt() and 255) < 80 }
        assertTrue("white text must be visible", before > 1000)
        assertTrue("outline covered the letters: $before -> $after", after >= before * .98)
        assertTrue("outline must be visible", red > 1000)
    }

    @Test fun playingAndSeekingVideoKeepsVisiblePreviewPixels() {
        val store = editor()
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        compose.runOnIdle { store.newProject(320, 180, 30f, "Preview regression") }
        compose.waitUntil(10000) { store.project.title == "Preview regression" }
        val video = File(context.filesDir, "preview-stability.mp4")
        instrumentation.context.assets.open("motion-fixture.mp4").use { input -> video.outputStream().use { input.copyTo(it) } }
        compose.runOnIdle { store.importVideo(Uri.fromFile(video)) }
        compose.waitUntil(30000) { store.layers.any { it.kind == 1 } }
        val videoLayer = store.layers.single { it.kind == 1 }
        val lastVisibleFrame = videoLayer.endFrame - 1
        assertTrue("fixture must span the seek samples", lastVisibleFrame >= 45)
        compose.runOnIdle { store.seek(0) }
        compose.waitForIdle()
        Thread.sleep(800)
        val bounds = compose.onNodeWithTag("editor.stage").fetchSemanticsNode().boundsInRoot
        fun coloredPixels(sample: String, minimum: Int): Int {
            val image = checkNotNull(instrumentation.uiAutomation.takeScreenshot())
            var colored = 0
            var cyan = 0
            var magenta = 0
            try {
                for (y in bounds.top.toInt().coerceAtLeast(0) until bounds.bottom.toInt().coerceAtMost(image.height) step 3)
                    for (x in bounds.left.toInt().coerceAtLeast(0) until bounds.right.toInt().coerceAtMost(image.width) step 3) {
                        val pixel = image.getPixel(x, y)
                        val hi = maxOf(Color.red(pixel), Color.green(pixel), Color.blue(pixel))
                        val lo = minOf(Color.red(pixel), Color.green(pixel), Color.blue(pixel))
                        if (hi - lo > 40 && hi > 100) ++colored
                        val r = Color.red(pixel); val g = Color.green(pixel); val b = Color.blue(pixel)
                        if (g > 170 && b > 170 && r < 90) ++cyan
                        if (r > 170 && b > 170 && g < 90) ++magenta
                    }
                if (sample == "baseline" || colored <= minimum || cyan <= 1 || magenta <= 1) {
                    val captures = File(context.getExternalFilesDir(null), "stability-screenshots").apply { mkdirs() }
                    File(captures, "preview-$sample.png").outputStream().use {
                        image.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it)
                    }
                }
                assertTrue("preview lost the fixture's cyan/magenta colors at $sample (cyan=$cyan magenta=$magenta); a flat green frame cannot pass",
                    cyan > 1 && magenta > 1)
            } finally { image.recycle() }
            return colored
        }
        val baseline = coloredPixels("baseline", 150)
        assertTrue("fixture must appear in the live preview", baseline > 150)
        compose.runOnIdle { store.play() }
        try {
            repeat(8) {
                Thread.sleep(80)
                val visible = coloredPixels("playback-$it", baseline / 5)
                assertTrue("preview became blank during playback at sample $it ($visible of $baseline)", visible > baseline / 5)
            }
        } finally { compose.runOnIdle { store.pause() } }
        // endFrame is exclusive: seeking to the 2-second fixture's frame 60
        // correctly displays the empty composition after the clip.
        for (frame in listOf(45, 5, lastVisibleFrame, 15)) {
            compose.runOnIdle { store.seek(frame) }
            Thread.sleep(80)
            val visible = coloredPixels("seek-$frame", baseline / 5)
            assertTrue("preview became blank after seek to $frame ($visible of $baseline)", visible > baseline / 5)
        }
    }
}
