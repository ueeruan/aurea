package com.aurea.aurea.editor

import android.app.ActivityManager
import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import android.media.MediaMetadataRetriever
import android.os.Debug
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.junit4.ComposeContentTestRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.EngineStatus
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.engine.MotionBlurSettings
import com.aurea.aurea.engine.PerfStats
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference
import kotlin.math.abs
import kotlin.math.max

/** Opt-in Android soak, deliberately separate from the unbounded export battery.
 * All imports, captures, save/reload, memory and native queries run on the test
 * thread. Main only hosts the real editor and submits its normal command queue. */
internal class HeavyEditingSoakHarness(private val compose: ComposeContentTestRule) {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private lateinit var store: EditorStore
    private val engine: AureaEngine get() = store.engineForStress
    private lateinit var journal: SoakJournal
    private var editorVisible by mutableStateOf(true)
    private val statusBuffer = ByteBuffer.allocateDirect(PodLayout.STATUS_BYTES).order(ByteOrder.nativeOrder())
    private val perfBuffer = ByteBuffer.allocateDirect(PerfStats.BYTES).order(ByteOrder.nativeOrder())
    private val captureBuffer = ByteBuffer.allocateDirect(320 * 180 * 4)
    private val dimensions = IntArray(2)
    private val rows = ByteBuffer.allocateDirect(PodLayout.LAYER_ROW_BYTES * 64).order(ByteOrder.nativeOrder())
    private val names = ByteBuffer.allocateDirect(8192)
    private val groups = mutableListOf<Long>()
    private var editableText = 0L
    private var edits = 0
    private var captures = 0
    private var reloads = 0
    private var surfaces = 0
    private var playbackSamples = 0

    fun run() {
        check(context.packageName == "com.aurea.aurea.uitest") { "Never run the soak against user data" }
        assumeTrue(InstrumentationRegistry.getArguments().getString("aureaStress") == "true")
        // The duration excludes fixture generation, scene construction and warm-up.
        val seconds = InstrumentationRegistry.getArguments().getString("aureaStressSeconds")
            ?.toLongOrNull()?.coerceIn(180, 1800) ?: 240L
        val folder = File(context.filesDir, "stress-sustained-${System.currentTimeMillis()}").apply { check(mkdirs()) }
        journal = SoakJournal(folder, context.getSystemService(Application.ACTIVITY_SERVICE) as ActivityManager)
        journal.start()
        try {
            journal.note("START durationSeconds=$seconds package=${context.packageName} pid=${android.os.Process.myPid()} device=${android.os.Build.MODEL} sdk=${android.os.Build.VERSION.SDK_INT}")
            var ready = false
            compose.setContent {
                store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
                ready = true
                AureaTheme {
                    if (editorVisible) EditorScreen(store) else Box(Modifier.fillMaxSize())
                }
            }
            compose.waitUntil(30000) { ready && store.engineReady }
            val readableVideoDiagnostic = InstrumentationRegistry.getArguments()
                .getString("aureaStressReadableVideo") == "true"
            if (readableVideoDiagnostic) {
                // Diagnostic A/B only: preserve the entire workload and pixel
                // assertions while removing the hardware/GL video path.
                engine.useReadableVideoPlanes()
                journal.note("DIAGNOSTIC readableVideoPlanes=true; not the default-path acceptance run")
            }
            val videos = makeFullHdVideos(folder)
            buildScene(videos)
            val project = File(folder, "sustained.aurea")
            assertEquals(0, engine.saveProject(project.absolutePath))
            verifyPlayback(12000)
            cycleSurface()
            reopenAndCompare(project, 45)
            val warmed = settledMemory("warm", project)
            val began = SystemClock.elapsedRealtime()
            var cycles = 0
            while (SystemClock.elapsedRealtime() - began < seconds * 1000 || cycles < 6) {
                journal.healthy()
                journal.stage.set("cycle ${cycles + 1}: edit, seek and play")
                mutateAndSeek(cycles)
                // Check presented pixels before captureFrame can repair a stale preview.
                verifyPresentedPreview("cycle-${cycles + 1}", cycles % 3 == 0)
                val current = capture("cycle-${cycles + 1}")
                assertContent(current)
                if (cycles % 2 == 0) cycleSurface()
                if (cycles % 3 == 0) reopenAndCompare(project, 45)
                verifyPlayback(12000)
                if (cycles % 2 == 1) {
                    compose.runOnIdle { store.pause(); store.setPreviewScale(false, 1, 2) }
                    waitNative("pause before pressure") { !it.playing }
                    val reclaimed = engine.trimMemory(80)
                    journal.note("TRIM cycle=${cycles + 1} reclaimed=$reclaimed")
                }
                val sample = sample("cycle-${++cycles}")
                assertTrue("Java heap exhausted: $sample", sample.javaBytes < Runtime.getRuntime().maxMemory() * .95)
                assertTrue("Workload exceeded the requested duration plus ten minutes", SystemClock.elapsedRealtime() - began < seconds * 1000 + 600000)
            }
            val final = settledMemory("final", project)
            assertBoundedGrowth(warmed, final)
            assertTrue("Surface recreation must actually repeat", surfaces >= 3)
            assertTrue("Save/reload must actually repeat", reloads >= 3)
            assertTrue("Repeated live playback observations are required", playbackSamples >= 30)
            assertEquals(34, engine.queryLayers(rows, 64, names))
            journal.healthy()
            journal.note("ALL PASSED elapsedMs=${SystemClock.elapsedRealtime() - began} cycles=$cycles edits=$edits captures=$captures reloads=$reloads surfaceRecreations=$surfaces livePlaybackSamples=$playbackSamples")
        } catch (failure: Throwable) {
            journal.note("FAILED ${failure.javaClass.name}: ${failure.message}\n${failure.stackTraceToString()}")
            throw failure
        } finally {
            journal.close()
        }
    }

    private fun batch(block: CommandBatch.() -> Unit) {
        compose.runOnIdle {
            engine.beginCommandBatch()
            CommandBatch(engine).block()
            assertTrue("Native command queue rejected a stress operation", engine.submitCommands() > 0)
        }
    }

    private fun status(): EngineStatus {
        assertTrue("Native status unavailable", engine.readStatus(statusBuffer))
        return EngineStatus().also { it.readFrom(statusBuffer) }
    }

    private fun waitNative(label: String, timeout: Long = 30000, predicate: (EngineStatus) -> Boolean): EngineStatus {
        val began = SystemClock.elapsedRealtime()
        var lastNote = began
        while (true) {
            journal.healthy()
            val state = status()
            if (predicate(state)) return state
            assertTrue("$label timed out: frame=${state.playhead}, playing=${state.playing}, error=${state.lastError}", SystemClock.elapsedRealtime() - began < timeout)
            if (SystemClock.elapsedRealtime() - lastNote > 3000) { sample("waiting:$label"); lastNote = SystemClock.elapsedRealtime() }
            Thread.sleep(100)
        }
    }

    private fun seek(frame: Int) {
        val time = engine.frameTimeNs(frame.toLong())
        batch { pause(); seek(time) }
        waitNative("seek $frame") { !it.playing && it.playhead == frame.toLong() }
    }

    private fun newProject(title: String) {
        compose.runOnIdle { store.newProject(1920, 1080, 30f, title) }
        compose.waitUntil(30000) { store.project.title == title && !store.projectOperationBusy }
        compose.runOnIdle {
            store.setCompositionDuration(90)
            store.setCompositionBackground(0f, 0f, 0f, 1f)
            store.setLoop(true)
            store.setPreviewScale(false, 1, 2)
        }
    }

    private fun makeFullHdVideos(folder: File): List<File> {
        journal.stage.set("generate and validate 1080p H.264 fixture")
        newProject("Soak fixture 1080p")
        val source = File(folder, "source-vfr.mp4")
        instrumentation.context.assets.open("preview-vfr.mp4").use { input -> source.outputStream().use { input.copyTo(it) } }
        sample("before fixture import")
        val id = engine.importVideo(source.absolutePath, "Synthetic VFR source")
        assertTrue(id > 0)
        sample("after fixture import")
        batch {
            setLayerTimeRange(id, 0, 90)
            setPosition(id, 960f, 540f, 0f)
            setScale(id, 12f, 12f, 1f)
        }
        seek(0)
        val output = File(folder, "fixture-1080p.mp4")
        val progress = ExportProgress()
        val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        sample("before fixture export")
        assertEquals(0, engine.startExport(output.absolutePath, 1080, 30.0, 0, 8))
        journal.note("FIXTURE export started")
        var movedAt = SystemClock.elapsedRealtime()
        var lastFrame = -1
        try {
            while (!progress.finished) {
                journal.healthy()
                assertTrue(engine.exportProgress(buffer)); progress.readFrom(buffer)
                if (lastFrame != progress.framesDone) {
                    lastFrame = progress.framesDone; movedAt = SystemClock.elapsedRealtime()
                    sample("fixture-export $lastFrame/${progress.framesTotal}")
                }
                assertTrue("Fixture export stalled at $lastFrame", SystemClock.elapsedRealtime() - movedAt < 90000)
                Thread.sleep(250)
            }
            assertEquals(progress.message, 0, progress.result)
            assertEquals(90, progress.framesDone)
            assertFalse("Full HD fixture must contain exact decoded frames", progress.frameFallback)
        } finally { if (!progress.finished) engine.cancelExport() }
        val retriever = MediaMetadataRetriever()
        try {
            retriever.setDataSource(output.absolutePath)
            assertEquals("1920", retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH))
            assertEquals("1080", retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT))
            val frame = checkNotNull(retriever.getFrameAtTime(1_500_000, MediaMetadataRetriever.OPTION_CLOSEST))
            try { assertTrue("Generated fixture is blank", (0 until frame.width step 60).map { frame.getPixel(it, frame.height / 2) }.distinct().size > 5) }
            finally { frame.recycle() }
        } finally { retriever.release() }
        journal.note("FIXTURE Full HD 1920x1080 frames=90 bytes=${output.length()}")
        return (0..3).map { index -> File(folder, "independent-video-$index.mp4").also { output.copyTo(it) } }
    }

    private fun buildScene(videos: List<File>) {
        journal.stage.set("create 48 layers, 92 effects, four Full HD decoders")
        newProject("Sustained Full HD 48 layers")
        val ids = mutableListOf<Long>()
        val effectRows = ByteBuffer.allocateDirect(3 * 32).order(ByteOrder.nativeOrder())
        val effectNames = ByteBuffer.allocateDirect(2048)
        for (index in 0 until 48) {
            val kind = index % 12
            journal.stage.set("create layer $index kind=$kind")
            sample("before layer $index")
            val id = when (kind) {
                0 -> engine.importVideo(videos[index / 12].absolutePath, "1080p video ${index / 12}")
                1 -> engine.addShape3d(index / 12 % 3, "3D $index")
                2, 3 -> engine.addText("AUREA $index")
                else -> engine.addShape(index % 5)
            }
            assertTrue("Layer $index failed", id > 0)
            sample("after layer $index id=$id")
            ids += id
            if (index == 2) editableText = id
            val x = 120f + index % 8 * 240f
            val y = 90f + index / 8 * 180f
            batch {
                setLayerTimeRange(id, 0, 90)
                setPosition(id, x, y, 0f)
                val scale = if (kind == 0) .105f else .42f
                setScale(id, scale, scale, scale)
                if (kind in 2..3) { setTextSize(id, 72f); setTextStrokeWidth(id, 6f); setTextStrokeColor(id, 1f, .15f, .1f, 1f) }
                if (kind >= 4) setShapeFill(id, if (index % 3 == 0) 1f else .2f, if (index % 3 == 1) 1f else .2f, if (index % 3 == 2) 1f else .2f, 1f)
                for (frame in listOf(0, 30, 60, 89)) {
                    insertKeyframe(id, TrackProperty.POSITION_X, -1, 0, frame, x + if (frame in 30..60) 40f else 0f)
                    insertKeyframe(id, TrackProperty.ROTATION_Z, -1, 0, frame, frame * 2f)
                }
                if (kind == 0) addEffect(id, effectTypeId("aurea.stylize.motion_tile"))
                if (kind != 1) {
                    addEffect(id, effectTypeId("aurea.blur.gaussian"))
                    addEffect(id, effectTypeId("aurea.light.glow"))
                }
            }
            if (kind != 1) {
                val count = if (kind == 0) 3 else 2
                waitNative("effects of layer $index") { engine.queryLayerEffects(id, effectRows, 3, effectNames) == count }
                batch {
                    val offset = if (kind == 0) 32 else 0
                    if (kind == 0) {
                        val tile = effectRows.getInt(0)
                        setEffectParam(id, tile, 3, 150f); setEffectParam(id, tile, 4, 150f)
                        setEffectParam(id, tile, 5, 1f); setEffectParam(id, tile, 10, 70f)
                    }
                    setEffectParam(id, effectRows.getInt(offset), 0, 3f)
                    setEffectParam(id, effectRows.getInt(offset + 32), 0, 20f)
                }
            }
            if (index % 12 == 11) sample("created=${index + 1}")
        }
        for (start in listOf(32, 40)) {
            val group = engine.precompose(ids.subList(start, start + 8).toLongArray())
            assertTrue("Precompose failed", group > 0); groups += group
            assertTrue(engine.openPrecomp(group))
            assertEquals(8, engine.queryLayers(rows, 64, names))
            assertTrue(engine.closePrecomp())
        }
        // Exercise transform motion blur without turning the soak into 32x export.
        assertTrue(engine.setMotionBlur(editableText, true))
        val blur = checkNotNull(engine.queryMotionBlurSettings())
        assertTrue(engine.setMotionBlurSettings(MotionBlurSettings(true, 180f, -90f, 4, 4, blur.previewSamples)))
        compose.runOnIdle { store.clearSelection() }
        assertEquals(34, engine.queryLayers(rows, 64, names))
        seek(0)
        verifyPresentedPreview("built", true)
        val first = capture("frame-0")
        seek(45)
        val middle = capture("frame-45")
        assertContent(first); assertContent(middle)
        assertTrue("Animated scene must change its rendered pixels", first.indices.count { abs((first[it].toInt() and 255) - (middle[it].toInt() and 255)) > 8 } > 500)
        sample("BUILT sourceLayers=48 rootLayers=34 precomps=2 videos1080p=4 effects=92 keys=384")
    }

    private fun mutateAndSeek(cycle: Int) {
        val frame = listOf(75, 12, 60, 0, 45, 25)[cycle % 6]
        val previousRevision = status().modelRevision
        val burst = LongArray(12) { engine.frameTimeNs(((it * 17 + cycle * 7) % 90).toLong()) }
        val finalTime = engine.frameTimeNs(frame.toLong())
        batch {
            pause()
            setPreviewScale(false, 1, if (cycle % 3 == 0) 1 else if (cycle % 3 == 1) 2 else 4)
            setTextStrokeWidth(editableText, if (cycle % 2 == 0) 12f else 6f)
            burst.forEach { seek(it) }
            seek(finalTime)
        }
        edits++
        waitNative("last seek wins") {
            !it.playing && it.playhead == frame.toLong() && it.modelRevision != previousRevision
        }
    }

    private fun verifyPlayback(duration: Long) {
        journal.stage.set("live playback")
        batch { seek(0L); setLoop(true); play() }
        waitNative("play starts", 45000) { it.playing && it.playhead > 0 }
        val began = SystemClock.elapsedRealtime()
        val frames = mutableSetOf<Long>()
        val firstPresented = verifyPresentedPreview("playing-start", false)
        var presentedChanged = false
        var iteration = 0
        while (SystemClock.elapsedRealtime() - began < duration) {
            journal.healthy()
            val current = status()
            assertTrue("Native playback stopped unexpectedly at ${current.playhead}", current.playing)
            frames += current.playhead
            sample("play frame=${current.playhead}")
            playbackSamples++
            if (iteration++ % 2 == 1) {
                val shown = verifyPresentedPreview("playing-progress", false)
                val changed = shown.indices.count {
                    abs(Color.red(shown[it]) - Color.red(firstPresented[it])) > 20 ||
                        abs(Color.green(shown[it]) - Color.green(firstPresented[it])) > 20 ||
                        abs(Color.blue(shown[it]) - Color.blue(firstPresented[it])) > 20
                }
                presentedChanged = presentedChanged || changed > 8
                journal.note("PRESENTED motion changedSamples=$changed/${shown.size}")
            }
            Thread.sleep(1100)
        }
        assertTrue("Live native playhead did not advance ($frames)", frames.size >= 3)
        assertTrue("Playhead advanced but the displayed composition stayed frozen", presentedChanged)
        batch { pause() }
        waitNative("pause") { !it.playing }
        verifyPresentedPreview("playback", false)
    }

    private fun cycleSurface() {
        journal.stage.set("surface recreation ${surfaces + 1}")
        seek(45)
        compose.runOnIdle { store.onEnterBackground(); editorVisible = false }
        // Removing EditorScreen disposes the actual SurfaceView, triggering its
        // production surfaceDestroyed callback. No fake native attach/detach.
        compose.waitForIdle()
        Thread.sleep(750)
        journal.healthy()
        compose.runOnIdle { editorVisible = true; store.onEnterForeground() }
        compose.waitUntil(30000) { store.engineReady }
        seek(45)
        verifyPresentedPreview("surface-${++surfaces}", true)
        sample("SURFACE recovered=$surfaces")
    }

    private fun reopenAndCompare(project: File, frame: Int) {
        journal.stage.set("save/reload ${reloads + 1}")
        seek(frame)
        val before = capture("before-reload")
        val beforeRepeat = capture("before-reload-repeat")
        assertEquals(0, engine.saveProject(project.absolutePath))
        assertTrue(project.length() > 1000)
        assertEquals(0, engine.loadProject(project.absolutePath))
        assertEquals(34, engine.queryLayers(rows, 64, names))
        seek(frame)
        verifyPresentedPreview("reload-${reloads + 1}", true)
        val after = capture("after-reload")
        val afterRepeat = capture("after-reload-repeat")
        assertContent(after)
        val maximum = before.indices.maxOf { abs((before[it].toInt() and 255) - (after[it].toInt() and 255)) }
        val changed = before.indices.count { abs((before[it].toInt() and 255) - (after[it].toInt() and 255)) > 4 }
        fun repeatChanged(a: ByteArray, b: ByteArray) = a.indices.count { abs((a[it].toInt() and 255) - (b[it].toInt() and 255)) > 4 }
        journal.note("CAPTURE STABILITY beforeChanged=${repeatChanged(before, beforeRepeat)} afterChanged=${repeatChanged(after, afterRepeat)}")
        journal.note("RELOAD ${++reloads} maxDifference=$maximum changedChannels=$changed/${before.size}")
        if (changed > before.size / 1000) {
            assertEquals(0, engine.saveProject(File(journal.folder, "reload-$reloads-after.aurea").absolutePath))
            File(journal.folder, "reload-$reloads-format.txt").writeText("RGBA8, 320x180, rowBytes=1280, frame=$frame\n")
            for ((label, rgba) in listOf("before" to before, "before-repeat" to beforeRepeat,
                "after" to after, "after-repeat" to afterRepeat)) {
                File(journal.folder, "reload-$reloads-$label.rgba").writeBytes(rgba)
                val bitmap = Bitmap.createBitmap(320, 180, Bitmap.Config.ARGB_8888)
                try {
                    bitmap.copyPixelsFromBuffer(ByteBuffer.wrap(rgba))
                    File(journal.folder, "reload-$reloads-$label.png").outputStream().use {
                        bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)
                    }
                } finally { bitmap.recycle() }
            }
            sample("RELOAD PARITY FAILURE frame=$frame")
        }
        assertTrue("Save/reload changed visible content: maximum=$maximum changed=$changed", changed <= before.size / 1000)
    }

    private fun capture(label: String): ByteArray {
        journal.stage.set("capture $label")
        journal.note("CAPTURE begin=$label")
        captureBuffer.clear()
        val began = SystemClock.elapsedRealtime()
        val count = engine.captureFrame(320, captureBuffer, dimensions)
        journal.note("CAPTURE end=$label elapsedMs=${SystemClock.elapsedRealtime() - began} bytes=$count size=${dimensions.contentToString()}")
        assertEquals(320, dimensions[0]); assertEquals(180, dimensions[1]); assertEquals(320 * 180 * 4, count)
        captures++
        return ByteArray(count).also { captureBuffer.rewind(); captureBuffer.get(it) }
    }

    private fun assertContent(bytes: ByteArray) {
        var lit = 0; var colored = 0
        for (i in bytes.indices step 4) {
            val r = bytes[i].toInt() and 255; val g = bytes[i + 1].toInt() and 255; val b = bytes[i + 2].toInt() and 255
            if (maxOf(r, g, b) > 70) lit++
            if (maxOf(r, g, b) - minOf(r, g, b) > 25) colored++
        }
        assertTrue("Dense scene vanished: lit=$lit colored=$colored", lit > 1000 && colored > 500)
    }

    private fun verifyPresentedPreview(label: String, save: Boolean): IntArray {
        journal.stage.set("presented preview $label")
        val stage = compose.onNodeWithTag("editor.stage").fetchSemanticsNode().boundsInRoot
        val began = SystemClock.elapsedRealtime()
        var detail = ""
        while (SystemClock.elapsedRealtime() - began < 30000) {
            journal.healthy()
            val bitmap = checkNotNull(instrumentation.uiAutomation.takeScreenshot())
            var colored = 0; var lit = 0; var samples = 0
            val cyan = IntArray(3)
            val magenta = IntArray(3)
            val colors = IntArray(25 * 41)
            try {
                // Restrict to the 16:9 composition inside the stage's letterbox.
                val width = minOf(stage.width, stage.height * 16f / 9f) * .9f
                val height = width * 9f / 16f
                for (y in 0..24) for (x in 0..40) {
                    val px = (stage.center.x - width / 2 + width * x / 40).toInt().coerceIn(0, bitmap.width - 1)
                    val py = (stage.center.y - height / 2 + height * y / 24).toInt().coerceIn(0, bitmap.height - 1)
                    val pixel = bitmap.getPixel(px, py)
                    val r = Color.red(pixel); val g = Color.green(pixel); val b = Color.blue(pixel)
                    if (maxOf(r, g, b) > 70) lit++
                    if (maxOf(r, g, b) - minOf(r, g, b) > 25) colored++
                    // The synthetic footage contains repeated cyan/magenta
                    // bands. Generic colored shapes still appear when every
                    // video is missing, so they cannot be the ready signal.
                    val region = minOf(2, x * 3 / 41)
                    if (g > 160 && b > 160 && r < 110) cyan[region]++
                    if (r > 160 && b > 160 && g < 110) magenta[region]++
                    colors[samples] = pixel
                    samples++
                }
                detail = "label=$label colored=$colored lit=$lit samples=$samples videoCyan=${cyan.contentToString()} videoMagenta=${magenta.contentToString()}"
                val videoPresent = cyan.sum() >= 20 && magenta.sum() >= 20 &&
                    (0..2).all { cyan[it] >= 4 && magenta[it] >= 4 }
                if (lit > 20 && colored > 10 && videoPresent) {
                    if (save) File(journal.folder, "presented-$label.png").outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
                    journal.note("PRESENTED $detail")
                    return colors
                }
                if (SystemClock.elapsedRealtime() - began > 29000) File(journal.folder, "failed-preview.png").outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
            } finally { bitmap.recycle() }
            sample("waiting-preview $detail")
            Thread.sleep(500)
        }
        fail("Live surface did not present scene content: $detail")
        error("unreachable")
    }

    private data class Memory(val pssBytes: Long, val javaBytes: Long, val nativeBytes: Long, val gpuBytes: Long, val budgetBytes: Long)

    private fun sample(label: String): Memory {
        journal.healthy()
        val mem = Debug.MemoryInfo().also { Debug.getMemoryInfo(it) }
        val runtime = Runtime.getRuntime()
        val java = runtime.totalMemory() - runtime.freeMemory()
        val native = Debug.getNativeHeapAllocatedSize()
        val measured = engine.readPerf(perfBuffer)
        val perf = if (measured) PerfStats.read(perfBuffer) else PerfStats()
        val state = status()
        val system = ActivityManager.MemoryInfo().also { (context.getSystemService(Application.ACTIVITY_SERVICE) as ActivityManager).getMemoryInfo(it) }
        val snapshot = Memory(mem.totalPss * 1024L, java, native, perf.gpuReservedBytes, perf.memoryBudgetMB * 1024L * 1024)
        journal.note("SAMPLE $label $snapshot available=${system.availMem} lowMemory=${system.lowMemory} javaMax=${runtime.maxMemory()} measured=$measured frame=${state.playhead} playing=${state.playing} error=${state.lastError} preview=${perf.previewWidth}x${perf.previewHeight} cpuMs=${perf.cpuFrameMs} gpuMs=${perf.gpuFrameMs} decodeMs=${perf.decodeMs} presentMs=${perf.presentMs} p50=${perf.pacingP50Ms} p95=${perf.pacingP95Ms} p99=${perf.pacingP99Ms} pacingSamples=${perf.pacingSamples} gpuUsed=${perf.gpuMemoryBytes} gpuAllocations=${perf.gpuAllocations} textures=${perf.physicalTextures} ram=${perf.ramBytes} decodedBytes=${perf.decodedCacheBytes} stale=${perf.staleFrames} dropped=${perf.droppedFrames} thermal=${perf.thermal} hwDecoder=${perf.hardwareDecoder} decoder=${perf.decoder} effects=${perf.activeEffects} passes=${perf.passesExecuted}")
        assertEquals("Native engine error during $label", 0, state.lastError)
        return snapshot
    }

    private fun settledMemory(label: String, project: File): Memory {
        // Use the same scene, frame, scale, empty undo history and trim policy at
        // both checkpoints. Compare retained memory, not a full cache vs cold boot.
        compose.runOnIdle { store.pause(); store.setPreviewScale(false, 1, 2) }
        reopenAndCompare(project, 45)
        journal.note("SETTLE $label reclaimed=${engine.trimMemory(80)}")
        System.gc()
        Thread.sleep(1500)
        val points = (0..2).map { Thread.sleep(500); sample("retained-$label-$it") }
        return Memory(points.minOf { it.pssBytes }, points.minOf { it.javaBytes }, points.minOf { it.nativeBytes }, points.minOf { it.gpuBytes }, points.maxOf { it.budgetBytes })
    }

    private fun assertBoundedGrowth(warm: Memory, final: Memory) {
        val mib = 1024L * 1024
        // Regression ceilings account for allocator retention and the configured
        // cache budget. Raw samples remain the evidence; this is not a leak proof.
        val budget = max(warm.budgetBytes, final.budgetBytes)
        val nativeAllowance = max(64 * mib, budget / 2)
        val pssAllowance = max(128 * mib, budget)
        val javaAllowance = max(32 * mib, Runtime.getRuntime().maxMemory() / 4)
        journal.note("GROWTH warm=$warm final=$final allowanceJava=$javaAllowance allowanceNativeGpu=$nativeAllowance allowancePss=$pssAllowance")
        assertTrue("Retained Java heap keeps growing: $warm -> $final", final.javaBytes <= warm.javaBytes + javaAllowance)
        assertTrue("Retained native heap keeps growing: $warm -> $final", final.nativeBytes <= warm.nativeBytes + nativeAllowance)
        assertTrue("Retained process memory keeps growing: $warm -> $final", final.pssBytes <= warm.pssBytes + pssAllowance)
        assertTrue("Retained GPU allocation keeps growing: $warm -> $final", final.gpuBytes <= warm.gpuBytes + nativeAllowance)
    }
}

/** Does not call the engine or Compose: it still writes when native work blocks. */
private class SoakJournal(val folder: File, private val activityManager: ActivityManager) {
    val stage = AtomicReference("initialization")
    private val file = File(folder, "progress.txt")
    private val started = SystemClock.elapsedRealtime()
    private val running = AtomicBoolean(true)
    private val pulsePostedAt = AtomicLong(0)
    private val failure = AtomicReference<String?>(null)
    private val main = Handler(Looper.getMainLooper())
    private val pulse = Runnable { pulsePostedAt.set(0) }
    private val watcher = Thread({
        var lastMemory = 0L
        while (running.get()) {
            if (pulsePostedAt.compareAndSet(0, SystemClock.uptimeMillis())) main.post(pulse)
            // Measure an unanswered request. If this worker was starved while
            // main stayed responsive, the newly posted request gets a fresh
            // deadline instead of reporting an old heartbeat as an ANR.
            val posted = pulsePostedAt.get()
            val lag = if (posted == 0L) 0 else SystemClock.uptimeMillis() - posted
            if (lag >= 5000 && failure.compareAndSet(null, "Main thread unresponsive for ${lag}ms during ${stage.get()}")) {
                note("ANR_WATCHDOG ${failure.get()}\n${Looper.getMainLooper().thread.stackTrace.joinToString("\n")}")
            }
            if (SystemClock.elapsedRealtime() - lastMemory >= 5000) {
                val runtime = Runtime.getRuntime()
                val memory = ActivityManager.MemoryInfo().also { activityManager.getMemoryInfo(it) }
                val process = Debug.MemoryInfo().also { Debug.getMemoryInfo(it) }
                note("WATCHDOG mainLagMs=$lag javaBytes=${runtime.totalMemory() - runtime.freeMemory()} nativeBytes=${Debug.getNativeHeapAllocatedSize()} pssKB=${process.totalPss} available=${memory.availMem} threshold=${memory.threshold} lowMemory=${memory.lowMemory}")
                lastMemory = SystemClock.elapsedRealtime()
            }
            try { Thread.sleep(500) } catch (_: InterruptedException) { break }
        }
    }, "aurea-soak-watchdog").apply { isDaemon = true }

    fun start() = watcher.start()
    @Synchronized fun note(text: String) {
        val line = "t=${SystemClock.elapsedRealtime() - started} stage=${stage.get()} $text"
        file.appendText(line + "\n")
        Log.i("AureaHeavySoak", line)
    }
    fun healthy() { failure.get()?.let { throw AssertionError(it) } }
    fun close() { running.set(false); watcher.interrupt(); watcher.join(2000); main.removeCallbacks(pulse) }
}
