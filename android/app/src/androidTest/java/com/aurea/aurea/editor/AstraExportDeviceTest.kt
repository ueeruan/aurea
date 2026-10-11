package com.aurea.aurea.editor

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaExtractor
import android.media.MediaFormat
import android.os.SystemClock
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.engine.MotionBlurSettings
import com.aurea.aurea.engine.TrackProperty
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.MessageDigest
import kotlin.math.PI
import kotlin.math.roundToLong
import kotlin.math.sin

/** Real native offline composition with no Surface, Activity or preview loop.
 * Every exported picture is independently decoded by Android MediaCodec.
 * Run only in the isolated uiTest app with -e aureaAstra true. */
class AstraExportDeviceTest {
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext
    private lateinit var engine: AureaEngine
    private val folder get() = File(context.filesDir, "astra-export").apply { mkdirs() }

    @Before fun initializeHeadlessNativeEngine() {
        org.junit.Assume.assumeTrue(InstrumentationRegistry.getArguments().getString("aureaAstra") == "true")
        check(context.packageName == "com.aurea.aurea.uitest")
        for (font in listOf("Roboto-Regular.ttf", "NotoSansJP-Regular.otf")) {
            context.assets.open(font).use { input ->
                File(context.cacheDir, font).outputStream().use { input.copyTo(it) }
            }
        }
        engine = AureaEngine.create(context)
        assertTrue(engine.startupError(), engine.initialize(60f, context.cacheDir.absolutePath, folder.absolutePath, false))
    }
    @After fun releaseNativeEngine() { if (::engine.isInitialized) engine.destroy() }

    private fun awaitCondition(timeout: Long = 30000, predicate: () -> Boolean) {
        val until = SystemClock.elapsedRealtime() + timeout
        while (!predicate()) {
            assertTrue("Native fixture preparation timed out", SystemClock.elapsedRealtime() < until)
            SystemClock.sleep(10)
        }
    }
    private fun scene(frames: Int, width: Int = 320, height: Int = 180): Long {
        assertTrue(engine.newProject(width, height, 30.0, "Astra offline acceptance", floatArrayOf(.03f, .03f, .03f, 1f)))
        val composition = engine.queryComposition(DoubleArray(8)); assertTrue(composition > 0)
        engine.beginCommandBatch()
        assertTrue(CommandBatch(engine).setCompositionDuration(composition, frames))
        assertEquals(1, engine.submitCommands())
        // Apply queued editing commands without an Activity or preview frame.
        assertEquals(0, engine.saveProject(File(folder, "owned-prepared-fixture.aurea").absolutePath))
        awaitCondition { val values = DoubleArray(8); engine.queryComposition(values) != 0L && values[3].toInt() == frames }
        return composition
    }
    private fun animatedShape(frames: Int, width: Int, height: Int) {
        val shape = engine.addShape(1); assertTrue(shape > 0)
        engine.beginCommandBatch(); val commands = CommandBatch(engine)
        assertTrue(commands.setLayerTimeRange(shape, 0, frames))
        assertTrue(commands.setScale(shape, .18f, .18f, 1f))
        assertTrue(commands.setShapeFill(shape, .95f, .1f, .15f, 1f))
        assertTrue(commands.setPosition(shape, width * .2f, height * .5f, 0f))
        assertTrue(commands.insertKeyframe(shape, TrackProperty.POSITION_X, -1, 0, 0, width * .2f))
        assertTrue(commands.insertKeyframe(shape, TrackProperty.POSITION_X, -1, 0, frames - 1, width * .8f))
        assertEquals(6, engine.submitCommands())
    }
    private fun tone(seconds: Int): File {
        val file = File(folder, "owned-stereo-$seconds.wav")
        val samples = seconds * 48000
        val header = ByteBuffer.allocate(44).order(ByteOrder.LITTLE_ENDIAN).apply {
            put("RIFF".toByteArray()); putInt(36 + samples * 4); put("WAVEfmt ".toByteArray())
            putInt(16); putShort(1); putShort(2); putInt(48000); putInt(192000); putShort(4); putShort(16)
            put("data".toByteArray()); putInt(samples * 4)
        }
        file.outputStream().buffered().use { stream ->
            stream.write(header.array())
            val block = ByteBuffer.allocate(4096 * 4).order(ByteOrder.LITTLE_ENDIAN)
            var written = 0
            while (written < samples) {
                block.clear()
                val count = minOf(4096, samples - written)
                repeat(count) { n ->
                    val t = (written + n).toDouble() / 48000
                    block.putShort((sin(2 * PI * 440 * t) * 12000).toInt().toShort())
                    block.putShort((sin(2 * PI * 880 * t) * 12000).toInt().toShort())
                }
                stream.write(block.array(), 0, count * 4); written += count
            }
        }
        return file
    }
    private fun export(name: String, frames: Int, width: Int, height: Int, audio: Boolean, requireAnimation: Boolean = true) {
        val movie = File(folder, "$name.mp4")
        val progressLog = File(folder, "$name-progress.txt")
        progressLog.writeText("headless-native=true preview-surface=false frames=$frames width=$width height=$height\n")
        assertEquals("Native FFmpeg export must open", 0, engine.startExport(movie.absolutePath, minOf(width, height), 30.0, 0, 4))
        val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        val progress = ExportProgress()
        val began = SystemClock.elapsedRealtime()
        var last = -1; var moved = began
        try {
            while (true) {
                assertTrue(engine.exportProgress(buffer)); progress.readFrom(buffer)
                if (progress.framesDone != last) {
                    last = progress.framesDone; moved = SystemClock.elapsedRealtime()
                    if (last % 30 == 0 || last == frames) progressLog.appendText("elapsedMs=${moved - began} frame=$last/${progress.framesTotal} flags=${progress.flags}\n")
                }
                if (progress.finished) break
                assertTrue("No frame progress: ${progress.message}", SystemClock.elapsedRealtime() - moved < 180000)
                assertTrue("Native export exceeded thirty minutes", SystemClock.elapsedRealtime() - began < 1800000)
                SystemClock.sleep(50)
            }
            assertEquals(progress.message, 0, progress.result)
            assertEquals(frames, progress.framesDone); assertEquals(frames, progress.framesTotal)
            assertFalse("Export must never substitute a preview picture", progress.frameFallback)
            assertTrue("No native encoder acceleration was reported", progress.flags and 3 != 0)
            assertTrue(movie.isFile && movie.length() > 0)
            independentlyDecode(movie, frames, width, height, audio, requireAnimation)
            progressLog.appendText("PASS all pictures decoded with exact output timestamps; bytes=${movie.length()}\n")
        } finally { if (!progress.finished) engine.cancelExport() }
    }
    private fun independentlyDecode(movie: File, expected: Int, width: Int, height: Int, audio: Boolean, requireAnimation: Boolean) {
        val extractor = MediaExtractor()
        var decoder: MediaCodec? = null
        try {
            extractor.setDataSource(movie.absolutePath)
            val tracks = (0 until extractor.trackCount).map { extractor.getTrackFormat(it) }
            assertEquals(1, tracks.count { it.getString(MediaFormat.KEY_MIME)?.startsWith("video/") == true })
            assertEquals(if (audio) 1 else 0, tracks.count { it.getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true })
            val track = tracks.indexOfFirst { it.getString(MediaFormat.KEY_MIME)?.startsWith("video/") == true }
            val format = tracks[track]
            assertEquals(width, format.getInteger(MediaFormat.KEY_WIDTH)); assertEquals(height, format.getInteger(MediaFormat.KEY_HEIGHT))
            format.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible)
            extractor.selectTrack(track)
            decoder = MediaCodec.createDecoderByType(checkNotNull(format.getString(MediaFormat.KEY_MIME)))
            decoder.configure(format, null, null, 0); decoder.start()
            val info = MediaCodec.BufferInfo(); var inputEnded = false; var outputEnded = false; var count = 0
            val hashes = HashSet<Long>(); var nonblank = false
            val until = SystemClock.elapsedRealtime() + 600000
            while (!outputEnded) {
                assertTrue("Independent Android decoder timed out at $count", SystemClock.elapsedRealtime() < until)
                if (!inputEnded) {
                    val index = decoder.dequeueInputBuffer(10000)
                    if (index >= 0) {
                        val input = checkNotNull(decoder.getInputBuffer(index))
                        val bytes = extractor.readSampleData(input, 0)
                        if (bytes < 0) {
                            decoder.queueInputBuffer(index, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM); inputEnded = true
                        } else {
                            decoder.queueInputBuffer(index, 0, bytes, extractor.sampleTime, 0); extractor.advance()
                        }
                    }
                }
                val index = decoder.dequeueOutputBuffer(info, 10000)
                if (index >= 0) {
                    if (info.size > 0) {
                        val expectedPts = (count * 1000000.0 / 30).roundToLong()
                        assertTrue("Decoded picture $count has wrong PTS: ${info.presentationTimeUs} versus $expectedPts",
                            kotlin.math.abs(expectedPts - info.presentationTimeUs) <= 2)
                        val image = checkNotNull(decoder.getOutputImage(index)) { "Decoder must expose actual pixels for picture $count" }
                        try {
                            val crop = image.cropRect
                            assertEquals(width, crop.width()); assertEquals(height, crop.height())
                            val plane = image.planes[0]; val pixels = plane.buffer
                            var hash = 1469598103934665603L; var low = 255; var high = 0
                            for (y in 0 until height step 4) for (x in 0 until width step 4) {
                                val value = pixels.get((crop.top + y) * plane.rowStride + (crop.left + x) * plane.pixelStride).toInt() and 255
                                low = minOf(low, value); high = maxOf(high, value)
                                hash = (hash xor value.toLong()) * 1099511628211L
                            }
                            if (high - low > 40) nonblank = true
                            hashes.add(hash)
                        } finally { image.close() }
                        count++
                    }
                    outputEnded = info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0
                    decoder.releaseOutputBuffer(index, false)
                }
            }
            assertEquals("Every rendered picture must be playable", expected, count)
            assertTrue("Decoded composition is blank", nonblank)
            if (requireAnimation) assertTrue("Animated composition exported as a repeated still", hashes.size > 1)
            File(folder, "${movie.nameWithoutExtension}-decode.txt").writeText("decoded=$count distinctPictureHashes=${hashes.size} nonblank=$nonblank\n")
        } finally {
            runCatching { decoder?.stop() }; decoder?.release(); extractor.release()
        }
    }

    @Test fun noPreviewSurfaceExportsEveryAnimatedFrameAndAudio() {
        scene(60); animatedShape(60, 320, 180)
        assertTrue(engine.importAudio(tone(2).absolutePath, "Owned stereo tone") > 0)
        export("headless-2d-stereo", 60, 320, 180, true)
    }
    @Test fun heavy3DParticlesTextAndMotionBlurRemainAnimated() {
        scene(60, 960, 540)
        val models = (0 until 24).map { engine.addShape3d(it % 3, "Mesh $it").also { id -> assertTrue(id > 0) } }
        val particles = engine.addParticles(20); assertTrue(particles > 0)
        val text = engine.addText("ASTRA OFFLINE"); assertTrue(text > 0)
        engine.beginCommandBatch(); val commands = CommandBatch(engine)
        models.forEachIndexed { index, id ->
            assertTrue(commands.setLayerTimeRange(id, 0, 60)); assertTrue(commands.setScale(id, .3f, .3f, .3f))
            assertTrue(commands.setPosition(id, 100f + index % 6 * 145f, 75f + index / 6 * 125f, 0f))
            assertTrue(commands.insertKeyframe(id, TrackProperty.ROTATION_Y, -1, 0, 0, 0f))
            assertTrue(commands.insertKeyframe(id, TrackProperty.ROTATION_Y, -1, 0, 59, 100f))
        }
        assertTrue(commands.setLayerTimeRange(particles, 0, 60)); assertTrue(commands.setLayerTimeRange(text, 0, 60))
        assertTrue(engine.submitCommands() > 0)
        models.forEach { assertTrue(engine.setMotionBlur(it, true)) }
        assertTrue(engine.setMotionBlurSettings(MotionBlurSettings(true, 270f, -135f, 8, 16, 4)))
        export("headless-heavy3d-blur", 60, 960, 540, false)
    }
    @Test fun importedGlbFbxAndObjGeometryExportsThroughOfflineComposition() {
        val sources = listOf("model-triangle.glb", "animated-character.fbx").map { name ->
            File(folder, name).also { target -> instrumentation.context.assets.open(name).use { input ->
                target.outputStream().use { input.copyTo(it) }
            } }
        } + File(folder, "owned-triangle.obj").apply {
            writeText("o OwnedTriangle\nv -1 -1 0\nv 1 -1 0\nv 0 1 0\nf 1 2 3\n")
        }
        for (source in sources) {
            scene(30, 640, 360)
            val detail = arrayOfNulls<String>(1)
            val id = engine.importModel(source.absolutePath, source.name, detail)
            assertTrue("${source.name}: ${detail[0]}", id > 0)
            engine.beginCommandBatch(); val commands = CommandBatch(engine)
            assertTrue(commands.setLayerTimeRange(id, 0, 30))
            assertTrue(commands.setPosition(id, 320f, 180f, 0f))
            assertTrue(commands.insertKeyframe(id, TrackProperty.ROTATION_Y, -1, 0, 0, 0f))
            assertTrue(commands.insertKeyframe(id, TrackProperty.ROTATION_Y, -1, 0, 29, 90f))
            assertEquals(4, engine.submitCommands())
            export("headless-imported-${source.extension}", 30, 640, 360, false)
        }
    }
    @Test fun texturedAnimatedGlbAssetsRenderWithMotionBlurOffline() {
        val directory = File(checkNotNull(InstrumentationRegistry.getArguments().getString("aureaHeavyModelDirectory")) {
            "The textured model test requires the owned fixture directory"
        })
        scene(60, 960, 540)
        val models = (0 until 8).map { index ->
            val source = File(directory, if (index % 2 == 0) "DamagedHelmet.glb" else "Fox.glb")
            assertTrue(source.isFile)
            val detail = arrayOfNulls<String>(1)
            engine.importModel(source.absolutePath, "Textured animated fixture $index", detail).also { id ->
                assertTrue("${source.name}: ${detail[0]}", id > 0)
            }
        }
        engine.beginCommandBatch(); val commands = CommandBatch(engine)
        models.forEachIndexed { index, id ->
            assertTrue(commands.setLayerTimeRange(id, 0, 60)); assertTrue(commands.setScale(id, .5f, .5f, .5f))
            assertTrue(commands.setPosition(id, 120f + index % 4 * 240f, 130f + index / 4 * 270f, 0f))
            assertTrue(commands.insertKeyframe(id, TrackProperty.ROTATION_Y, -1, 0, 0, 0f))
            assertTrue(commands.insertKeyframe(id, TrackProperty.ROTATION_Y, -1, 0, 59, 75f))
        }
        assertTrue(engine.submitCommands() > 0)
        models.forEach { assertTrue(engine.setMotionBlur(it, true)) }
        assertTrue(engine.setMotionBlurSettings(MotionBlurSettings(true, 270f, -135f, 8, 16, 4)))
        export("headless-textured-animated-glb-blur", 60, 960, 540, false)
    }
    @Test fun twoMinutesOfCompositionAndAudioDoNotLosePictures() {
        scene(3600); animatedShape(3600, 320, 180)
        assertTrue(engine.importAudio(tone(120).absolutePath, "Owned two minute stereo") > 0)
        export("headless-long-120s", 3600, 320, 180, true)
    }
    @Test fun oldProjectReadsWithoutRewritingItsBytesAndExportsOffline() {
        val input = InstrumentationRegistry.getArguments().getString("aureaLegacyProject")
        verifyLegacyProject(File(checkNotNull(input) { "Old project validation requires the fixture path" }), "headless-legacy",
            InstrumentationRegistry.getArguments().getString("aureaLegacyAudio") == "true")
    }
    @Test fun existingUserProjectReadsAndExportsWithoutDestructiveMigration() {
        val input = InstrumentationRegistry.getArguments().getString("aureaLegacyUserProject")
        val helper = InstrumentationRegistry.getArguments().getString("aureaLegacyFixtureHelper")
        val models = InstrumentationRegistry.getArguments().getString("aureaHeavyModelDirectory")
        val originalModelPath = checkNotNull(InstrumentationRegistry.getArguments().getString("aureaLegacyOriginalModelPath")) {
            "Original model source identity must be supplied separately from the immutable project"
        }
        System.load(checkNotNull(helper) { "Unmodified Windows fixture requires its instrumentation-only cwd helper" })
        val originalDirectory = android.system.Os.readlink("/proc/self/cwd")
        val working = File(folder, "owned-user-working-directory").apply { mkdirs() }
        require(originalModelPath.length > 3 && originalModelPath[1] == ':' && originalModelPath[2] == '/')
        val target = File(working, originalModelPath)
        require(target.canonicalPath.startsWith(working.canonicalPath + File.separator))
        target.parentFile!!.mkdirs()
        File(checkNotNull(models), "DamagedHelmet.glb").copyTo(target, overwrite = true)
        assertEquals(0, changeFixtureDirectory(working.absolutePath))
        try {
            verifyLegacyProject(File(checkNotNull(input) { "Existing user project validation requires an isolated copy" }), "headless-user-project", false)
        } finally { assertEquals(0, changeFixtureDirectory(originalDirectory)) }
    }
    private external fun changeFixtureDirectory(directory: String): Int

    @Test fun oldProjectWithMissingMediaFailsExplicitlyAndPreservesItsBytes() {
        val file = File(checkNotNull(InstrumentationRegistry.getArguments().getString("aureaMissingMediaProject")))
        val original = file.readBytes()
        assertEquals(0, engine.loadProject(file.absolutePath))
        val movie = File(folder, "missing-media-must-not-publish.mp4")
        assertFalse(movie.exists())
        assertEquals(0, engine.startExport(movie.absolutePath, 36, 30.0, 0, 4))
        val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        val progress = ExportProgress()
        val began = SystemClock.elapsedRealtime()
        try {
            do {
                assertTrue(engine.exportProgress(buffer)); progress.readFrom(buffer)
                assertTrue(SystemClock.elapsedRealtime() - began < 180000)
                if (!progress.finished) SystemClock.sleep(25)
            } while (!progress.finished)
            assertEquals("Missing real source must remain a decode error", 14, progress.result)
            assertFalse(progress.frameFallback)
            assertFalse("Failed export must not publish an invented picture", movie.exists())
            assertArrayEquals(original, file.readBytes())
        } finally { if (!progress.finished) engine.cancelExport() }
    }
    private fun verifyLegacyProject(file: File, name: String, audio: Boolean) {
        assertTrue(file.isFile)
        fun digest() = MessageDigest.getInstance("SHA-256").digest(file.readBytes()).toList()
        val original = digest()
        assertEquals(0, engine.loadProject(file.absolutePath))
        assertEquals("Legacy reader cannot silently omit layers or media", 0, engine.loadNotice() and 11)
        assertEquals("Read must never rewrite the original project", original, digest())
        val composition = DoubleArray(8); assertTrue(engine.queryComposition(composition) > 0)
        val frames = kotlin.math.ceil(composition[3] * 30 / composition[2] - 1e-8).toInt()
        export(name, frames, composition[0].toInt(), composition[1].toInt(), audio, false)
        assertEquals("Export must never rewrite the original project", original, digest())
    }
}
