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
import com.aurea.aurea.engine.ModelPlan
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
        val ownedFolder = folder.canonicalFile
        check(ownedFolder.parentFile == context.filesDir.canonicalFile)
        val runId = "$build-${System.currentTimeMillis()}"
        // This package is the isolated UITest app (checked above). Remove only
        // direct files in its named test-owned folder; never recurse or follow
        // a child out of that directory. Old PNGs must not prove a new run.
        var cleanedArtifacts = 0
        for (artifact in folder.listFiles().orEmpty()) {
            if (!artifact.isFile) continue
            check(artifact.canonicalFile.parentFile == ownedFolder)
            check(artifact.delete()) { "Could not remove stale test artifact: ${artifact.name}" }
            ++cleanedArtifacts
        }
        val report = File(folder, "model-preview-report.jsonl")
        report.writeText(JSONObject().put("kind", "actual-editor-model-preview").put("targetBuild", build)
            .put("device", Build.MODEL).put("sdk", Build.VERSION.SDK_INT).put("quality", "ORIGINAL")
            .put("geometryQuality", "ORIGINAL").put("importEvidenceSchema", 2)
            .put("texturePolicy", "native device and project budget; reductions are reported separately")
            .put("mipPolicy", "native generated mipmaps; retained texture dimensions are not exposed by this interface")
            .put("width", 960).put("height", 540).put("measurement", "SurfaceFlinger pixels and native preview statistics")
            .put("pictureEvidenceSchema", 2).put("runId", runId).put("cleanedArtifacts", cleanedArtifacts)
            .put("artifactPolicy", "direct files in the isolated package's named test folder; no recursive deletion")
            .put("pixelRoi", "central-70-percent-composition").put("pixelInsetFraction", 0.15)
            .put("savedScreenshotScope", "full composition viewport including editor overlays; classification uses the inset ROI")
            .put("playbackBoundsPolicy", "cache stage bounds while paused; no Compose idle wait per playing picture")
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
        fun record(model: String, label: String, elapsed: Long = 0L, extra: JSONObject = JSONObject()): EngineStatus {
            assertTrue(engine.readPerf(perfBuffer))
            val perf = PerfStats.read(perfBuffer)
            val state = status()
            fun finite(value: Float) = if (value.isFinite()) value.toDouble() else 0.0
            extra.put("model", model).put("sample", label).put("elapsedMs", elapsed).put("targetBuild", build).put("runId", runId)
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
            return state
        }
        fun ui(action: () -> Unit) { instrumentation.runOnMainSync(action) }
        var background = 0
        val emptyImageHashes = mutableMapOf<String, Long>()
        val emptyRoiSizes = mutableMapOf<String, Pair<Int, Int>>()
        val pendingPictures = mutableMapOf<String, Int>()
        fun pixels(model: String, label: String, requireModel: Boolean = true, save: Boolean = false,
                   stageBounds: androidx.compose.ui.geometry.Rect? = null,
                   observe: ((Long, Long) -> Unit)? = null): Boolean {
            val began = SystemClock.elapsedRealtime()
            // Semantics queries await Compose idleness. Resolve them while
            // paused; the stage layout stays fixed throughout this play cycle.
            val bounds = stageBounds ?: compose.onNodeWithTag("editor.stage").fetchSemanticsNode().boundsInWindow
            val boundsLookupMs = SystemClock.elapsedRealtime() - began
            assertTrue(engine.readPerf(perfBuffer))
            val capturePerf = PerfStats.read(perfBuffer)
            val screenshotBegan = SystemClock.elapsedRealtime()
            val screenshot = checkNotNull(instrumentation.uiAutomation.takeScreenshot())
            val screenshotMs = SystemClock.elapsedRealtime() - screenshotBegan
            val scanBegan = SystemClock.elapsedRealtime()
            try {
                // Exclude letterboxing and editor controls. Composition is 16:9.
                val width = minOf(bounds.width, bounds.height * 16f / 9f).roundToInt().coerceAtLeast(1)
                val height = (width * 9f / 16f).roundToInt().coerceAtLeast(1)
                val left = (bounds.center.x - width / 2f).roundToInt().coerceIn(0, screenshot.width - 1)
                val top = (bounds.center.y - height / 2f).roundToInt().coerceIn(0, screenshot.height - 1)
                val right = (left + width).coerceAtMost(screenshot.width)
                val bottom = (top + height).coerceAtMost(screenshot.height)
                // The zoom control is inside editor.stage. It must not count
                // as a visible model in an otherwise black/empty cold frame.
                // Keep the full viewport PNG for independent inspection.
                val insetX = ((right - left) * 0.15f).roundToInt()
                val insetY = ((bottom - top) * 0.15f).roundToInt()
                val roiLeft = left + insetX; val roiTop = top + insetY
                val roiRight = right - insetX; val roiBottom = bottom - insetY
                assertTrue("Invalid central model ROI", roiRight > roiLeft && roiBottom > roiTop)
                val histogram = HashMap<Int, Int>()
                var total = 0; var foreground = 0; var green = 0; var imageHash = 1L
                for (y in roiTop until roiBottom step 3) for (x in roiLeft until roiRight step 3) {
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
                // A green cube is a legitimate model. A whole flat green
                // surface is a failure; texture hue alone is not one.
                val flatGreen = green.toDouble() / total.coerceAtLeast(1) > 0.95
                val nativeModelDrawn = capturePerf.draws3D > 0 && capturePerf.triangles3D > 0
                val roiSize = Pair(roiRight - roiLeft, roiBottom - roiTop)
                val emptyImageHash = emptyImageHashes[model]
                val backgroundCompared = requireModel && emptyImageHash != null && emptyRoiSizes[model] == roiSize
                val backgroundDifferent = backgroundCompared && imageHash != emptyImageHash
                // A cached composition can legitimately report no new 3D
                // draws. Visibility requires central ROI pixels differing
                // from this model's measured empty composition instead.
                val valid = if (requireModel) backgroundDifferent && foreground >= maxOf(16, total / 500) && histogram.size >= 3 && !flatGreen
                            else dominantFraction > 0.97 && !flatGreen
                if (!requireModel && valid && dominant != null) {
                    val bin = dominant.key
                    background = Color.rgb(((bin shr 10) and 31) * 8 + 4, ((bin shr 5) and 31) * 8 + 4, (bin and 31) * 8 + 4)
                    emptyImageHashes[model] = imageHash
                    emptyRoiSizes[model] = roiSize
                }
                val sampleLabel = if (!valid && (label == "empty" || label == "cold")) {
                    val key = "$model-$label"
                    val attempt = pendingPictures.getOrDefault(key, 0)
                    pendingPictures[key] = attempt + 1
                    "$label-pending-$attempt"
                } else label
                val pixelScanMs = SystemClock.elapsedRealtime() - scanBegan
                val observed = record(model, sampleLabel, SystemClock.elapsedRealtime() - began,
                    JSONObject().put("validPixels", valid).put("samples", total).put("foreground", foreground)
                        .put("colorBins", histogram.size).put("dominantFraction", dominantFraction)
                        .put("flatGreen", flatGreen).put("imageHash", imageHash).put("screenshotMs", screenshotMs)
                        .put("requestedSample", label).put("backgroundCompared", backgroundCompared)
                        .put("backgroundDifferent", backgroundDifferent)
                        .put("emptyImageHash", emptyImageHashes[model] ?: JSONObject.NULL)
                        .put("boundsLookupMs", boundsLookupMs).put("pixelScanMs", pixelScanMs)
                        .put("cachedPlaybackBounds", stageBounds != null)
                        .put("nativeModelDrawnAtScreenshot", nativeModelDrawn)
                        .put("draws3DAtScreenshot", capturePerf.draws3D).put("triangles3DAtScreenshot", capturePerf.triangles3D)
                        .put("pixelRoi", "central-70-percent-composition").put("pixelInsetFraction", 0.15)
                        .put("roiLeft", roiLeft - left).put("roiTop", roiTop - top)
                        .put("roiRight", roiRight - left).put("roiBottom", roiBottom - top)
                        .put("savedViewportWidth", right - left).put("savedViewportHeight", bottom - top))
                observe?.invoke(imageHash, observed.playhead)
                if (save || !valid || !requireModel) {
                    val cropped = Bitmap.createBitmap(screenshot, left, top, right - left, bottom - top)
                    try { File(folder, "$model-$sampleLabel.png").outputStream().use { cropped.compress(Bitmap.CompressFormat.PNG, 100, it) } }
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
        val sourceTextureCounts = mapOf("DamagedHelmet" to 5, "Fox" to 1, "AnimatedMorphCube" to 0)
        var allTexturesAtSourceResolution = true
        try {
            for ((name, checksum) in fixtures) {
                val model = name.substringBeforeLast('.')
                val source = File(folder, name)
                instrumentation.context.assets.open("models-preview-owned/$name").use { input -> source.outputStream().use { input.copyTo(it) } }
                val actualHash = MessageDigest.getInstance("SHA-256").digest(source.readBytes()).joinToString("") { "%02x".format(it) }
                assertEquals("Fixture must match the licensed benchmark input", checksum, actualHash)
                val sourcePlan = engine.inspectModel(source.absolutePath, ModelPlan.memoryNow(context))
                assertTrue("Pinned fixture must have an exact source plan", sourcePlan.valid && sourcePlan.exact)
                assertEquals("Pinned source texture count changed", sourceTextureCounts.getValue(model), sourcePlan.textures)
                ui { store.newProject(960, 540, 30f, "Model preview $model") }
                compose.waitUntil(60000) { store.project.title == "Model preview $model" && !store.projectOperationBusy }
                ui {
                    // Keep motion active through the bounded screenshot window;
                    // slow SurfaceFlinger observations can consume several seconds.
                    store.setCompositionDuration(360); store.setCompositionBackground(0.012f, 0.005f, 0.025f)
                    store.setPreviewScale(false, 1, 1); store.setLoop(false); store.pause(); store.seek(0)
                }
                compose.waitUntil(30000) { status().duration == 360L && status().playhead == 0L && !status().playing }
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
                // JNI layout is source triangles, kept triangles, textures
                // reduced, textures skipped, quality. Slot 2 is NOT imported
                // textures; ORIGINAL geometry can still hit a texture budget.
                assertEquals("Model import report layout changed", 5, importReport.size)
                assertTrue("Model source geometry was empty", importReport[0] > 0L)
                assertEquals("ORIGINAL import reduced geometry", importReport[0], importReport[1])
                assertEquals("Source plan and imported geometry disagree", sourcePlan.triangles, importReport[0])
                assertEquals("Embedded source textures were skipped", 0L, importReport[3])
                assertEquals("Model quality must stay ORIGINAL", MODEL_QUALITY_ORIGINAL.toLong(), importReport.getOrElse(4) { -1L })
                allTexturesAtSourceResolution = allTexturesAtSourceResolution && importReport[2] == 0L && importReport[3] == 0L
                record(model, "import", SystemClock.elapsedRealtime() - importAt,
                    JSONObject().put("sourceSha256", actualHash).put("sourceBytes", source.length())
                        .put("sourceTextures", sourcePlan.textures).put("plannedTextureCap", sourcePlan.textureCap(MODEL_QUALITY_ORIGINAL))
                        .put("geometryTrianglesUnchanged", importReport[0] == importReport[1])
                        .put("texturesAtSourceResolution", importReport[2] == 0L && importReport[3] == 0L)
                        .put("textureClassification", if (importReport[2] == 0L) "SOURCE_UNCHANGED" else "TEXTURES_REDUCED")
                        .put("importReport", JSONArray(importReport.toList())))
                ui {
                    store.clearSelection()
                    engine.beginCommandBatch()
                    CommandBatch(engine).apply {
                        setLayerTimeRange(layer.id, 0, 360)
                        insertKeyframe(layer.id, TrackProperty.ROTATION_Z, -1, 0, 0, -12f)
                        insertKeyframe(layer.id, TrackProperty.ROTATION_Z, -1, 0, 359, 12f)
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
                    val observedBounds = compose.onNodeWithTag("editor.stage").fetchSemanticsNode().boundsInWindow
                    val playbackBounds = androidx.compose.ui.geometry.Rect(
                        observedBounds.left, observedBounds.top, observedBounds.right, observedBounds.bottom)
                    assertTrue("Missing model before play", pixels(model, "warm-$round-before", stageBounds = playbackBounds))
                    val playAt = SystemClock.elapsedRealtime()
                    ui { store.play() }
                    compose.waitUntil(30000) { status().playhead > 0L }
                    record(model, "warm-$round-start", SystemClock.elapsedRealtime() - playAt)
                    var samples = 0
                    val pictureHashes = mutableSetOf<Long>()
                    val picturePlayheads = mutableSetOf<Long>()
                    val deadline = playAt + 10000
                    do {
                        assertTrue("Blank/flat green playback picture: $model round=$round",
                            pixels(model, "play-$round-$samples", save = true, stageBounds = playbackBounds, observe = { hash, playhead ->
                                pictureHashes.add(hash); picturePlayheads.add(playhead)
                            }))
                        ++samples
                        if (SystemClock.elapsedRealtime() < deadline) SystemClock.sleep(100)
                    } while (SystemClock.elapsedRealtime() < deadline &&
                        (SystemClock.elapsedRealtime() - playAt < 3000 || samples < 3 ||
                         pictureHashes.size < 2 || picturePlayheads.size < 3))
                    val advanced = status().playhead
                    val elapsed = SystemClock.elapsedRealtime() - playAt
                    record(model, "warm-$round-played", elapsed,
                        JSONObject().put("pictureSamples", samples).put("advancedFrames", advanced)
                            .put("distinctPictureHashes", pictureHashes.size).put("timelineSamples", picturePlayheads.size))
                    assertTrue("Playback observed fewer than three actual displayed pictures", samples >= 3)
                    assertTrue("Displayed model picture froze while timeline advanced", pictureHashes.size >= 2)
                    assertTrue("Playback pictures did not span three timeline positions", picturePlayheads.size >= 3)
                    assertTrue("Playback observation must run for at least three seconds", elapsed >= 3000)
                    assertTrue("Playback observations exceeded the ten second deadline", elapsed <= 10000)
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
            report.appendText(JSONObject().put("result", "PASS").put("targetBuild", build).put("runId", runId)
                .put("scope", "actual editor operations and moving pictures; source texture quality is classified separately")
                .put("sourceTextureQualityPass", allTexturesAtSourceResolution).toString() + "\n")
        } catch (error: Throwable) {
            report.appendText(JSONObject().put("result", "FAIL").put("targetBuild", build).put("runId", runId).put("error", error.toString())
                .put("editorError", store.errorMessage).put("busy", store.busyMessage).toString() + "\n")
            throw error
        } finally { ui { store.pause() } }
    }
}
