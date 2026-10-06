package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.media.MediaMetadataRetriever
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.EngineStatus
import com.aurea.aurea.engine.PerfStats
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.abs

/** Diagnostic only. Four visible layer instances keep four decoders active.
 * Preserve every mismatch; do not relax the acceptance soak's assertions. */
class DiagnosticMultiDecoderColorDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun compareFourDecoderPathsAcrossTwelveReopensAndPlanar() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        check(context.packageName == "com.aurea.aurea.uitest")
        org.junit.Assume.assumeTrue(InstrumentationRegistry.getArguments().getString("aureaStress") == "true")
        val forcedSoftwareFallback = ParcelFileDescriptor.AutoCloseInputStream(
            instrumentation.uiAutomation.executeShellCommand("getprop debug.aurea.force_sw_fallback")
        ).bufferedReader().use { it.readText().trim() } == "1"
        val source = context.filesDir.listFiles().orEmpty().filter { it.name.startsWith("stress-sustained-") }
            .sortedByDescending { it.name }.map { File(it, "fixture-1080p.mp4") }.firstOrNull { it.length() > 1000 }
        assertNotNull("Generate the Full HD soak fixture before this diagnostic", source)
        val fixture = checkNotNull(source)
        val folder = File(context.filesDir, "video-multi-color-${System.currentTimeMillis()}").apply { mkdirs() }
        val report = File(folder, "progress.txt")
        report.writeText("Four decoder paths; forcedSoftwareFallback=$forcedSoftwareFallback; fixture=${fixture.absolutePath}\n")
        val media = MediaMetadataRetriever()
        try {
            media.setDataSource(fixture.absolutePath)
            for ((name, key) in listOf("width" to MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH,
                "height" to MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT,
                "standard" to MediaMetadataRetriever.METADATA_KEY_COLOR_STANDARD,
                "range" to MediaMetadataRetriever.METADATA_KEY_COLOR_RANGE,
                "transfer" to MediaMetadataRetriever.METADATA_KEY_COLOR_TRANSFER)) {
                report.appendText("metadata $name=${media.extractMetadata(key)}\n")
            }
        } finally { media.release() }
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(1920, 1080, 30f, "Four decoder color diagnostic") }
        compose.waitUntil(30000) { store.project.title == "Four decoder color diagnostic" && !store.projectOperationBusy }
        val engine = store.engineForStress
        val layers = LongArray(4) { index -> engine.importVideo(fixture.absolutePath, "Independent decoder $index") }
        assertTrue(layers.all { it > 0 })
        assertEquals(4, layers.toSet().size)
        compose.runOnIdle {
            store.setCompositionDuration(90)
            engine.beginCommandBatch()
            val cmd = CommandBatch(engine)
            layers.forEachIndexed { index, id ->
                cmd.setLayerTimeRange(id, 0, 90)
                cmd.setPosition(id, 480f + (index % 2) * 960f, 270f + (index / 2) * 540f, 0f)
                cmd.setScale(id, .5f, .5f, .5f)
            }
            assertEquals(12, engine.submitCommands())
        }
        val statusBuffer = ByteBuffer.allocateDirect(PodLayout.STATUS_BYTES).order(ByteOrder.nativeOrder())
        fun seek() {
            compose.runOnIdle { store.pause(); store.seek(45) }
            val began = SystemClock.elapsedRealtime()
            while (true) {
                assertTrue(engine.readStatus(statusBuffer))
                val status = EngineStatus().also { it.readFrom(statusBuffer) }
                if (!status.playing && status.playhead == 45L) break
                assertTrue("Native seek did not finish", SystemClock.elapsedRealtime() - began < 30000)
                Thread.sleep(50)
            }
        }
        val width = 640
        val height = 360
        fun capture(label: String): ByteArray {
            val began = SystemClock.elapsedRealtime()
            val buffer = ByteBuffer.allocateDirect(width * height * 4)
            val dimensions = IntArray(2)
            val count = engine.captureFrame(width, buffer, dimensions)
            report.appendText("capture $label ms=${SystemClock.elapsedRealtime() - began} bytes=$count size=${dimensions.contentToString()}\n")
            assertEquals(width * height * 4, count)
            assertArrayEquals(intArrayOf(width, height), dimensions)
            val rgba = ByteArray(count).also { buffer.rewind(); buffer.get(it) }
            File(folder, "$label.rgba").writeBytes(rgba)
            val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
            try {
                bitmap.copyPixelsFromBuffer(ByteBuffer.wrap(rgba))
                File(folder, "$label.png").outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
            } finally { bitmap.recycle() }
            for (quadrant in 0..3) {
                var cyan = 0; var magenta = 0
                val left = quadrant % 2 * width / 2
                val top = quadrant / 2 * height / 2
                for (y in top until top + height / 2) for (x in left until left + width / 2) {
                    val offset = (y * width + x) * 4
                    val r = rgba[offset].toInt() and 255
                    val g = rgba[offset + 1].toInt() and 255
                    val b = rgba[offset + 2].toInt() and 255
                    if (g > 170 && b > 170 && r < 90) cyan++
                    if (r > 170 && b > 170 && g < 90) magenta++
                }
                report.appendText("visible $label quadrant=$quadrant cyan=$cyan magenta=$magenta\n")
                val probePositions = listOf(.92f to .25f, .42f to .55f, .60f to .55f, .25f to .55f)
                val samples = probePositions.joinToString("|") { (u, v) ->
                    val x = left + (u * (width / 2)).toInt()
                    val y = top + (v * (height / 2)).toInt()
                    val offset = (y * width + x) * 4
                    (0..3).joinToString(",") { channel -> (rgba[offset + channel].toInt() and 255).toString() }
                }
                report.appendText("pixels $label quadrant=$quadrant positions=FBO-probe samples=$samples\n")
                assertTrue("Every decoder must produce visible video: $label quadrant=$quadrant", cyan > 5 && magenta > 5)
            }
            val perfBuffer = ByteBuffer.allocateDirect(PerfStats.BYTES).order(ByteOrder.nativeOrder())
            assertTrue(engine.readPerf(perfBuffer))
            val perf = PerfStats.read(perfBuffer)
            // Perf is an aggregate. Per-decoder actual path is logged natively;
            // hardware=true here proves at least one hardware source, not all four.
            report.appendText("perf $label anyHardware=${perf.hardwareDecoder} firstDecoder=${perf.decoder} stale=${perf.staleFrames} decodedBytes=${perf.decodedCacheBytes}\n")
            if (forcedSoftwareFallback || label.startsWith("software-planar")) {
                assertFalse("Every decoder must use the requested software fallback: $label", perf.hardwareDecoder)
            }
            return rgba
        }
        val colorFailures = mutableListOf<String>()
        fun compare(label: String, a: ByteArray, b: ByteArray) {
            assertEquals(a.size, b.size)
            for (quadrant in 0..3) {
                var maximum = 0; var changed = 0; var sum = 0L
                val left = quadrant % 2 * width / 2
                val top = quadrant / 2 * height / 2
                for (y in top until top + height / 2) for (x in left until left + width / 2) for (channel in 0..3) {
                    val offset = (y * width + x) * 4 + channel
                    val delta = abs((a[offset].toInt() and 255) - (b[offset].toInt() and 255))
                    maximum = maxOf(maximum, delta); sum += delta
                    if (delta > 4) changed++
                }
                report.appendText("compare $label quadrant=$quadrant max=$maximum above4=$changed mean=${sum.toDouble() / (width * height)}\n")
                if (maximum > 4) colorFailures.add("$label quadrant=$quadrant max=$maximum")
            }
        }
        seek()
        val first = capture("initial-00")
        compare("initial-repeat", first, capture("initial-00-repeat"))
        val project = File(folder, "four-decoders.aurea")
        assertEquals(0, engine.saveProject(project.absolutePath))
        val originals = mutableListOf(first)
        for (round in 1..12) {
            val began = SystemClock.elapsedRealtime()
            assertEquals(0, engine.loadProject(project.absolutePath))
            report.appendText("reload round=$round ms=${SystemClock.elapsedRealtime() - began}\n")
            seek()
            val name = "initial-${round.toString().padStart(2, '0')}"
            val actual = capture(name)
            compare("$name-repeat", actual, capture("$name-repeat"))
            compare("$name-versus-initial", first, actual)
            compare("$name-versus-prior", originals.last(), actual)
            originals.add(actual)
        }
        engine.useReadableVideoPlanes()
        assertEquals(0, engine.loadProject(project.absolutePath))
        seek()
        val planar = capture("software-planar")
        compare("planar-repeat", planar, capture("software-planar-repeat"))
        originals.forEachIndexed { index, rgba -> compare("initial-$index-versus-planar", rgba, planar) }
        // Finish collecting every pair before failing, so changing decoder
        // assignment across reopens remains visible in the saved evidence.
        report.appendText("COMPLETE; all paths enforce <=4 per channel; failures=${colorFailures.size}\n")
        assertTrue("Every decoder must preserve color across repeats, twelve reopens and the planar reference: " +
            colorFailures.take(8).joinToString("; "), colorFailures.isEmpty())
    }
}
