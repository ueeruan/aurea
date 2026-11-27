package com.aurea.aurea.editor

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaExtractor
import android.media.MediaFormat
import android.os.SystemClock
import android.util.Base64
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.engine.MotionBlurSettings
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.engine.LayerEffect
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.presets.PresetKind
import com.aurea.aurea.presets.PresetLibrary
import com.aurea.aurea.state.Shape3DCatalog
import com.aurea.aurea.state.Shape3DInfo
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
        val debugBudget = InstrumentationRegistry.getArguments().getString("aureaDenseDebug") == "true"
        assertTrue(engine.startupError(), engine.initialize(60f, context.cacheDir.absolutePath, folder.absolutePath, debugBudget))
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
    private fun animatedShape(frames: Int, width: Int, height: Int): Long {
        val shape = engine.addShape(1); assertTrue(shape > 0)
        engine.beginCommandBatch(); val commands = CommandBatch(engine)
        assertTrue(commands.setLayerTimeRange(shape, 0, frames))
        assertTrue(commands.setScale(shape, .18f, .18f, 1f))
        assertTrue(commands.setShapeFill(shape, .95f, .1f, .15f, 1f))
        assertTrue(commands.setPosition(shape, width * .2f, height * .5f, 0f))
        assertTrue(commands.insertKeyframe(shape, TrackProperty.POSITION_X, -1, 0, 0, width * .2f))
        assertTrue(commands.insertKeyframe(shape, TrackProperty.POSITION_X, -1, 0, frames - 1, width * .8f))
        assertEquals(6, engine.submitCommands())
        return shape
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
    private fun export(name: String, frames: Int, width: Int, height: Int, audio: Boolean, requireAnimation: Boolean = true,
                       trimDuringExport: Boolean = false, safeMode: Int = 0) {
        val movie = File(folder, "$name.mp4")
        val progressLog = File(folder, "$name-progress.txt")
        progressLog.writeText("headless-native=true preview-surface=false frames=$frames width=$width height=$height\n")
        assertEquals("Native FFmpeg export must open", 0,
            engine.startExport(movie.absolutePath, minOf(width, height), 30.0, 0, 4, safeMode = safeMode))
        val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        val progress = ExportProgress()
        val began = SystemClock.elapsedRealtime()
        var last = -1; var moved = began
        var trimmed = false
        try {
            while (true) {
                assertTrue(engine.exportProgress(buffer)); progress.readFrom(buffer)
                if (progress.framesDone != last) {
                    last = progress.framesDone; moved = SystemClock.elapsedRealtime()
                    if (last % 30 == 0 || last == frames) progressLog.appendText("elapsedMs=${moved - began} frame=$last/${progress.framesTotal} flags=${progress.flags}\n")
                }
                if (progress.finished) break
                if (trimDuringExport && !trimmed && progress.framesDone in 1 until frames) {
                    engine.trimMemory(80)
                    trimmed = true
                    progressLog.appendText("memory-warning-at-frame=${progress.framesDone}\n")
                }
                assertTrue("No frame progress: ${progress.message}", SystemClock.elapsedRealtime() - moved < 180000)
                assertTrue("Native export exceeded thirty minutes", SystemClock.elapsedRealtime() - began < 1800000)
                SystemClock.sleep(50)
            }
            assertEquals(progress.message, 0, progress.result)
            if (trimDuringExport) {
                assertTrue("The memory warning must land during encoding", trimmed)
                assertFalse("The scene must retain every element", progress.elementsOmitted)
                assertFalse("The export must retain the requested motion blur", progress.blurReduced)
                assertFalse("The export must retain the requested resolution", progress.resolutionCapped)
                assertFalse("The export must retain the original 3D models", progress.modelFallback)
            }
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

    @Test fun portableSourceExportsImportedVideoWithEffectAndAudio() {
        scene(60); animatedShape(60, 320, 180)
        export("portable-source-fixture", 60, 320, 180, false)
        scene(60)
        val clip = engine.importVideo(File(folder, "portable-source-fixture.mp4").absolutePath, "Portable source")
        assertTrue(clip > 0)
        val tone = tone(2)
        assertTrue(engine.importAudio(tone.absolutePath, "Owned stereo tone") > 0)
        engine.beginCommandBatch()
        assertTrue(CommandBatch(engine).addEffect(clip, effectTypeId("aurea.blur.gaussian")))
        assertEquals(1, engine.submitCommands())
        assertEquals(0, engine.saveProject(File(folder, "portable-imported-video.aurea").absolutePath))
        export("portable-imported-video", 60, 320, 180, true, safeMode = 2)
    }

    @Test fun importsColorLooksAndRendersEachPreset() {
        val input = File(context.filesDir, "color-studio-input")
        org.junit.Assume.assumeTrue(input.isDirectory)
        val source = File(input, "original.jpg")
        val image = checkNotNull(BitmapFactory.decodeFile(source.absolutePath))
        val width = image.width
        val height = image.height
        val imagePixels = ByteBuffer.allocateDirect(width * height * 4)
        val row = IntArray(width)
        for (y in 0 until height) {
            image.getPixels(row, 0, width, 0, y, width, 1)
            for (pixel in row) {
                imagePixels.put((pixel shr 16).toByte()).put((pixel shr 8).toByte())
                    .put(pixel.toByte()).put((pixel ushr 24).toByte())
            }
        }
        image.recycle()
        val output = File(folder, "color-studio").apply { mkdirs() }
        fun capture(name: String? = null): ByteArray {
            val buffer = ByteBuffer.allocateDirect(width * height * 4)
            val dimensions = IntArray(2)
            assertEquals(buffer.capacity(), engine.captureFrame(maxOf(width, height), buffer, dimensions))
            assertArrayEquals(intArrayOf(width, height), dimensions)
            buffer.rewind()
            if (name != null) {
                val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
                bitmap.copyPixelsFromBuffer(buffer)
                File(output, "$name.png").outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
                bitmap.recycle()
                buffer.rewind()
            }
            return ByteArray(buffer.capacity()).also { buffer.get(it) }
        }
        val galleryNames = linkedMapOf(
            "4K QUALITY CC" to "Pulso Cromático",
            "DARK CC" to "Sombra Fria",
            "HDR CC" to "Luz de Estádio",
            "Lapis CC" to "Azul Mineral",
        )
        val galleryPresets = mutableListOf<Pair<String, String>>()
        fun importPortrait(): Long {
            scene(30, width, height)
            imagePixels.rewind()
            val layer = engine.importImage(imagePixels, width, height, "Retrato", source.absolutePath)
            assertTrue(layer > 0)
            return layer
        }
        for ((preset, galleryName) in galleryNames) {
            val layer = importPortrait()
            val original = capture("Original")
            val file = File(input, "$preset.ffx")
            val report = org.json.JSONObject(engine.importAeData(file.readBytes(), file.name, layer))
            File(output, "$preset.json").writeText(report.toString(2))
            assertTrue(report.toString(), report.getBoolean("ok"))
            val rows = ByteBuffer.allocateDirect(32 * LayerEffect.ROW_BYTES).order(ByteOrder.nativeOrder())
            val names = ByteBuffer.allocateDirect(16384)
            val count = engine.queryLayerEffects(layer, rows, 32, names)
            val stack = (0 until count).map { LayerEffect.read(rows, it, names) }
            val looks = stack.filter { it.typeId == effectTypeId("aurea.color.studio_look") }
            assertTrue(report.toString(), looks.any { it.enabled })
            val result = capture(preset)
            assertFalse(preset, result.contentEquals(original))
            if (preset == "4K QUALITY CC" || preset == "HDR CC") {
                val project = File(output, "$preset.aurea")
                assertTrue(looks.any { engine.colorLutName(layer, it.effectId).endsWith(".cube") })
                assertEquals(0, engine.saveProject(project.absolutePath))
                assertEquals(0, engine.loadProject(project.absolutePath))
                assertTrue(looks.any { engine.colorLutName(layer, it.effectId).endsWith(".cube") })
            }
            engine.beginCommandBatch()
            val batch = CommandBatch(engine)
            for (look in looks) assertTrue(batch.setEffectEnabled(layer, look.effectId, false))
            assertEquals(looks.size, engine.submitCommands())
            assertFalse(preset, capture().contentEquals(result))
            engine.beginCommandBatch()
            val isolate = CommandBatch(engine)
            for (entry in stack) {
                val queued = if (entry in looks) isolate.setEffectEnabled(layer, entry.effectId, entry.enabled)
                    else isolate.removeEffect(layer, entry.effectId)
                assertTrue(queued)
            }
            assertEquals(stack.size, engine.submitCommands())
            val lookRender = capture("$preset-looks")
            assertFalse(preset, lookRender.contentEquals(original))
            val portable = checkNotNull(engine.savePreset(layer, PresetKind.Effects.id, galleryName))
            val embeddedLut = org.json.JSONObject(portable).optJSONArray("colorLuts")
            if (preset in setOf("4K QUALITY CC", "HDR CC")) {
                assertEquals(1, embeddedLut?.length())
                val single = checkNotNull(engine.saveEffectPreset(layer, looks.first().effectId, galleryName))
                assertEquals(1, org.json.JSONObject(single).optJSONArray("colorLuts")?.length())
            }
            val destination = importPortrait()
            assertNull(engine.applyPreset(destination, portable))
            assertTrue("$galleryName changed when reapplied", capture().contentEquals(lookRender))
            galleryPresets.add(galleryName to portable)
        }
        if (InstrumentationRegistry.getArguments().getString("aureaSeedColorGallery") == "true") {
            val library = PresetLibrary(context.applicationContext as android.app.Application)
            for ((name, json) in galleryPresets) {
                if (library.exists(PresetKind.Effects, name)) {
                    val entry = library.entries(PresetKind.Effects).first { !it.builtin && it.name == name }
                    assertEquals(library.jsonOf(entry), json)
                } else {
                    assertNotNull(library.save(PresetKind.Effects, name, json))
                }
            }
        }
    }

    @Test fun importKeepsOtherEffectsWhenALookRecipeIsIncomplete() {
        val fixture = File(context.filesDir, "color-studio-input/DARK CC.ffx")
        org.junit.Assume.assumeTrue(fixture.isFile)
        val source = fixture.readBytes()
        val signature = "MBL2".toByteArray(Charsets.US_ASCII)
        val start = (0..source.size - signature.size).first { i -> signature.indices.all { source[i + it] == signature[it] } }
        val raw = ByteBuffer.wrap(source).order(ByteOrder.BIG_ENDIAN)
        fun record(tag: String, tool: String? = null): Int {
            var cursor = start + 8
            while (cursor + 8 <= source.size) {
                val kind = source.decodeToString(cursor, cursor + 4)
                val length = raw.getInt(cursor + 4)
                check(length in 0..(source.size - cursor - 8))
                if (kind == tag && (tool == null || (length >= 4 &&
                            source.decodeToString(cursor + 8, cursor + 12) == tool))) return cursor
                cursor += 8 + length
            }
            error("Missing fixture record $tag")
        }
        fun patch(at: Int, value: Int) = source.copyOf().also { ByteBuffer.wrap(it).putInt(at, value) }
        fun apply(data: ByteArray): org.json.JSONObject {
            scene(10, 32, 32)
            val pixels = ByteBuffer.allocateDirect(4).putInt(-1).apply { rewind() }
            val layer = engine.importImage(pixels, 1, 1, "Fixture", "")
            assertTrue(layer > 0)
            val report = org.json.JSONObject(engine.importAeData(data, fixture.name, layer))
            assertTrue(report.toString(), report.getBoolean("ok"))
            return report
        }
        fun skips(report: org.json.JSONObject): List<org.json.JSONObject> {
            val items = report.getJSONArray("skipped")
            return (0 until items.length()).map(items::getJSONObject)
        }
        val expected = apply(source).getInt("effects")
        assertEquals(expected, apply(patch(start + 4, 999)).getInt("effects"))
        val unsupportedTool = apply(patch(record("tool", "husa") + 8, 0x6e657721)) // "new!"
        assertEquals(expected, unsupportedTool.getInt("effects"))
        assertTrue(skips(unsupportedTool).any { it.optString("name").endsWith("new!") })

        for ((at, value) in listOf(
            record("#tls") + 8 to 0,
            record("Tstr") + 8 to 1024,
            record("strn") + 4 to Int.MAX_VALUE,
            record("strn") + 8 to 0x7ff80000,
        )) {
            val report = apply(patch(at, value))
            assertEquals(report.toString(), expected - 1, report.getInt("effects"))
            assertTrue(skips(report).any { it.optString("reasonKey") == "ae_reason_looks_invalid" })
        }
    }

    @Test fun dynamicTurbulenceUsesTheReferenceEffectAndPinsNegativeDisplacement() {
        val width = 256
        val height = 144
        scene(60, width, height)
        val colors = intArrayOf(0xffffff, 0xffff00, 0x00ffff, 0x00ff00, 0xff00ff, 0xff0000, 0x0000ff, 0)
        val source = ByteBuffer.allocateDirect(width * height * 4)
        for (y in 0 until height) for (x in 0 until width) {
            val color = colors[x * colors.size / width]
            source.put((color shr 16).toByte()).put((color shr 8).toByte()).put(color.toByte()).put((-1).toByte())
        }
        source.rewind()
        val layer = engine.importImage(source, width, height, "Turbulent Displace bars", "")
        assertTrue(layer > 0)
        fun capture(): ByteArray {
            val pixels = ByteBuffer.allocateDirect(width * height * 4)
            assertEquals(pixels.capacity(), engine.captureFrame(width, pixels, IntArray(2)))
            return ByteArray(pixels.capacity()).also { pixels.rewind(); pixels.get(it) }
        }
        val original = capture()
        val presets = org.json.JSONArray(context.assets.open("presets/efeitos.json").bufferedReader().use { it.readText() })
        val preset = (0 until presets.length()).map(presets::getJSONObject).single {
            it.getString("name").startsWith("Preset Turbul")
        }
        val presetEffect = preset.getJSONArray("effects").getJSONObject(0)
        assertEquals("evolution", presetEffect.getJSONArray("tracks").getJSONObject(0).getString("param"))
        engine.applyPreset(layer, preset.toString())
        val rows = ByteBuffer.allocateDirect(LayerEffect.ROW_BYTES).order(ByteOrder.nativeOrder())
        val names = ByteBuffer.allocateDirect(1024)
        assertEquals(1, engine.queryLayerEffects(layer, rows, 1, names))
        val effect = LayerEffect.read(rows, 0, names)
        assertEquals(effectTypeId("aurea.distort.turbulent_displace_reference"), effect.typeId)

        fun pins(mode: Float) {
            engine.beginCommandBatch()
            val commands = CommandBatch(engine)
            assertTrue(commands.setEffectParam(layer, effect.effectId, 1, -180f))
            assertTrue(commands.setEffectParam(layer, effect.effectId, 2, 65f))
            assertTrue(commands.setEffectParam(layer, effect.effectId, 9, mode))
            assertEquals(3, engine.submitCommands())
        }
        fun changedEdge(pixels: ByteArray): Int = (0 until width).count { x ->
            (0..2).any { offset ->
                val i = x * 4 + offset
                kotlin.math.abs((original[i].toInt() and 255) - (pixels[i].toInt() and 255)) > 2
            }
        }
        pins(0f)
        val free = capture()
        pins(8f)
        val locked = capture()
        assertTrue("An unpinned edge should move", changedEdge(free) > 20)
        assertEquals("Negative amount must preserve the locked edge", 0, changedEdge(locked))
        assertFalse("Locking the edge must still distort the interior", original.contentEquals(locked))
    }

    @Test fun faceRigDoesNotPaintDarkBandsFromTransparentSkinSamples() {
        val width = 384
        val height = 384
        scene(15, width, height)
        val photo = ByteBuffer.allocateDirect(width * height * 4)
        repeat(width * height) { i ->
            if (i / width in 118..248) {
                photo.put(if (i % width / 16 % 2 == 0) 240.toByte() else 190.toByte())
                photo.put(170.toByte()).put(130.toByte()).put(255.toByte())
            } else {
                repeat(4) { photo.put(0) }
            }
        }
        photo.rewind()
        val layer = engine.importImage(photo, width, height, "Face rig opacity fixture", "")
        assertTrue(layer > 0)
        val type = effectTypeId("aurea.rig.face")
        engine.beginCommandBatch()
        assertTrue(CommandBatch(engine).addEffect(layer, type))
        assertEquals(1, engine.submitCommands())
        val raw = ByteBuffer.allocateDirect(width * height * 4)
        val dimensions = IntArray(2)
        assertEquals(raw.capacity(), engine.capturePreviewFrame(width, raw, dimensions))
        val rows = ByteBuffer.allocateDirect(4 * LayerEffect.ROW_BYTES).order(ByteOrder.nativeOrder())
        val names = ByteBuffer.allocateDirect(1024)
        val count = engine.queryLayerEffects(layer, rows, 4, names)
        val effect = List(count) { LayerEffect.read(rows, it, names) }.single { it.typeId == type }.effectId
        // Both skin reference rows land outside the alpha silhouette while
        // the center of the stretched eyelid remains in the opaque source.
        assertTrue(engine.faceRigMoveHandle(layer, effect, 2, .37f, .46f, false))
        assertTrue(engine.faceRigMoveHandle(layer, effect, 3, .46f, .565f, false))
        assertTrue(engine.faceRigMoveHandle(layer, effect, 4, .63f, .46f, false))
        assertTrue(engine.faceRigMoveHandle(layer, effect, 5, .72f, .565f, false))
        engine.beginCommandBatch()
        val edit = CommandBatch(engine)
        assertTrue(edit.setEffectParam(layer, effect, 3, 0f))
        assertTrue(edit.setEffectParam(layer, effect, 4, 0f))
        assertEquals(2, engine.submitCommands())

        val output = File(folder, "face-rig-transparent-samples.mp4")
        export(output.nameWithoutExtension, 15, width, height, false, requireAnimation = false)
        for (preview in listOf(true, false)) {
            val captured = if (preview) engine.capturePreviewFrame(width, raw, dimensions)
                else engine.captureFrame(width, raw, dimensions)
            assertEquals(raw.capacity(), captured)
            assertArrayEquals(intArrayOf(width, height), dimensions)
            for (x in listOf(142, 242)) {
                val red = raw.get((166 * width + x) * 4).toInt() and 255
                assertTrue("Transparent reference painted a dark band at x=$x, preview=$preview ($red)", red > 150)
            }
        }
    }

    @Test fun textSpringKeepsItsProgressKeysAndExportsAnimatedFrames() {
        scene(45)
        val text = engine.addText("AUREA")
        assertTrue(text > 0)
        assertTrue(engine.applyMotionTextPreset(text, 10))
        val project = File(folder, "text-spring.aurea")
        assertEquals(0, engine.saveProject(project.absolutePath))
        assertEquals(0, engine.loadProject(project.absolutePath))
        val fields = checkNotNull(engine.textAnimatorFields(text, 0))
        assertEquals(3f, fields[21], 0f)
        val progress = checkNotNull(engine.textAnimValues(text, 0, intArrayOf(0)))
        assertTrue(progress[1].toInt() and 1 != 0)
        export("headless-text-spring", 45, 320, 180, false)
    }

    @Test fun explainerWipeAndChromaticZoomRenderAsEditableEffects() {
        val width = 320
        val height = 180
        val frames = 30
        scene(frames, width, height)
        val shape = engine.addShape(1)
        assertTrue(shape > 0)
        fun edit(block: CommandBatch.() -> Unit) {
            engine.beginCommandBatch()
            CommandBatch(engine).block()
            assertTrue(engine.submitCommands() > 0)
        }
        edit {
            assertTrue(setLayerTimeRange(shape, 0, frames))
            assertTrue(setPosition(shape, width * .5f, height * .5f, 0f))
            assertTrue(setShapeFill(shape, 1f, 1f, 1f, 1f))
        }
        fun frame(): ByteArray {
            val rgba = ByteBuffer.allocateDirect(width * height * 4)
            assertEquals(rgba.capacity(), engine.captureFrame(width, rgba, IntArray(2)))
            return ByteArray(rgba.capacity()) { rgba.get(it) }
        }
        fun add(key: String): Int {
            val type = effectTypeId(key)
            edit { assertTrue(addEffect(shape, type)) }
            frame()
            val rows = ByteBuffer.allocateDirect(4 * LayerEffect.ROW_BYTES).order(ByteOrder.nativeOrder())
            val names = ByteBuffer.allocateDirect(1024)
            val count = engine.queryLayerEffects(shape, rows, 4, names)
            return List(count) { LayerEffect.read(rows, it, names) }.single { it.typeId == type }.effectId
        }
        fun red(pixels: ByteArray, x: Int): Int = pixels[(height / 2 * width + x) * 4].toInt() and 255
        val original = frame()
        val wipe = add("aurea.transition.range_wipe")
        assertArrayEquals("An open wipe keeps the original", original, frame())
        edit {
            assertTrue(setEffectParam(shape, wipe, 0, 25f))
            assertTrue(setEffectParam(shape, wipe, 1, 75f))
        }
        val strip = frame()
        for (x in listOf(width / 2 - 20, width / 2 + 20)) {
            assertTrue(red(original, x) > 200)
            assertTrue(red(strip, x) < 100)
        }
        assertTrue(red(strip, width / 2) > 200)
        edit { assertTrue(setEffectEnabled(shape, wipe, false)) }

        val blur = add("aurea.blur.chromatic_zoom")
        edit {
            assertTrue(setEffectParam(shape, blur, 1, 160f))
            assertTrue(setEffectParam(shape, blur, 4, 80f))
        }
        fun coloredPixels(pixels: ByteArray): Int = (0 until width * height).count {
            val offset = it * 4
            kotlin.math.abs((pixels[offset].toInt() and 255) - (pixels[offset + 2].toInt() and 255)) > 20
        }
        assertTrue("Zoom trails separate RGB on a neutral subject", coloredPixels(frame()) > 50)
        edit { assertTrue(setEffectParam(shape, blur, 4, 0f)) }
        assertEquals("Zero separation keeps neutral colors", 0, coloredPixels(frame()))
    }

    @Test fun textPrismRendersLiveTextStackedGradientsAndAlpha() {
        val frames = 30
        val width = 480
        val height = 270
        scene(frames, width, height)
        fun capture(): IntArray {
            val pixels = ByteBuffer.allocateDirect(width * height * 4)
            assertEquals(pixels.capacity(), engine.captureFrame(width, pixels, IntArray(2)))
            return IntArray(width * height) { n ->
                val offset = n * 4
                val r = pixels.get(offset).toInt() and 255
                val g = pixels.get(offset + 1).toInt() and 255
                val b = pixels.get(offset + 2).toInt() and 255
                (255 shl 24) or (r shl 16) or (g shl 8) or b
            }
        }
        fun commands(block: CommandBatch.() -> Unit) {
            engine.beginCommandBatch()
            CommandBatch(engine).block()
            assertTrue(engine.submitCommands() > 0)
        }
        fun saveSample(name: String, pixels: IntArray) {
            val image = Bitmap.createBitmap(pixels, width, height, Bitmap.Config.ARGB_8888)
            try {
                File(folder, "text-prism-$name.png").outputStream().use { image.compress(Bitmap.CompressFormat.PNG, 100, it) }
            } finally {
                image.recycle()
            }
        }
        val blank = capture()
        val id = engine.addText("PRISM")
        assertTrue(id > 0)
        val type = effectTypeId("aurea.text.prism")
        assertTrue(AureaEngine.nativeEffectRequiresHyper(type))
        assertNull(AureaEngine.nativeEffectProductId(type))
        commands { assertTrue(setTextSize(id, 92f)); assertTrue(setLayerTimeRange(id, 0, frames)) }
        val original = capture()
        commands { assertTrue(addEffect(id, type)) }
        fun effects(): List<LayerEffect> {
            val rows = ByteBuffer.allocateDirect(4 * LayerEffect.ROW_BYTES).order(ByteOrder.nativeOrder())
            val names = ByteBuffer.allocateDirect(1024)
            return List(engine.queryLayerEffects(id, rows, 4, names)) { LayerEffect.read(rows, it, names) }
        }
        val first = effects().single().effectId
        val linear = capture()
        val saturated = linear.count { c ->
            val p = listOf(c shr 16 and 255, c shr 8 and 255, c and 255)
            p.max() - p.min() > 40
        }
        assertTrue("Prism should color the live glyphs", saturated > 250)
        assertFalse(original.contentEquals(linear))
        commands { assertTrue(setEffectParam(id, first, 0, 1f)) }
        val radial = capture()
        commands { assertTrue(setEffectParam(id, first, 0, 2f)) }
        val conical = capture()
        saveSample("linear", linear)
        saveSample("radial", radial)
        saveSample("conical", conical)
        assertFalse(linear.contentEquals(radial))
        assertFalse(conical.contentEquals(radial))
        commands { assertTrue(setEffectParam(id, first, 5, 0f)) }
        val rgb = capture()
        commands { assertTrue(setEffectParam(id, first, 5, 2f)) }
        val oklab = capture()
        assertFalse(rgb.contentEquals(conical))
        assertFalse(conical.contentEquals(oklab))
        commands {
            assertTrue(setEffectParam(id, first, 5, 1f))
            assertTrue(setEffectParam(id, first, 48, 3f))
            assertTrue(setEffectParam(id, first, 49, 1f))
        }
        val stroke = capture()
        saveSample("stroke", stroke)
        fun changed(from: IntArray, i: Int): Boolean = listOf(0, 8, 16).any { shift ->
            kotlin.math.abs((from[i] shr shift and 255) - (blank[i] shr shift and 255)) > 32
        }
        assertTrue("The generated outline is visible without enabling the text stroke", stroke.indices.count { changed(stroke, it) } > 150)
        val neighborhood = (-4..4).flatMap { y ->
            (-4..4).filter { x -> x * x + y * y <= 16 }.map { x -> y * width + x }
        }
        val interior = (4 until height - 4).flatMap { y -> (4 until width - 4).map { x -> y * width + x } }.filter { i ->
            changed(conical, i) && neighborhood.all { changed(conical, i + it) }
        }
        assertTrue("The reference has an opaque interior", interior.size > 200)
        val cleared = interior.count { !changed(stroke, it) }
        assertTrue("Solo stroke removes the filled interior ($cleared of ${interior.size} pixels)", cleared > interior.size * .9)
        commands {
            assertTrue(setEffectParam(id, first, 48, 0f))
            assertTrue(setEffectParam(id, first, 49, 0f))
            assertTrue(setEffectParam(id, first, 44, 35f))
        }
        assertFalse("Path motion must move the glyph coverage too", conical.contentEquals(capture()))
        commands { assertTrue(setEffectParam(id, first, 44, 0f)) }
        val transparent = floatArrayOf(0f, 1f, 0f, 0f, 0f, 1f, 0f, 1f, 0f, 0f)
        commands { assertTrue(setEffectParam(id, first, 3, 0f)); assertTrue(setEffectParam(id, first, 6, 4f)) }
        assertTrue(engine.setEffectGradient(id, first, 4, transparent))
        assertTrue("Transparent stops should form a matte", blank.contentEquals(capture()))
        commands {
            assertTrue(setEffectParam(id, first, 3, 5f))
            assertTrue(setEffectParam(id, first, 6, 0f))
            assertTrue(addEffect(id, type))
            assertTrue(setTextContent(id, "AUREA"))
            assertTrue(setTextStrokeWidth(id, 0f))
        }
        capture()
        val second = effects().last().effectId
        commands {
            assertTrue(setEffectParam(id, second, 2, 1f))
            assertTrue(setEffectParam(id, second, 3, 2f))
            assertTrue(setEffectParam(id, second, 48, 3f))
            assertTrue(setEffectParam(id, first, 57, 1f))
            assertTrue(setEffectParam(id, first, 20, 2f))
            assertTrue(setEffectParam(id, first, 21, 9f))
            assertTrue(setEffectParam(id, first, 42, 2f))
            assertTrue(insertKeyframe(id, TrackProperty.EFFECT_PARAM, first, 13 * 4, 0, 0f))
            assertTrue(insertKeyframe(id, TrackProperty.EFFECT_PARAM, first, 13 * 4, frames - 1, 180f))
            assertTrue(insertKeyframe(id, TrackProperty.EFFECT_PARAM, first, 44 * 4, 0, 0f))
            assertTrue(insertKeyframe(id, TrackProperty.EFFECT_PARAM, first, 44 * 4, 15, -26f))
            assertTrue(insertKeyframe(id, TrackProperty.EFFECT_PARAM, first, 44 * 4, 29, 0f))
        }
        val beforeReopen = capture()
        assertFalse(conical.contentEquals(beforeReopen))
        val project = File(folder, "text-prism.aurea")
        assertEquals(0, engine.saveProject(project.absolutePath))
        assertEquals(0, engine.loadProject(project.absolutePath))
        assertTrue("Stacked palettes should survive project reopen", beforeReopen.contentEquals(capture()))
        export("headless-text-prism", frames, width, height, false)
    }

    @Test fun pinnedMaskKeepsItsCompositionCoordinatesAcrossEditsAndReload() {
        scene(30)
        val shape = engine.addShape(1)
        assertTrue(shape > 0)
        val points = floatArrayOf(35f, 35f, 0f, 0f, 0f, 0f, 155f, 35f, 0f, 0f, 0f, 0f,
            155f, 145f, 0f, 0f, 0f, 0f, 35f, 145f, 0f, 0f, 0f, 0f)
        assertTrue(engine.addMask(shape, points, 4, true) >= 0)
        val masks = FloatArray(128)
        assertTrue(engine.queryMasks(shape, masks) > 0)
        val initial = masks.copyOfRange(0, 6)
        assertTrue(engine.pinLayerMasks(shape, true))
        engine.beginCommandBatch()
        val batch = CommandBatch(engine)
        assertTrue(batch.setPosition(shape, 120f, 80f, 0f))
        assertTrue(batch.insertKeyframe(shape, TrackProperty.POSITION_X, -1, 0, 0, 120f))
        assertTrue(batch.insertKeyframe(shape, TrackProperty.POSITION_X, -1, 0, 29, 275f))
        assertEquals(3, engine.submitCommands())
        val project = File(folder, "pinned-mask.aurea")
        assertEquals(0, engine.saveProject(project.absolutePath))
        assertEquals(0, engine.loadProject(project.absolutePath))
        val count = engine.queryMasks(shape, masks)
        assertTrue(count > 0)
        assertEquals(1f, masks[count - 1], 0f)
        assertArrayEquals(initial, masks.copyOfRange(0, 6), .001f)
        export("headless-pinned-mask", 30, 320, 180, false)
        assertTrue(engine.pinLayerMasks(shape, false))
        assertTrue(engine.queryMasks(shape, masks) > 0)
        assertFalse(initial.contentEquals(masks.copyOfRange(0, 6)))
    }

    @Test fun groupedSphereKeepsVolumeAtNinetyDegreesAndChildrenCanBeReordered() {
        scene(16)
        fun capture(): ByteArray {
            val image = ByteBuffer.allocateDirect(320 * 180 * 4)
            assertEquals(image.capacity(), engine.captureFrame(320, image, IntArray(2)))
            return ByteArray(image.capacity()) { image.get(it) }
        }
        val background = capture()
        val sphere = engine.addShape3d(1, "Sphere")
        val text = engine.addText("TEXT")
        assertTrue(sphere > 0 && text > 0)
        engine.beginCommandBatch()
        val setup = CommandBatch(engine)
        assertTrue(setup.setScale(sphere, .65f, .65f, .65f))
        assertTrue(setup.setPosition(sphere, 160f, 90f, 0f))
        assertTrue(setup.setPosition(text, 50f, 22f, 0f))
        assertEquals(3, engine.submitCommands())
        val group = engine.precompose(longArrayOf(sphere, text))
        assertTrue(group > 0)
        assertTrue(engine.setLayer3D(group, true))
        fun sphereArea(frame: ByteArray): Int = (56 until 124).sumOf { y ->
            (126 until 194).count { x ->
                val offset = (y * 320 + x) * 4
                (0..2).any { c ->
                    kotlin.math.abs((frame[offset + c].toInt() and 255) - (background[offset + c].toInt() and 255)) > 10
                }
            }
        }
        val frontArea = sphereArea(capture())
        engine.beginCommandBatch()
        assertTrue(CommandBatch(engine).setRotation(group, 0f, 90f, 0f))
        assertEquals(1, engine.submitCommands())
        val edgeOn = capture()
        val edgeArea = sphereArea(edgeOn)
        assertTrue("The sphere must be visible head-on ($frontArea pixels)", frontArea > 150)
        assertTrue("Quarter turn should preserve the sphere's projected area ($frontArea to $edgeArea pixels)",
            edgeArea >= frontArea * .65)
        val rows = ByteBuffer.allocateDirect(PodLayout.LAYER_ROW_BYTES * 2).order(ByteOrder.nativeOrder())
        val names = ByteBuffer.allocateDirect(256)
        assertEquals(2, engine.queryGroupLayers(group, rows, 2, names))
        val top = rows.getLong(PodLayout.LAYER_OFF_ID)
        val bottom = rows.getLong(PodLayout.LAYER_ROW_BYTES + PodLayout.LAYER_OFF_ID)
        assertTrue(engine.setInlineEditTarget(group, bottom))
        engine.beginCommandBatch()
        assertTrue(CommandBatch(engine).reorderLayer(bottom, 1))
        assertEquals(1, engine.submitCommands())
        assertTrue(engine.setInlineEditTarget(0, 0))
        assertEquals(2, engine.queryGroupLayers(group, rows, 2, names))
        assertEquals(bottom, rows.getLong(PodLayout.LAYER_OFF_ID))
        assertEquals(top, rows.getLong(PodLayout.LAYER_ROW_BYTES + PodLayout.LAYER_OFF_ID))
        assertTrue(engine.setLayer3D(group, false))
        assertFalse(edgeOn.contentEquals(capture()))
        val project = File(folder, "rotated-group.aurea")
        assertEquals(0, engine.saveProject(project.absolutePath))
        assertEquals(0, engine.loadProject(project.absolutePath))
        assertFalse(edgeOn.contentEquals(capture()))
    }
    @Test fun cameraMotionBlurKeepsShadowsOnStationaryModels() {
        val frames = 12
        val width = 960
        val height = 540
        scene(frames, width, height)
        val models = (0 until 24).map { engine.addShape3d(it % 3, "Stationary model $it").also { id -> assertTrue(id > 0) } }
        val camera = engine.addCamera()
        val light = engine.addLight(0)
        assertTrue(camera > 0 && light > 0)
        engine.beginCommandBatch()
        val commands = CommandBatch(engine)
        models.forEachIndexed { index, model ->
            assertTrue(commands.setLayerTimeRange(model, 0, frames))
            assertTrue(commands.setScale(model, .24f, .24f, .24f))
            assertTrue(commands.setPosition(model, 185f + index % 6 * 115f, 110f + index / 6 * 100f, 0f))
        }
        assertTrue(commands.setLayerTimeRange(camera, 0, frames))
        assertTrue(commands.setLayerTimeRange(light, 0, frames))
        assertTrue(commands.insertKeyframe(camera, TrackProperty.POSITION_X, -1, 0, 0, 375f))
        assertTrue(commands.insertKeyframe(camera, TrackProperty.POSITION_X, -1, 0, frames - 1, 585f))
        assertTrue(engine.submitCommands() > 0)
        models.forEach { assertTrue(engine.setMotionBlur(it, true)) }
        assertTrue(engine.setMotionBlurSettings(MotionBlurSettings(true, 270f, -135f, 16, 16, 4)))
        export("stationary-models-camera-motion-blur", frames, width, height, false)
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
    @Test fun denseGltfExportsAllFramesAtRequestedSizeDuringMemoryWarning() {
        val frames = 36
        val width = 960
        val height = 540
        val columns = 48
        val rows = 25
        val objects = columns * rows
        scene(frames, width, height)
        val vertices = ByteBuffer.allocate(9 * Float.SIZE_BYTES).order(ByteOrder.LITTLE_ENDIAN)
        for (value in floatArrayOf(-.18f, -.14f, 0f, .18f, -.14f, 0f, 0f, .19f, 0f)) vertices.putFloat(value)
        val data = Base64.encodeToString(vertices.array(), Base64.NO_WRAP)
        val nodes = (0 until objects).joinToString(",") { index ->
            val x = (index % columns - (columns - 1) * .5f) * .43f
            val y = (index / columns - (rows - 1) * .5f) * .43f
            """{"mesh":0,"translation":[$x,$y,0]}"""
        }
        val source = File(folder, "dense-grid.gltf").apply {
            writeText("""
                {"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[${(0 until objects).joinToString(",")}]}],
                "nodes":[$nodes],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],
                "materials":[{"doubleSided":true,"pbrMetallicRoughness":{"baseColorFactor":[0.9,0.7,0.25,1],"metallicFactor":0}}],
                "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-0.18,-0.14,0],"max":[0.18,0.19,0]}],
                "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36,"target":34962}],
                "buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,$data"}]}
            """.trimIndent())
        }
        val detail = arrayOfNulls<String>(1)
        val id = engine.importModel(source.absolutePath, "Dense 3D scene ($objects objects)", detail)
        assertTrue("Dense glTF import: ${detail[0]}", id > 0)
        engine.beginCommandBatch()
        val commands = CommandBatch(engine)
        assertTrue(commands.setLayerTimeRange(id, 0, frames))
        assertTrue(commands.setPosition(id, width * .5f, height * .5f, 0f))
        assertTrue(commands.insertKeyframe(id, TrackProperty.ROTATION_Y, -1, 0, 0, 0f))
        assertTrue(commands.insertKeyframe(id, TrackProperty.ROTATION_Y, -1, 0, frames - 1, 38f))
        assertEquals(4, engine.submitCommands())
        assertTrue(engine.setMotionBlur(id, true))
        assertTrue(engine.setMotionBlurSettings(MotionBlurSettings(true, 270f, -135f, 8, 16, 4)))
        export("headless-dense-$objects", frames, width, height, false, trimDuringExport = true)
    }

    @Test fun newSolidShapesKeepTheirTypesAndPartsAfterReopen() {
        scene(30)
        val counts = listOf(3, 3, 3, 3, 3, 4, 2, 4, 3)
        val layers = (10 until Shape3DCatalog.CLOUD).map { kind ->
            engine.addShape3d(kind, "Solid $kind").also { assertTrue("Shape $kind", it > 0) }
        }
        val project = File(folder, "new-solids.aurea")
        assertEquals(0, engine.saveProject(project.absolutePath))
        assertEquals(0, engine.loadProject(project.absolutePath))
        for ((index, layer) in layers.withIndex()) {
            val recipe = FloatArray(Shape3DInfo.FIELDS)
            assertEquals(counts[index], engine.queryShape3d(layer, recipe))
            assertEquals(10 + index, recipe[0].toInt())
        }
    }

    @Test fun volumetricCloudRespondsToDensityAndLightingAndExports() {
        val width = 320
        val height = 180
        val frames = 12
        scene(frames, width, height)
        fun capture(): IntArray {
            val raw = ByteBuffer.allocateDirect(width * height * 4)
            val dims = IntArray(2)
            assertEquals(raw.capacity(), engine.captureFrame(width, raw, dims))
            assertArrayEquals(intArrayOf(width, height), dims)
            return IntArray(width * height) { i ->
                val offset = i * 4
                fun channel(c: Int) = raw.get(offset + c).toInt() and 0xFF
                (channel(3) shl 24) or (channel(0) shl 16) or (channel(1) shl 8) or channel(2)
            }
        }
        fun difference(a: IntArray, b: IntArray): Int = a.indices.count { i ->
            val r = kotlin.math.abs(((a[i] shr 16) and 255) - ((b[i] shr 16) and 255))
            val g = kotlin.math.abs(((a[i] shr 8) and 255) - ((b[i] shr 8) and 255))
            val blue = kotlin.math.abs((a[i] and 255) - (b[i] and 255))
            r + g + blue > 12
        }
        val blank = capture()
        val id = engine.addShape3d(Shape3DCatalog.CLOUD, "3D cloud")
        assertTrue(id > 0)
        val cloud = capture()
        assertTrue("Cloud must cover pixels outside the blank composition", difference(blank, cloud) > 450)
        assertTrue("Cloud must retain variation across the density field", cloud.toSet().size > 40)
        Bitmap.createBitmap(cloud, width, height, Bitmap.Config.ARGB_8888).useForCloudCapture()
        assertTrue(engine.setMaterialParam(id, 0, 4, 0f))
        assertTrue("Zero density must remove the volume", difference(blank, capture()) < 20)
        assertTrue(engine.setMaterialParam(id, 0, 4, .57f))
        val light = engine.addLight(0)
        assertTrue(light > 0)
        val lit = capture()
        engine.beginCommandBatch()
        assertTrue(CommandBatch(engine).setRotation(light, 25f, 150f, 0f))
        assertEquals(1, engine.submitCommands())
        val turned = capture()
        assertTrue("Changing the scene light must change the cloud", difference(lit, turned) > 300)
        engine.beginCommandBatch()
        val commands = CommandBatch(engine)
        assertTrue(commands.setLayerTimeRange(id, 0, frames))
        assertTrue(commands.insertKeyframe(id, TrackProperty.POSITION_X, -1, 0, 0, width * .39f))
        assertTrue(commands.insertKeyframe(id, TrackProperty.POSITION_X, -1, 0, frames - 1, width * .61f))
        assertEquals(3, engine.submitCommands())
        val project = File(folder, "reactive-cloud.aurea")
        assertEquals(0, engine.saveProject(project.absolutePath))
        assertEquals(0, engine.loadProject(project.absolutePath))
        export("headless-volume-cloud", frames, width, height, false)
    }

    private fun Bitmap.useForCloudCapture() {
        try {
            File(folder, "aurea-3d-cloud.png").outputStream().use { compress(Bitmap.CompressFormat.PNG, 100, it) }
        } finally {
            recycle()
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
    @Test fun nestedAudioFollowsParentSpeedAndTimeRemapWithoutPreview() {
        val composition = scene(60)
        val shape = animatedShape(60, 320, 180)
        val audio = engine.importAudio(tone(2).absolutePath, "Owned nested stereo clock")
        assertTrue(audio > 0)
        val nested = engine.precompose(longArrayOf(shape, audio)); assertTrue(nested > 0)
        engine.beginCommandBatch(); var commands = CommandBatch(engine)
        assertTrue(commands.setCompositionDuration(composition, 30))
        assertTrue(commands.setLayerSpeed(nested, 2f))
        assertTrue(commands.setLayerTimeRange(nested, 0, 30))
        assertEquals(3, engine.submitCommands())
        export("headless-nested-speed2x", 30, 320, 180, true)
        engine.beginCommandBatch(); commands = CommandBatch(engine)
        assertTrue(commands.setLayerSpeed(nested, 1f)); assertTrue(commands.setLayerTimeRange(nested, 0, 30))
        assertEquals(2, engine.submitCommands())
        assertEquals(0, engine.saveProject(File(folder, "owned-nested-remap-prepared.aurea").absolutePath))
        assertTrue(engine.setTimeRemap(nested, true))
        assertTrue(engine.setTimeRemapValue(nested, 0, 0f))
        assertTrue(engine.setTimeRemapValue(nested, 30, 60f))
        engine.beginCommandBatch(); commands = CommandBatch(engine)
        assertTrue(commands.setCompositionDuration(composition, 30))
        assertEquals(1, engine.submitCommands())
        assertEquals(0, engine.saveProject(File(folder, "owned-nested-remap-frozen.aurea").absolutePath))
        val clock = DoubleArray(8)
        assertEquals(composition, engine.queryComposition(clock)); assertEquals(30, clock[3].toInt())
        export("headless-nested-remap2x", 30, 320, 180, true)
        File(folder, "nested-audio-expectation.txt").writeText(
            "fps=30 frames=30 sourceToneHz=440,880 decodedToneHz=880,1760 sourcePhaseRadians=0,0\n")
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
