package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Runs the shared neural models and real Android renderer; no model download. */
class LocalAiDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun depthAndRotobrushProduceRealPixelsWithoutDownloadedWeights() {
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
        val engine = store.engineForStress
        // Preserve any weights left by older tests, while proving they are not needed.
        val folder = File(engine.foregroundModelDirectory())
        val backups = listOf("u2netp.param", "u2netp.bin").mapNotNull { name ->
            val source = File(folder, name)
            if (!source.exists()) null else {
                val backup = File(folder, "$name.offline-test-${System.nanoTime()}")
                check(source.renameTo(backup))
                source to backup
            }
        }
        fun capture(): ByteArray {
            val output = ByteBuffer.allocateDirect(256 * 256 * 4)
            val dimensions = IntArray(2)
            // Capture waits on native rendering; never block the Android UI thread.
            val count = engine.captureFrame(256, output, dimensions)
            assertEquals(256 * 256 * 4, count)
            return ByteArray(count).also { output.rewind(); output.get(it) }
        }
        fun channel(image: ByteArray, x: Int, y: Int, c: Int) = image[(y * 256 + x) * 4 + c].toInt() and 255
        fun mean(image: ByteArray, x0: Int, y0: Int, x1: Int, y1: Int): Double {
            var total = 0L
            for (y in y0 until y1) for (x in x0 until x1) total += channel(image, x, y, 0)
            return total.toDouble() / ((x1 - x0) * (y1 - y0))
        }
        fun importImage(bitmap: Bitmap, title: String): Long {
            compose.runOnIdle { store.newProject(256, 256, 30f, title) }
            compose.waitUntil(15000) { store.project.title == title }
            val pixels = ByteBuffer.allocateDirect(256 * 256 * 4)
            bitmap.copyPixelsToBuffer(pixels); pixels.rewind()
            val source = File(context.filesDir, "$title.png")
            source.outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
            bitmap.recycle()
            var layer = 0L
            compose.runOnIdle {
                layer = engine.importImage(pixels, 256, 256, title, source.absolutePath)
                assertTrue(layer > 0)
                store.select(layer)
            }
            compose.waitUntil(10000) { store.layers.any { it.id == layer } }
            return layer
        }
        fun apply(layer: Long, key: String) {
            compose.runOnIdle { store.addEffect(effectTypeId(key), listOf(layer)) }
            compose.waitUntil(10000) { store.effects.any { it.typeId == effectTypeId(key) } }
            // Observe the actually presented preview before calling captureFrame:
            // export capture is synchronous and could otherwise conceal a broken
            // async-ready redraw. Preview cache may legitimately be disabled by
            // thermal/memory policy and is never a readiness condition here.
            val stage = compose.onNodeWithTag("editor.stage").fetchSemanticsNode().boundsInRoot
            val started = android.os.SystemClock.uptimeMillis()
            var lastDiagnostic = ""
            var lastLog = 0L
            val diagnostics = File(context.getExternalFilesDir(null), "stability-screenshots").apply { mkdirs() }
            val status = ByteBuffer.allocateDirect(256).order(ByteOrder.nativeOrder())
            val bit = if (key == "aurea.ai.depth_map") 1 else 2
            var shown = false
            while (!shown && android.os.SystemClock.uptimeMillis() - started < 30000) {
                val activity = engine.localAiStatus()
                val bitmap = checkNotNull(InstrumentationRegistry.getInstrumentation().uiAutomation.takeScreenshot())
                try {
                    val side = minOf(stage.width, stage.height)
                    val left = stage.center.x - side / 2
                    val top = stage.center.y - side / 2
                    fun pixel(x: Float, y: Float): Int = bitmap.getPixel(
                        (left + side * x).toInt().coerceIn(0, bitmap.width - 1),
                        (top + side * y).toInt().coerceIn(0, bitmap.height - 1))
                    var colored = 0; var samples = 0; var bright = 0; var dark = 0
                    for (y in 15..85 step 2) for (x in 15..85 step 2) {
                        val color = pixel(x / 100f, y / 100f)
                        val r = android.graphics.Color.red(color); val g = android.graphics.Color.green(color); val b = android.graphics.Color.blue(color)
                        if (maxOf(r, g, b) - minOf(r, g, b) > 18) ++colored
                        if (r > 130) ++bright
                        if (r < 60) ++dark
                        ++samples
                    }
                    val inside = pixel(.42f, .42f)
                    val corner = pixel(.2f, .2f)
                    shown = if (bit == 1) colored < samples / 40 && bright > samples / 12 && dark > samples / 24
                        else android.graphics.Color.red(inside) > 150 && android.graphics.Color.green(corner) < 20
                    if (engine.readStatus(status)) {
                        lastDiagnostic = "effect=$key elapsed=${android.os.SystemClock.uptimeMillis() - started}ms activity=$activity uiActivity=${store.localAiActivity} shown=$shown colored=$colored/$samples bright=$bright dark=$dark " +
                            "frame=${status.getLong(PodLayout.ST_OFF_PLAYHEAD)} error=${status.getInt(PodLayout.ST_OFF_LAST_ERROR)} " +
                            "pressure=${status.getFloat(PodLayout.ST_OFF_MEMORY_PRESSURE)} frameMs=${status.getFloat(PodLayout.ST_OFF_AVERAGE_FRAME_MS)} " +
                            "buffer=${status.getInt(PodLayout.ST_OFF_PREVIEW_BUFFER)} ranges=${store.previewBufferRanges}"
                    }
                    val now = android.os.SystemClock.uptimeMillis()
                    if (shown || now - lastLog >= 2000) {
                        android.util.Log.i("AureaLocalAI", lastDiagnostic)
                        File(diagnostics, "local-ai-diagnostic.txt").appendText(lastDiagnostic + "\n")
                        lastLog = now
                    }
                    if (shown || now - started >= 29000) {
                        File(diagnostics, if (shown) "local-ai-preview-$bit.png" else "local-ai-pending-$bit.png").outputStream().use {
                            bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)
                        }
                    }
                } finally { bitmap.recycle() }
                if (!shown) Thread.sleep(250)
            }
            assertTrue("Async local AI preview did not update: $lastDiagnostic", shown)
        }
        try {
            val original = context.assets.open("previa_efeitos.jpg").use { BitmapFactory.decodeStream(it) }!!
            val photo = Bitmap.createScaledBitmap(original, 256, 256, true)
            if (photo !== original) original.recycle()
            val image = importImage(photo, "Depth offline regression")
            apply(image, "aurea.ai.depth_map")
            val depth = capture()
            assertEquals("Depth worker must finish without an error", 0, engine.localAiStatus() and 5)
            var colored = 0
            for (i in depth.indices step 4) {
                val r = depth[i].toInt() and 255; val g = depth[i + 1].toInt() and 255; val b = depth[i + 2].toInt() and 255
                if (maxOf(r, g, b) - minOf(r, g, b) > 4) ++colored
            }
            assertTrue("Depth map must replace the colored photo with grayscale", colored < 100)
            val person = mean(depth, 96, 96, 160, 192)
            val background = (mean(depth, 0, 0, 32, 32) + mean(depth, 224, 0, 256, 32)) / 2
            assertTrue("Near person must be brighter than distant background: $person / $background", person > background + 45)
            stabilityScreenshot("depth-offline.png")

            val circle = Bitmap.createBitmap(256, 256, Bitmap.Config.ARGB_8888)
            for (y in 0 until 256) for (x in 0 until 256) {
                val inside = (x - 128) * (x - 128) + (y - 128) * (y - 128) < 64 * 64
                circle.setPixel(x, y, if (inside) android.graphics.Color.rgb(220, 40, 30) else android.graphics.Color.rgb(30, 120, 70))
            }
            val foreground = importImage(circle, "Rotobrush offline regression")
            compose.runOnIdle { store.setCompositionBackground(0f, 0f, 0f, 1f) }
            apply(foreground, "aurea.key.rotobrush")
            val cutout = capture()
            assertEquals("Rotobrush worker must finish without an error", 0, engine.localAiStatus() and 10)
            assertTrue("Foreground remains visible", channel(cutout, 128, 128, 0) > 180)
            assertTrue("Green background must be cut out", channel(cutout, 0, 0, 1) < 15)
            assertFalse(File(folder, "u2netp.bin").exists())
            assertFalse(File(folder, "u2netp.param").exists())
            stabilityScreenshot("rotobrush-offline.png")
            val project = File(context.filesDir, "local-ai-offline.aurea")
            assertEquals(0, engine.saveProject(project.absolutePath))
            assertEquals(0, engine.loadProject(project.absolutePath))
            val reopened = capture()
            val reopenedCenter = (0..3).joinToString { channel(reopened, 128, 128, it).toString() }
            val reopenedCorner = (0..3).joinToString { channel(reopened, 0, 0, it).toString() }
            val reopenDiagnostic = "Rotobrush after reopen: center=[$reopenedCenter], corner=[$reopenedCorner], activity=${engine.localAiStatus()}"
            android.util.Log.i("AureaLocalAI", reopenDiagnostic)
            assertTrue(reopenDiagnostic, channel(reopened, 128, 128, 0) > 180)
            assertTrue(reopenDiagnostic, channel(reopened, 0, 0, 1) < 15)
        } finally {
            backups.forEach { (source, backup) -> check(backup.renameTo(source)) }
        }
    }
}
