package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.Build
import android.os.SystemClock
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.diagnostics.ExportDiagnosticReport
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.MotionBlurSettings
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ExportOptions
import com.aurea.aurea.state.ExportPhase
import com.aurea.aurea.ui.theme.AureaTheme
import java.io.File
import java.security.MessageDigest
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

/** Production export sessions, own inputs, independent decode; no OEM claim. */
class AstraExportMatrixDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun exportMatrixPreservesFramesAndSources() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        check(context.packageName.endsWith(".uitest"))
        val artifacts = File(context.getExternalFilesDir(null), "astra-export/${SystemClock.elapsedRealtime()}")
        check(artifacts.mkdirs())
        val report = File(artifacts, "matrix.txt")
        report.writeText("manufacturer=${Build.MANUFACTURER} model=${Build.MODEL} " +
            "hardware=${Build.HARDWARE} api=${Build.VERSION.SDK_INT} abi=${Build.SUPPORTED_ABIS.joinToString()}\n")
        val codecs = MediaCodecList(MediaCodecList.ALL_CODECS).codecInfos.filter { it.isEncoder }
        val inventory = File(artifacts, "codec-capabilities.txt")
        codecs.forEach { codec ->
            codec.supportedTypes.filter { it == "video/avc" || it == "video/hevc" }.forEach { mime ->
                val caps = codec.getCapabilitiesForType(mime)
                val modes = (0..2).filter { caps.encoderCapabilities.isBitrateModeSupported(it) }
                inventory.appendText("name=${codec.name} mime=$mime hardware=${codec.isHardwareAccelerated} " +
                    "software=${codec.isSoftwareOnly} alias=${codec.isAlias} colors=${caps.colorFormats.joinToString()} " +
                    "widthAlign=${caps.videoCapabilities.widthAlignment} heightAlign=${caps.videoCapabilities.heightAlignment} " +
                    "rateModes=$modes profiles=${caps.profileLevels.joinToString { "${it.profile}:${it.level}" }}\n")
                for (color in listOf(MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420SemiPlanar,
                    MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Planar)) {
                    val format = MediaFormat.createVideoFormat(mime, 640, 368)
                    format.setInteger(MediaFormat.KEY_COLOR_FORMAT, color)
                    format.setInteger(MediaFormat.KEY_FRAME_RATE, 30)
                    format.setInteger(MediaFormat.KEY_BIT_RATE, 2_000_000)
                    format.setInteger(MediaFormat.KEY_BITRATE_MODE, MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR)
                    inventory.appendText("  padded640x368 color=$color formatSupported=${caps.isFormatSupported(format)}\n")
                }
            }
        }
        val source = File(context.filesDir, "astra-own-image-${SystemClock.elapsedRealtime()}.png")
        val bitmap = Bitmap.createBitmap(128, 128, Bitmap.Config.ARGB_8888)
        for (y in 0 until 128) for (x in 0 until 128) bitmap.setPixel(x, y, Color.rgb(60+x, 35+y, 170))
        source.outputStream().use { check(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        bitmap.recycle()
        fun digest(file: File) = MessageDigest.getInstance("SHA-256").digest(file.readBytes())
        val sourceDigest = digest(source)
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        var ownVideo: Uri? = null
        val motionSource = File(context.filesDir, "astra-motion-with-tone.mp4")
        instrumentation.context.assets.open("video-with-tone.mp4").use { input -> motionSource.outputStream().use { input.copyTo(it) } }
        val motionDigest = digest(motionSource)
        for (case in listOf("simple", "video", "multiple", "effects", "motion-blur", "3d", "nested-movement", "adjustment-movement", "optical-flow", "serial-color", "legacy44")) {
            val frameCount = when { case == "optical-flow" -> 120; case.endsWith("-movement") -> 100; else -> 12 }
            val title = "ASTRA $case ${SystemClock.elapsedRealtime()}"
            if (case == "legacy44") {
                val legacy = File(context.filesDir, "astra-own-legacy44.aurea")
                instrumentation.context.assets.open("astra/legacy44.aurea").use { input -> legacy.outputStream().use { input.copyTo(it) } }
                compose.runOnIdle { store.openProject(legacy.absolutePath) }
                compose.waitUntil(20000) { !store.projectOperationBusy && store.project.path == legacy.absolutePath && store.layers.size == 1 }
                compose.runOnIdle { assertTrue("Native loader must report older-format migration", store.engineForStress.loadNotice() and 4 != 0) }
            } else {
                compose.runOnIdle { store.newProject(640, 360, 30f, title) }
                compose.waitUntil(20000) { !store.projectOperationBusy && store.project.title == title }
            }
            if (case == "legacy44") {
                assertEquals(12, store.project.durationFrames)
            } else if (case == "video") {
                compose.runOnIdle { store.importVideo(checkNotNull(ownVideo)) }
            } else if (case == "optical-flow") {
                compose.runOnIdle { store.importVideo(Uri.fromFile(motionSource)) }
            } else if (case == "3d") {
                compose.runOnIdle { assertTrue(store.addShape3D(0) > 0) }
            } else {
                compose.runOnIdle { store.importImage(Uri.fromFile(source)) }
            }
            compose.waitUntil(20000) { store.layers.isNotEmpty() && store.busyMessage == null }
            val primary = store.layers.first().id
            if (case == "optical-flow") {
                val flow = effectTypeId("aurea.time.flow_retime")
                compose.runOnIdle { store.select(primary); store.addEffect(flow, listOf(primary)) }
                compose.waitUntil(10000) { store.effects.any { it.typeId == flow } }
                val effect = store.effects.single { it.typeId == flow }.effectId
                compose.runOnIdle {
                    val engine = store.engineForStress
                    engine.beginCommandBatch()
                    CommandBatch(engine).setEffectParam(primary, effect, 0, 37.5f)
                    assertTrue(engine.submitCommands() > 0)
                }
            }
            if (case == "serial-color") {
                compose.runOnIdle {
                    store.select(primary)
                    val first = checkNotNull(store.colorGradeFor(2))
                    val second = checkNotNull(store.colorGradeFor(3))
                    assertNotEquals(first, second)
                    val engine = store.engineForStress
                    engine.beginCommandBatch()
                    val commands = CommandBatch(engine)
                    commands.setEffectParam(primary, first, 3, .5f)
                    commands.setEffectParam(primary, second, 79, 2f)
                    commands.setEffectParam(primary, second, 4, 25f)
                    assertTrue(engine.submitCommands() > 0)
                }
            }
            if (case == "multiple") {
                compose.runOnIdle { repeat(5) { store.addShape(1) } }
                compose.waitUntil(10000) { store.layers.size == 6 }
            }
            if (case == "effects") {
                val tile = effectTypeId("aurea.stylize.motion_tile")
                compose.runOnIdle { store.select(primary); store.addEffect(tile, listOf(primary)) }
                compose.waitUntil(10000) { store.effects.any { it.typeId == tile } }
                val effect = store.effects.single { it.typeId == tile }.effectId
                compose.runOnIdle {
                    val engine = store.engineForStress
                    engine.beginCommandBatch()
                    CommandBatch(engine).setEffectParam(primary, effect, 5, 1f)
                    assertTrue(engine.submitCommands() > 0)
                }
                val blur = effectTypeId("aurea.blur.gaussian")
                val glow = effectTypeId("aurea.light.deep_glow")
                compose.runOnIdle {
                    store.addEffect(blur, listOf(primary))
                    store.addEffect(glow, listOf(primary))
                }
                compose.waitUntil(10000) { listOf(blur, tile, glow).all { type -> store.effects.any { it.typeId == type } } }
                val glowId = store.effects.single { it.typeId == glow }.effectId
                compose.runOnIdle {
                    val engine = store.engineForStress
                    engine.beginCommandBatch()
                    CommandBatch(engine).setEffectParam(primary, glowId, 0, 0f)
                    assertTrue(engine.submitCommands() > 0)
                }
            }
            if (case.endsWith("-movement")) {
                compose.runOnIdle {
                    if (case == "nested-movement") store.precompose(listOf(primary))
                    else store.addAdjustmentLayer()
                }
                compose.waitUntil(10000) { store.primary != primary && store.detail != null }
                val target = checkNotNull(store.primary)
                val tile = effectTypeId("aurea.stylize.motion_tile")
                val oscillate = effectTypeId("aurea.motion.oscillate")
                compose.runOnIdle {
                    store.addEffect(tile, listOf(target))
                    store.addEffect(oscillate, listOf(target))
                }
                compose.waitUntil(10000) { store.effects.any { it.typeId == tile } && store.effects.any { it.typeId == oscillate } }
            }
            compose.runOnIdle {
                val engine = store.engineForStress
                if (case != "legacy44") engine.beginCommandBatch()
                val commands = CommandBatch(engine)
                if (case != "legacy44") store.layers.forEachIndexed { index, layer ->
                    commands.setLayerTimeRange(layer.id, 0, frameCount)
                    if (case != "3d") commands.setPosition(layer.id, 220f + index * 35f, 180f, 0f)
                    if (case == "multiple" && layer.id != primary) commands.setShapeFill(layer.id, .2f, .7f, .3f, 1f)
                }
                if (case == "motion-blur") {
                    commands.insertKeyframe(primary, TrackProperty.POSITION_X, -1, 0, 0, 180f)
                    commands.insertKeyframe(primary, TrackProperty.POSITION_X, -1, 0, 11, 440f)
                }
                if (case != "legacy44") assertTrue(engine.submitCommands() > 0)
                if (case == "motion-blur") {
                    assertTrue(engine.setMotionBlur(primary, true))
                    assertTrue(engine.setMotionBlurSettings(MotionBlurSettings(true, 180f, -90f, 4, 8, 2)))
                    assertTrue(checkNotNull(engine.queryMotionBlurSettings()).enabled)
                }
                if (case != "legacy44") store.setCompositionDuration(frameCount)
                store.pause(); store.seek(0)
            }
            compose.waitUntil(15000) { store.project.durationFrames == frameCount && store.layers.all { it.endFrame == frameCount } }
            if (case == "3d") compose.runOnIdle {
                store.select(primary)
                assertTrue((checkNotNull(store.detail).flags and PodLayout.FLAG_THREE_D) != 0)
            }
            val layerIds = store.layers.map { it.id }.toSet()
            if (case == "nested-movement") {
                compose.onNodeWithTag("layer.menu").performClick()
                compose.onNodeWithTag("layer.menu.group.prerender").performScrollTo().performClick()
                compose.waitUntil(90000) {
                    store.preRenderProgress.complete ||
                        (!store.preRenderPreparing && !store.preRenderProgress.running && store.preRenderProgress.error != 0)
                }
                assertEquals(0, store.preRenderProgress.error)
                assertTrue(store.preRenderProgress.complete)
                assertEquals(layerIds, store.layers.map { it.id }.toSet())
            }
            val started = SystemClock.elapsedRealtime()
            compose.runOnIdle {
                store.exporter.start(title, 640, 360, 30.0,
                    ExportOptions(shortSide = 360, fps = 30.0, customMbps = 2, hevc = false, trimToContent = false))
            }
            val activeSamples = mutableListOf<JSONObject>()
            var sampledAt = 0L
            compose.waitUntil(180000) {
                if (store.exporter.busy && SystemClock.elapsedRealtime()-sampledAt >= 20L) {
                    sampledAt = SystemClock.elapsedRealtime()
                    val sample = JSONObject(store.engineForStress.exportDiagnostics())
                    if (sample.has("sessionToken") && !sample.optBoolean("busy") && activeSamples.size < 100) activeSamples += sample
                }
                !store.exporter.busy
            }
            val state = store.exporter.state
            val diagnostic = JSONObject(store.engineForStress.exportDiagnostics())
            File(artifacts, "$case-diagnostics.json").writeText(diagnostic.toString(2))
            val token = diagnostic.getString("sessionToken")
            val active = checkNotNull(activeSamples.firstOrNull { it.optString("sessionToken") == token && it.optString("sessionStage") != "complete" })
            File(artifacts, "$case-active-diagnostics.json").writeText(active.toString(2))
            assertTrue(diagnostic.getString("encoder").isNotBlank())
            assertTrue(diagnostic.getString("backend").isNotBlank())
            assertEquals("complete", diagnostic.getString("sessionStage"))
            assertEquals(0, diagnostic.getInt("result"))
            val persistedText = checkNotNull(ExportDiagnosticReport.last(context)) { "Production Exporter must persist export diagnostics" }
            val persisted = JSONObject(persistedText)
            assertTrue(persistedText.length <= ExportDiagnosticReport.LIMIT)
            assertEquals(Build.MANUFACTURER, persisted.getJSONObject("device").getString("manufacturer"))
            assertEquals(Build.MODEL, persisted.getJSONObject("device").getString("model"))
            val attempts = persisted.getJSONArray("attempts")
            assertEquals(token, attempts.getJSONObject(attempts.length()-1).getString("sessionToken"))
            assertEquals(frameCount.toLong(), attempts.getJSONObject(attempts.length()-1).getLong("submitted"))
            File(artifacts, "$case-persisted-diagnostics.json").writeText(persisted.toString(2))
            report.appendText("case=$case phase=${state.phase} frames=${state.framesDone}/${state.framesTotal} " +
                "elapsedMs=${SystemClock.elapsedRealtime()-started} message=${state.message}\n")
            assertEquals(state.message, ExportPhase.Done, state.phase)
            assertEquals(frameCount, state.framesDone)
            assertEquals(frameCount, state.framesTotal)
            assertEquals("Only user frames belong to the session diagnostics", frameCount.toLong(), diagnostic.getLong("submitted"))
            assertEquals(frameCount.toLong(), diagnostic.getLong("videoPackets"))
            assertTrue(diagnostic.getLong("firstInputMs") >= 0L)
            assertTrue(diagnostic.getLong("firstPacketMs") >= diagnostic.getLong("firstInputMs"))
            assertTrue("Negotiated color must stay unknown without a provider observation", diagnostic.isNull("negotiatedColor"))
            assertEquals(layerIds, store.layers.map { it.id }.toSet())
            assertArrayEquals(sourceDigest, digest(source))
            assertArrayEquals(motionDigest, digest(motionSource))
            val uri = checkNotNull(state.outputUri)
            if (case == "simple") ownVideo = uri
            val output = File(artifacts, "$case.mp4")
            context.contentResolver.openInputStream(uri)!!.use { input -> output.outputStream().use { input.copyTo(it) } }
            val extractor = MediaExtractor()
            try {
                extractor.setDataSource(output.absolutePath)
                val video = (0 until extractor.trackCount).first { extractor.getTrackFormat(it).getString(MediaFormat.KEY_MIME) == "video/avc" }
                extractor.selectTrack(video)
                val format = extractor.getTrackFormat(video)
                assertEquals(640, format.getInteger(MediaFormat.KEY_WIDTH))
                assertEquals(360, format.getInteger(MediaFormat.KEY_HEIGHT))
                assertTrue("Video track duration must preserve the exact frame range",
                    kotlin.math.abs(format.getLong(MediaFormat.KEY_DURATION) - frameCount * 1_000_000L / 30L) <= 1L)
                var packets = 0
                var last = -1L
                while (extractor.sampleTime >= 0) {
                    assertTrue(extractor.sampleTime > last)
                    last = extractor.sampleTime
                    packets++
                    if (!extractor.advance()) break
                }
                assertEquals("Every input picture must have a packet", frameCount, packets)
                report.appendText("case=$case packets=$packets lastPtsUs=$last format=$format\n")
            } finally { extractor.release() }
            val decoder = MediaMetadataRetriever()
            try {
                decoder.setDataSource(output.absolutePath)
                val duration = checkNotNull(decoder.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)).toLong()
                val expectedDuration = frameCount * 1000L / 30L
                if (case == "optical-flow") {
                    // Android's retriever includes this fixture's 1024-sample
                    // AAC priming interval. The video track above is exact.
                    // Independent ffprobe inspection also checks the MP4 edit
                    // list and the published audio duration.
                    assertTrue("AAC metadata duration must stay within one priming frame",
                        duration in expectedDuration..(expectedDuration + 22L))
                } else assertEquals(expectedDuration, duration)
                report.appendText("case=$case retrieverDurationMs=$duration expectedDurationMs=$expectedDuration\n")
                for (time in listOf(0L, (frameCount - 1L) * 1000000L / 30L)) {
                    val decoded = checkNotNull(decoder.getFrameAtTime(time, MediaMetadataRetriever.OPTION_CLOSEST))
                    try {
                        assertEquals(640, decoded.width); assertEquals(360, decoded.height)
                        var lit = 0
                        for (y in 0 until 360 step 8) for (x in 0 until 640 step 8) {
                            val pixel = decoded.getPixel(x,y)
                            if (maxOf(Color.red(pixel), Color.green(pixel), Color.blue(pixel)) > 35) lit++
                        }
                        assertTrue("Decoded own content must be visible for $case, lit=$lit", lit > 5)
                        report.appendText("case=$case decodedUs=$time lit=$lit\n")
                    } finally { decoded.recycle() }
                }
            } finally { decoder.release() }
            assertEquals("Completed elapsed time must stay frozen", diagnostic.getLong("elapsedMs"),
                JSONObject(store.engineForStress.exportDiagnostics()).getLong("elapsedMs"))
        }
        report.appendText("result=PASS realOEM=false\n")
    }
}
