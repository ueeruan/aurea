package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import android.net.Uri
import android.util.Log
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
import java.nio.ByteBuffer

class PrecompInstanceDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun duplicatedPrecompKeepsIndependentVideoFramesAfterSeekAndReload() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        check(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(320, 180, 30f, "Independent precomp video") }
        compose.waitUntil(10000) { store.project.title == "Independent precomp video" }
        val video = File(context.filesDir, "precomp-instance.mp4")
        instrumentation.context.assets.open("motion-fixture.mp4").use { input -> video.outputStream().use { input.copyTo(it) } }
        compose.runOnIdle { store.importVideo(Uri.fromFile(video)) }
        compose.waitUntil(30000) { store.layers.any { it.kind == 1 } }
        val engine = store.engineForStress
        val group = engine.precompose(longArrayOf(store.layers.single { it.kind == 1 }.id))
        assertTrue(group >= 0)
        assertTrue(engine.layoutTransform(group, 3, .5f))
        assertTrue(engine.layoutTransform(group, 4, .5f))
        assertTrue(engine.layoutTransform(group, 0, 80f))
        assertTrue(engine.layoutTransform(group, 1, 90f))
        fun capture(frame: Int): ByteArray {
            compose.runOnIdle { store.seek(frame) }
            compose.waitUntil(10000) { store.playhead == frame }
            val buffer = ByteBuffer.allocateDirect(320 * 180 * 4)
            val dimensions = IntArray(2)
            val count = engine.captureFrame(320, buffer, dimensions)
            assertEquals("Exact capture at frame $frame", 320 * 180 * 4, count)
            return ByteArray(count).also { buffer.rewind(); buffer.get(it) }
        }
        val sampleFrames = listOf(0, 7, 14)
        val early = sampleFrames.associateWith { capture(it) }
        val late = sampleFrames.associateWith { capture(it + 30) }
        fun meanDifference(a: ByteArray, b: ByteArray, targetX: Int = 0): Double {
            var sum = 0L
            var count = 0
            for (y in 48 until 132) for (x in 4 until 156) for (channel in 0..2) {
                val source = (y * 320 + x) * 4 + channel
                val target = (y * 320 + x + targetX) * 4 + channel
                sum += kotlin.math.abs((a[source].toInt() and 255) - (b[target].toInt() and 255))
                ++count
            }
            return sum.toDouble() / count
        }
        assertTrue("Fixture must distinguish the two source times", sampleFrames.maxOf {
            meanDifference(early.getValue(it), late.getValue(it))
        } > 2.0)
        assertTrue(engine.editClipTime(group, 1, 30))
        compose.runOnIdle { store.seek(0) }
        compose.waitUntil(10000) { store.layers.size == 1 && store.layers[0].id == group }
        compose.runOnIdle { store.duplicateLayers(listOf(group)) }
        compose.waitUntil(10000) { store.layers.size == 2 }
        val copy = store.layers.single { it.id != group }.id
        assertTrue(engine.layoutTransform(copy, 0, 240f))
        assertTrue(engine.editClipTime(copy, 2, 30))
        val directory = File(context.getExternalFilesDir(null), "stability-screenshots").apply { mkdirs() }
        fun verify(frame: Int, name: String) {
            val combined = capture(frame)
            val left = meanDifference(early.getValue(frame), combined)
            val right = meanDifference(late.getValue(frame), combined, 160)
            Log.i("AureaDeviceTest", "Precomp independent $name frame=$frame leftMean=$left rightMean=$right")
            assertTrue("First instance changed to the second video's time ($left)", left < 1.0)
            assertTrue("Second instance did not retain its offset ($right)", right < 1.0)
            val bitmap = Bitmap.createBitmap(320, 180, Bitmap.Config.ARGB_8888)
            try {
                val colors = IntArray(320 * 180) { index ->
                    val p = index * 4
                    Color.argb(combined[p + 3].toInt() and 255, combined[p].toInt() and 255,
                        combined[p + 1].toInt() and 255, combined[p + 2].toInt() and 255)
                }
                bitmap.setPixels(colors, 0, 320, 0, 0, 320, 180)
                File(directory, "precomp-independent-$name-$frame.png").outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
            } finally { bitmap.recycle() }
        }
        for (frame in listOf(14, 0, 7)) verify(frame, "seek")
        val project = File(context.filesDir, "independent-precomp.aurea")
        assertEquals(0, engine.saveProject(project.absolutePath))
        assertEquals(0, engine.loadProject(project.absolutePath))
        verify(0, "reload")
    }
}
