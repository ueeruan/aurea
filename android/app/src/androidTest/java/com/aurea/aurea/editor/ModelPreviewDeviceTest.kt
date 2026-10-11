package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import android.net.Uri
import android.os.Build
import android.os.SystemClock
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.EngineStatus
import com.aurea.aurea.engine.MODEL_QUALITY_ORIGINAL
import com.aurea.aurea.engine.PerfStats
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import kotlin.math.abs
import kotlin.math.roundToInt

/** Opt-in, licensed real models through the actual editor. Pixel samples come
 * from SurfaceFlinger, never a synchronous engine offscreen render. Timings are
 * measurements, not a minimum-FPS guarantee or a synthetic benchmark result. */
class ModelPreviewDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun originalModelsRemainVisibleThroughPlaybackSeekReopenAndTrim() {
        assumeTrue("Enable with -e aureaModelPreview true",
            InstrumentationRegistry.getArguments().getString("aureaModelPreview") == "true")
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        check(context.packageName == "com.aurea.aurea.uitest")
        val info = context.packageManager.getPackageInfo(context.packageName, 0)
        @Suppress("DEPRECATION")
        val build = if (Build.VERSION.SDK_INT >= 28) info.longVersionCode else info.versionCode.toLong()
        val folder = File(context.filesDir, "models-preview-owned").apply { mkdirs() }
        val report = File(folder, "model-preview-report.jsonl")
        report.writeText(JSONObject().put("kind", "actual-editor-model-preview").put("targetBuild", build)
            .put("device", Build.MODEL).put("sdk", Build.VERSION.SDK_INT).put("quality", "ORIGINAL")
            .put("width", 960).put("height", 540).put("measurement", "SurfaceFlinger pixels and native preview statistics")
            .toString() + "\n")
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(60000) { ready && store.engineReady }
        val engine = store.engineForStress
        val statusBuffer = ByteBuffer.allocateDirect(PodLayout.STATUS_BYTES).order(ByteOrder.nativeOrder())
        val perfBuffer = ByteBuffer.allocateDirect(PerfStats.BYTES).order(ByteOrder.nativeOrder())
        fun status(): EngineStatus {
            assertTrue(engine.readStatus(statusBuffer))
            return EngineStatus().also { it.readFrom(statusBuffer) }
        }
        fun record(model: String, label: String, elapsed: Long = 0L, extra: JSONObject = JSONObject()) {
            assertTrue(engine.readPerf(perfBuffer))
            val perf = PerfStats.read(perfBuffer)
            val state = status()
            fun finite(value: Float) = if (value.isFinite()) value.toDouble() else 0.0
            extra.put("model", model).put("sample", label).put("elapsedMs", elapsed).put("targetBuild", build)
                .put("playhead", state.playhead).put("playing", state.playing).put("lastError", state.lastError)
                .put("previewFps", finite(perf.previewFps)).put("cpuMs", finite(perf.cpuFrameMs))
                .put("gpuMs", finite(perf.gpuFrameMs)).put("presentMs", finite(perf.presentMs))
                .put("acquireMs", finite(perf.acquireMs)).put("cpuPrepareMs", finite(perf.cpuPrepareMs))
                .put("cpuRecordMs", finite(perf.cpuRecordMs)).put("pacingP50Ms", finite(perf.pacingP50Ms))
                .put("pacingP95Ms", finite(perf.pacingP95Ms)).put("pacingP99Ms", finite(perf.pacingP99Ms))
                .put("droppedFrames", perf.droppedFrames).put("staleFrames", perf.staleFrames)
                .put("draws3D", perf.draws3D).put("triangles3D", perf.triangles3D).put("culled3D", perf.culled3D)
                .put("ramBytes", perf.ramBytes).put("gpuBytes", perf.gpuMemoryBytes)
                .put("gpuReservedBytes", perf.gpuReservedBytes).put("scene3dBytes", perf.scene3dBytes)
                .put("gpuName", perf.gpuName).put("previewWidth", perf.previewWidth).put("previewHeight", perf.previewHeight)
            report.appendText(extra.toString() + "\n")
        }
        fun ui(action: () -> Unit) { instrumentation.runOnMainSync(action) }
        var background = 0
        fun pixels(model: String, label: String, requireModel: Boolean = true, save: Boolean = false): Boolean {
            val began = SystemClock.elapsedRealtime()
            val bounds = compose.onNodeWithTag("editor.stage").fetchSemanticsNode().boundsInWindow
            val screenshot = checkNotNull(instrumentation.uiAutomation.takeScreenshot())
            val screenshotMs = SystemClock.elapsedRealtime() - began
            try {
                // Exclude letterboxing and editor controls. Composition is 16:9.
                val width = minOf(bounds.width, bounds.height * 16f / 9f).roundToInt().coerceAtLeast(1)
                val height = (width * 9f / 16f).roundToInt().coerceAtLeast(1)
                val left = (bounds.center.x - width / 2f).roundToInt().coerceIn(0, screenshot.width - 1)
                val top = (bounds.center.y - height / 2f).roundToInt().coerceIn(0, screenshot.height - 1)
                val right = (left + width).coerceAtMost(screenshot.width)
                val bottom = (top + height).coerceAtMost(screenshot.height)
                val histogram = HashMap<Int, Int>()
                var total = 0; var foreground = 0; var green = 0; var imageHash = 1L
                for (y in top until bottom step 3) for (x in left until right step 3) {
                    val color = screenshot.getPixel(x, y)
                    val r = Color.red(color); val g = Color.green(color); val b = Color.blue(color)
                    val bin = ((r shr 3) shl 10) or ((g shr 3) shl 5) or (b shr 3)
                    histogram[bin] = (histogram[bin] ?: 0) + 1
                    if (abs(r - Color.red(background)) + abs(g - Color.green(background)) + abs(b - Color.blue(background)) > 48) ++foreground
                    if (g > 110 && g > r * 1.5f && g > b * 1.5f) ++green
                    imageHash = imageHash * 31 + color
                    ++total
                }
                val dominant = histogram.maxByOrNull { it.value }
                val dominantFraction = (dominant?.value ?: 0).toDouble() / total.coerceAtLeast(1)
                if (!requireModel && dominant != null) {
                    val bin = dominant.key
                    background = Color.rgb(((bin shr 10) and 31) * 8 + 4, ((bin shr 5) and 31) * 8 + 4, (bin and 31) * 8 + 4)
                }
                // A green cube is a legitimate model. A whole flat green
                // surface is a failure; texture hue alone is not one.
                val flatGreen = green.toDouble() / total.coerceAtLeast(1) > 0.95
                val valid = if (requireModel) foreground >= maxOf(16, total / 500) && histogram.size >= 3 && !flatGreen
                            else dominantFraction > 0.97 && !flatGreen
                record(model, label, SystemClock.elapsedRealtime() - began,
                    JSONObject().put("validPixels", valid).put("samples", total).put("foreground", foreground)
                        .put("colorBins", histogram.size).put("dominantFraction", dominantFraction)
                        .put("flatGreen", flatGreen).put("imageHash", imageHash).put("screenshotMs", screenshotMs))
                if (save || !valid) {
                    val cropped = Bitmap.createBitmap(screenshot, left, top, right - left, bottom - top)
                    try { File(folder, "$model-$label.png").outputStream().use { cropped.compress(Bitmap.CompressFormat.PNG, 100, it) } }
                    finally { if (cropped !== screenshot) cropped.recycle() }
                }
                return valid
            } finally { screenshot.recycle() }
        }
        val fixtures = linkedMapOf(
            "DamagedHelmet.glb" to "a1e3b04de97b11de564ce6e53b95f02954a297f0008183ac63a4f5974f6b32d8",
            "Fox.glb" to "d97044e701822bac5a62696459b27d7b375aada5de8574ed4362edbba94771f7",
            "AnimatedMorphCube.glb" to "214ee56160a50dbf22543a1d66dbf860986e87f0efac3d89feac1359d0e6aeab",
        )
        try {
            for ((name, checksum) in fixtures) {
                val model = name.substringBeforeLast('.')
                val source = File(folder, name)
                instrumentation.context.assets.open("models-preview-owned/$name").use { input -> source.outputStream().use { input.copyTo(it) } }
                val actualHash = MessageDigest.getInstance("SHA-256").digest(source.readBytes()).joinToString("") { "%02x".format(it) }
                assertEquals("Fixture must match the licensed benchmark input", checksum, actualHash)
                ui { store.newProject(960, 540, 30f, "Model preview $model") }
                compose.waitUntil(60000) { store.project.title == "Model preview $model" && !store.projectOperationBusy }
                ui {
                    store.setCompositionDuration(180); store.setCompositionBackground(0.012f, 0.005f, 0.025f)
                    store.setPreviewScale(false, 1, 1); store.setLoop(false); store.pause(); store.seek(0)
                }
                compose.waitUntil(30000) { status().duration == 180L && status().playhead == 0L && !status().playing }
                var emptyReady = false
                val emptyDeadline = SystemClock.elapsedRealtime() + 30000
                while (!emptyReady && SystemClock.elapsedRealtime() < emptyDeadline) {
                    emptyReady = pixels(model, "empty", requireModel = false)
                    if (!emptyReady) SystemClock.sleep(100)
                }
                assertTrue("New composition never displayed its empty background", emptyReady)
                val importAt = SystemClock.elapsedRealtime()
                ui { store.importModel(Uri.fromFile(source)) }
                compose.waitUntil(180000) {
                    store.modelOptimize?.let { request -> ui { store.modelOptimizeQuality = MODEL_QUALITY_ORIGINAL; store.confirmModelOptimize(request) } }
                    store.layers.any { it.kind == 10 } && store.busyMessage == null
                }
                val layer = store.layers.single { it.kind == 10 }
                assertTrue("Imported model lost embedded textures", engine.modelMissingTextures(layer.id).isEmpty())
                val importReport = engine.lastModelImport()
                assertEquals("Model quality must stay ORIGINAL", MODEL_QUALITY_ORIGINAL.toLong(), importReport.getOrElse(4) { -1L })
                record(model, "import", SystemClock.elapsedRealtime() - importAt,
                    JSONObject().put("sourceSha256", actualHash).put("sourceBytes", source.length()).put("importReport", JSONArray(importReport.toList())))
                ui {
                    store.clearSelection()
                    engine.beginCommandBatch()
                    CommandBatch(engine).apply {
                        setLayerTimeRange(layer.id, 0, 180)
                        insertKeyframe(layer.id, TrackProperty.ROTATION_Z, -1, 0, 0, -12f)
                        insertKeyframe(layer.id, TrackProperty.ROTATION_Z, -1, 0, 179, 12f)
                    }
                    assertEquals(3, engine.submitCommands())
                    store.seek(0)
                }
                var visible = false
                val coldDeadline = SystemClock.elapsedRealtime() + 60000
                while (!visible && SystemClock.elapsedRealtime() < coldDeadline) {
                    visible = pixels(model, "cold", save = true)
                    if (!visible) SystemClock.sleep(100)
                }
                record(model, "cold-ready", SystemClock.elapsedRealtime() - importAt)
                assertTrue("ORIGINAL model never appeared: $name; ${store.errorMessage}", visible)
                val saved = File(folder, "$model.aurea")
                assertEquals(0, engine.saveProject(saved.absolutePath))
                for (round in 0..1) {
                    ui { store.pause(); store.seek(0) }
                    compose.waitUntil(30000) { !status().playing && status().playhead == 0L }
                    assertTrue("Missing model before play", pixels(model, "warm-$round-before"))
                    val playAt = SystemClock.elapsedRealtime()
                    ui { store.play() }
                    compose.waitUntil(30000) { status().playhead > 0L }
                    record(model, "warm-$round-start", SystemClock.elapsedRealtime() - playAt)
                    var samples = 0
                    val until = SystemClock.elapsedRealtime() + 3000
                    do {
                        assertTrue("Blank/flat green playback picture: $model round=$round", pixels(model, "play-$round-$samples", save = samples == 0))
                        ++samples
                        SystemClock.sleep(100)
                    } while (SystemClock.elapsedRealtime() < until)
                    val advanced = status().playhead
                    record(model, "warm-$round-played", SystemClock.elapsedRealtime() - playAt,
                        JSONObject().put("pictureSamples", samples).put("advancedFrames", advanced))
                    assertTrue("Timeline did not advance through actual playback", advanced >= 3L)
                    ui { store.pause() }
                    for (frame in listOf(45, 5, 90, 15)) {
                        val seekAt = SystemClock.elapsedRealtime()
                        ui { store.seek(frame) }
                        compose.waitUntil(30000) { !status().playing && status().playhead == frame.toLong() }
                        assertTrue("Model disappeared on seek: $model/$frame", pixels(model, "seek-$round-$frame"))
                        record(model, "seek-$round-$frame-ready", SystemClock.elapsedRealtime() - seekAt)
                    }
                    val reopenAt = SystemClock.elapsedRealtime()
                    assertEquals(0, engine.loadProject(saved.absolutePath))
                    compose.waitUntil(60000) { status().layerCount == 1 && !status().playing }
                    ui { store.clearSelection(); store.seek(30) }
                    compose.waitUntil(30000) { status().playhead == 30L }
                    assertTrue("Model disappeared on reopen", pixels(model, "reopen-$round", save = true))
                    record(model, "reopen-$round-ready", SystemClock.elapsedRealtime() - reopenAt)
                    val trimAt = SystemClock.elapsedRealtime()
                    val reclaimed = engine.trimMemory(80)
                    ui { store.seek(60) }
                    compose.waitUntil(30000) { status().playhead == 60L && !status().playing }
                    assertTrue("Model disappeared after memory trim", pixels(model, "trim-$round", save = true))
                    record(model, "trim-$round-ready", SystemClock.elapsedRealtime() - trimAt, JSONObject().put("reclaimedBytes", reclaimed))
                }
            }
            report.appendText(JSONObject().put("result", "PASS").put("targetBuild", build).toString() + "\n")
        } catch (error: Throwable) {
            report.appendText(JSONObject().put("result", "FAIL").put("targetBuild", build).put("error", error.toString())
                .put("editorError", store.errorMessage).put("busy", store.busyMessage).toString() + "\n")
            throw error
        } finally { ui { store.pause() } }
    }
}
