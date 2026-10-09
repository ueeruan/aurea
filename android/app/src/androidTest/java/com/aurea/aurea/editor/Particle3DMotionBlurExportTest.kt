package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMetadataRetriever
import android.os.Debug
import android.os.Process
import android.os.SystemClock
import android.provider.MediaStore
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.engine.MotionBlurSettings
import com.aurea.aurea.engine.LayerDetail
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ExportOptions
import com.aurea.aurea.state.ExportPhase
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Production GPU and encoders; opt-in and isolated from the user's projects. */
class Particle3DMotionBlurExportTest {
    @get:Rule val compose = createComposeRule()

    @Test fun eightAnimatedObjectsAndParticularExportWithForcedMotionBlur() {
        org.junit.Assume.assumeTrue(InstrumentationRegistry.getArguments().getString("aureaStress") == "true")
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName == "com.aurea.aurea.uitest")
        val arguments = InstrumentationRegistry.getArguments()
        val forced = arguments.getString("aureaForcedBlur") == "true"
        val source = File(checkNotNull(arguments.getString("aureaProjectPath")) { "The user's portable project is required" })
        assertTrue("Push the original project into the isolated test app first", source.isFile)
        val label = if (forced) "forced" else "original"
        val folder = File(context.filesDir, "project8-$label").apply { mkdirs() }
        val report = File(folder, "progress.txt")
        report.writeText("Project 8; native ${if (Process.is64Bit()) 64 else 32}-bit process; forced=$forced; quality=High; complete original timeline\n")
        fun note(message: String) {
            val memory = Debug.MemoryInfo().also { Debug.getMemoryInfo(it) }
            report.appendText("$message pssKB=${memory.totalPss}\n")
        }
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        val engine = store.engineForStress
        val runId = SystemClock.elapsedRealtime()
        val imported = File(folder, "Project8-copy-$runId.aurea")
        val packageResult = engine.importProjectPackage(source.absolutePath, imported.absolutePath, File(folder, "media-$runId").absolutePath)
        assertEquals("Portable project must import successfully", "0", packageResult.getOrNull(0))
        assertEquals("All original media must resolve", "0", packageResult.getOrNull(4))
        compose.runOnIdle { store.openProject(imported.absolutePath) }
        compose.waitUntil(120000) { store.project.path == imported.absolutePath && !store.projectOperationBusy && store.layers.isNotEmpty() }
        assertEquals("Project must open without corruption, partial-load or missing-media flags", 0, engine.loadNotice() and 11)
        val original = DoubleArray(8)
        assertTrue(engine.queryComposition(original) != 0L)
        val totalFrames = original[3].toInt()
        val originalFps = original[2]
        assertTrue(totalFrames > 0 && originalFps > 0)
        note("IMPORTED width=${original[0]} height=${original[1]} fps=$originalFps frames=$totalFrames rootLayers=${store.layers.size}")
        compose.runOnIdle { store.pause() }
        var visibleCaptures = 0
        for (frame in listOf(0, totalFrames / 4, totalFrames / 2, totalFrames * 3 / 4, totalFrames - 1).distinct()) {
            compose.runOnIdle { store.seek(frame) }
            compose.waitUntil(30000) { store.playhead == frame }
            val rgba = ByteBuffer.allocateDirect(320 * maxOf(320, kotlin.math.ceil(320 * original[1] / original[0]).toInt() + 4) * 4)
            val dimensions = IntArray(2)
            val bytes = engine.captureFrame(320, rgba, dimensions)
            assertTrue("Original project must render frame $frame", bytes > 0)
            val bitmap = Bitmap.createBitmap(dimensions[0], dimensions[1], Bitmap.Config.ARGB_8888)
            try {
                rgba.rewind(); bitmap.copyPixelsFromBuffer(rgba)
                File(folder, "original-frame-$frame.png").outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
                var lit = 0
                for (y in 0 until bitmap.height step 2) for (x in 0 until bitmap.width step 2) {
                    val pixel = bitmap.getPixel(x, y)
                    if (maxOf(Color.red(pixel), Color.green(pixel), Color.blue(pixel)) > 35) lit++
                }
                if (lit > 100) visibleCaptures++
                note("PREVIEW frame=$frame lit=$lit")
            } finally { bitmap.recycle() }
        }
        assertTrue("The ready original edit must render visible content", visibleCaptures > 0)
        compose.runOnIdle { store.seek(0) }
        var particlesId = 0L
        if (forced) compose.runOnIdle {
            // queryLayers addresses the current composition. Traverse precomps
            // explicitly, otherwise their 3D children retain the original state.
            var verifiedLayers = 0
            var verified3d = 0
            fun enableComposition(depth: Int) {
                assertTrue("Precomposition nesting must be bounded", depth <= 64)
                var capacity = 256
                var rows = ByteBuffer.allocateDirect(capacity * PodLayout.LAYER_ROW_BYTES).order(ByteOrder.nativeOrder())
                val names = ByteBuffer.allocateDirect(1024 * 1024)
                var count = engine.queryLayers(rows, capacity, names)
                while (count >= capacity) {
                    capacity *= 2
                    rows = ByteBuffer.allocateDirect(capacity * PodLayout.LAYER_ROW_BYTES).order(ByteOrder.nativeOrder())
                    count = engine.queryLayers(rows, capacity, names)
                }
                val ids = List(count) { index ->
                    val offset = index * PodLayout.LAYER_ROW_BYTES
                    Triple(rows.getLong(offset), rows.getInt(offset + 8), rows.getInt(offset + 28))
                }
                assertTrue(engine.setMotionBlurSettings(MotionBlurSettings(true, 360f, -180f, 8, 16, 4)))
                val detail = ByteBuffer.allocateDirect(LayerDetail.BYTES).order(ByteOrder.nativeOrder())
                for ((id, kind, flags) in ids) {
                    assertTrue("Enable blur on layer $id at depth $depth", engine.setMotionBlur(id, true))
                    assertTrue(engine.queryLayerDetail(id, detail))
                    assertTrue("Read back blur on layer $id at depth $depth", LayerDetail.read(detail).motionBlur)
                    verifiedLayers++
                    if (kind == 10 || (flags and 32) != 0) verified3d++
                    if (kind == 12) {
                        assertTrue(engine.openPrecomp(id))
                        try { enableComposition(depth + 1) } finally { assertTrue(engine.closePrecomp()) }
                    }
                }
                assertTrue(engine.queryMotionBlurSettings()!!.enabled)
            }
            enableComposition(0)
            note("BLUR_VERIFIED originalLayers=$verifiedLayers original3d=$verified3d; includes nested compositions; shutter=360 samples=8 adaptive=16")
            // The supplied edit already contains many 3D objects. Extra models
            // are an optional benchmark, not required to validate this edit.
            val addedObjects = arguments.getString("aureaAdded3dObjects")?.toInt() ?: maxOf(0, 3 - verified3d)
            for (index in 0 until addedObjects) {
                val objectId = engine.addShape3d(index % 3, "Animated 3D $index")
                assertTrue(objectId > 0)
                engine.beginCommandBatch()
                val commands = CommandBatch(engine)
                commands.setLayerTimeRange(objectId, 0, totalFrames)
                commands.setScale(objectId, .55f, .55f, .55f)
                val x = original[0].toFloat() * (.15f + index % 4 * .23f)
                val y = original[1].toFloat() * (.25f + index / 4 * .5f)
                commands.setPosition(objectId, x, y, 0f)
                commands.insertKeyframe(objectId, TrackProperty.POSITION_X, -1, 0, 0, x)
                commands.insertKeyframe(objectId, TrackProperty.POSITION_X, -1, 0, totalFrames - 1, x + original[0].toFloat() * .07f)
                commands.insertKeyframe(objectId, TrackProperty.ROTATION_Z, -1, 0, 0, 0f)
                commands.insertKeyframe(objectId, TrackProperty.ROTATION_Z, -1, 0, totalFrames - 1, 160f)
                assertTrue(engine.submitCommands() > 0)
                assertTrue(engine.setMotionBlur(objectId, true))
                val detail = ByteBuffer.allocateDirect(LayerDetail.BYTES).order(ByteOrder.nativeOrder())
                assertTrue(engine.queryLayerDetail(objectId, detail))
                assertTrue("Added 3D object $index must have blur enabled", LayerDetail.read(detail).motionBlur)
            }
            val particles = engine.addShape(0)
            particlesId = particles
            assertTrue(particles > 0)
            engine.beginCommandBatch()
            val commands = CommandBatch(engine)
            commands.setLayerTimeRange(particles, 0, totalFrames)
            commands.setPosition(particles, original[0].toFloat() * .5f, original[1].toFloat() * .5f, 0f)
            commands.setScale(particles, 8f, 8f, 1f)
            commands.addEffect(particles, effectTypeId("aurea.generate.particular"))
            assertTrue(engine.submitCommands() > 0)
            assertTrue(engine.setMotionBlur(particles, true))
            assertTrue(engine.setMotionBlurSettings(MotionBlurSettings(true, 360f, -180f, 8, 16, 4)))
            store.clearSelection()
        }
        if (forced) {
        val particles = particlesId
        val effectRows = ByteBuffer.allocateDirect(32).order(ByteOrder.nativeOrder())
        val effectNames = ByteBuffer.allocateDirect(1024)
        compose.waitUntil(15000) { engine.queryLayerEffects(particles, effectRows, 1, effectNames) == 1 }
        val effectId = effectRows.getInt(0)
        compose.runOnIdle {
            engine.beginCommandBatch()
            val commands = CommandBatch(engine)
            // Particular uses milliseconds for pre-roll and lifetime.
            for ((parameter, value) in listOf(0 to 120f, 1 to 1500f, 5 to 120f,
                6 to 80f, 9 to 350f, 22 to 1500f, 24 to 6f, 38 to 1f, 39 to 360f)) {
                commands.setEffectParam(particles, effectId, parameter, value)
            }
            assertEquals(9, engine.submitCommands())
        }
        assertTrue(engine.queryMotionBlurSettings()!!.enabled)
        }
        val capabilities = checkNotNull(engine.deviceReport())
        note("READY forced=$forced; encoderLimit=${capabilities.maxExportWidth}x${capabilities.maxExportHeight}")
        val requestedHeight = arguments.getString("aureaExportHeight")?.toInt() ?: 1080
        val requestedFps = arguments.getString("aureaExportFps")?.toDouble() ?: originalFps
        val requestedCodec = arguments.getString("aureaExportCodec")?.toInt() ?: 0
        for ((height, fps, codec) in listOf(Triple(requestedHeight, requestedFps, requestedCodec))) {
            val output = File(folder, "stress-${height}p-${fps.toInt()}fps-${if (codec == 0) "avc" else "hevc"}.mp4")
            compose.runOnIdle {
                store.exporter.start("Project8 test $label ${if (Process.is64Bit()) 64 else 32}bit",
                    original[0].toInt(), original[1].toInt(), originalFps,
                    ExportOptions(shortSide = height, fps = fps, hevc = codec == 1, quality = 2, trimToContent = false))
            }
            val progress = ExportProgress()
            val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
            var lastFrame = -1
            var lastProgress = SystemClock.elapsedRealtime()
            val deadline = lastProgress + 5400000
            try {
                do {
                    assertNotEquals("Export startup/publication must succeed: ${store.exporter.state.message}", ExportPhase.Failed, store.exporter.state.phase)
                    assertTrue(engine.exportProgress(buffer)); progress.readFrom(buffer)
                    if (progress.framesDone != lastFrame) {
                        lastFrame = progress.framesDone; lastProgress = SystemClock.elapsedRealtime()
                        if (lastFrame % 4 == 0) note("EXPORT $height/$fps/$codec frame=$lastFrame/${progress.framesTotal}")
                    }
                    if (progress.finished) break
                    assertTrue("Export GPU/encoder stalled at frame $lastFrame", SystemClock.elapsedRealtime() - lastProgress < 90000)
                    SystemClock.sleep(100)
                } while (SystemClock.elapsedRealtime() < deadline)
                assertTrue("Export deadline exceeded", progress.finished)
                assertEquals(progress.message, 0, progress.result)
                assertFalse("Full quality must not use approximate frame fallback", progress.frameFallback)
                val expectedFrames = kotlin.math.ceil(totalFrames * fps / originalFps - 1e-6).toInt()
                assertEquals(expectedFrames, progress.framesDone)
                val publishDeadline = SystemClock.elapsedRealtime() + 120000
                while (store.exporter.state.phase !in listOf(ExportPhase.Done, ExportPhase.Failed) && SystemClock.elapsedRealtime() < publishDeadline) {
                    SystemClock.sleep(100)
                }
                assertEquals("The complete edit must appear in the gallery: ${store.exporter.state.message}", ExportPhase.Done, store.exporter.state.phase)
                val published = checkNotNull(store.exporter.state.outputUri)
                context.contentResolver.query(published, arrayOf(MediaStore.MediaColumns.IS_PENDING), null, null, null)!!.use {
                    assertTrue(it.moveToFirst()); assertEquals("Gallery item must be published", 0, it.getInt(0))
                }
                context.contentResolver.openInputStream(published)!!.use { input -> output.outputStream().use { input.copyTo(it) } }
                note("GALLERY uri=$published bytes=${output.length()}")
                val extractor = MediaExtractor()
                try {
                    extractor.setDataSource(output.absolutePath)
                    val track = (0 until extractor.trackCount).first { extractor.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("video/") == true }
                    val format = extractor.getTrackFormat(track)
                    val outputHeight = format.getInteger(MediaFormat.KEY_HEIGHT)
                    val outputWidth = format.getInteger(MediaFormat.KEY_WIDTH)
                    assertEquals(height, minOf(outputHeight, outputWidth))
                    assertEquals(original[0] / original[1], outputWidth.toDouble() / outputHeight, 2.0 / outputHeight)
                    assertEquals(if (codec == 0) "video/avc" else "video/hevc", format.getString(MediaFormat.KEY_MIME))
                    extractor.selectTrack(track)
                    var frames = 0; var previousTime = -1L
                    while (extractor.sampleTime >= 0) {
                        assertTrue("Encoded timestamps must advance", extractor.sampleTime > previousTime)
                        previousTime = extractor.sampleTime; frames++
                        if (!extractor.advance()) break
                    }
                    assertEquals(expectedFrames, frames)
                    assertEquals((expectedFrames - 1) * 1000000.0 / fps, previousTime.toDouble(), 2.0)
                } finally { extractor.release() }
                val reader = MediaMetadataRetriever()
                try {
                    reader.setDataSource(output.absolutePath)
                    var visibleFrames = 0
                    val durationUs = totalFrames * 1000000.0 / originalFps
                    for (time in listOf(0L, (durationUs * .25).toLong(), (durationUs * .5).toLong(), (durationUs * .75).toLong())) {
                        val bitmap = checkNotNull(reader.getScaledFrameAtTime(time, MediaMetadataRetriever.OPTION_CLOSEST, 320, 180))
                        try {
                            var visible = 0
                            for (y in 0 until bitmap.height step 2) for (x in 0 until bitmap.width step 2) {
                                val pixel = bitmap.getPixel(x, y)
                                if (maxOf(Color.red(pixel), Color.green(pixel), Color.blue(pixel)) > 35) visible++
                            }
                            if (visible > 100) visibleFrames++
                        } finally { bitmap.recycle() }
                    }
                    assertTrue("The original edit must produce independently decoded visible content", visibleFrames > 0)
                } finally { reader.release() }
                note("PASS $height/$fps/$codec frames=$expectedFrames bytes=${output.length()} fallback=false")
            } finally { if (store.exporter.busy) compose.runOnIdle { store.exporter.cancel() } }
        }
        note("PASS COMPLETE PROJECT EXPORT forced=$forced")
    }
}
