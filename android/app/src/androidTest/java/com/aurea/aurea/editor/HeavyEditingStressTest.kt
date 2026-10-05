package com.aurea.aurea.editor

import android.app.Application
import android.media.MediaMetadataRetriever
import android.os.Debug
import android.os.SystemClock
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Opt-in, isolated app only. Builds a real project through production commands,
 * then exercises live preview, memory pressure, reload and MediaCodec export. */
class HeavyEditingStressTest {
    @get:Rule val compose = createComposeRule()

    @Test fun sustainedFullHdEditingRecoversAfterReloadAndSurfaceRecreation() {
        HeavyEditingSoakHarness(compose).run()
    }

    @Test fun denseEditSurvivesPreviewTrimReloadAndExport() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        check(context.packageName.endsWith(".uitest"))
        org.junit.Assume.assumeTrue(InstrumentationRegistry.getArguments().getString("aureaStress") == "true")
        val folder = File(context.filesDir, "stress-2126").apply { mkdirs() }
        val report = File(folder, "progress.txt")
        fun note(text: String) {
            val mem = Debug.MemoryInfo().also { Debug.getMemoryInfo(it) }
            report.appendText("$text pssKB=${mem.totalPss} nativeBytes=${Debug.getNativeHeapAllocatedSize()}\n")
        }
        report.writeText("AUREA heavy editing stress\n")
        val video = File(folder, "preview-vfr.mp4")
        instrumentation.context.assets.open("preview-vfr.mp4").use { input -> video.outputStream().use { input.copyTo(it) } }
        val fbx = File(folder, "animated-character.fbx")
        instrumentation.context.assets.open(fbx.name).use { input -> fbx.outputStream().use { input.copyTo(it) } }
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(1920, 1080, 30f, "Heavy edit 96 layers") }
        compose.waitUntil(15000) { store.project.title == "Heavy edit 96 layers" }
        val engine = store.engineForStress
        val ids = mutableListOf<Long>()
        for (i in 0 until 96) {
            val kind = i % 12
            var id = 0L
            val fbxLayer = if (i == 1) {
                val detail = arrayOfNulls<String>(1)
                engine.importModel(fbx.absolutePath, "Animated FBX", detail).also {
                    assertTrue("FBX import: ${detail[0]}", it > 0)
                    assertTrue(engine.modelMissingTextures(it).isEmpty())
                    note("animated-FBX-import-passed")
                }
            } else 0L
            compose.runOnIdle {
                id = when (kind) {
                    0 -> engine.importVideo(video.absolutePath, "Video $i")
                    1 -> if (fbxLayer > 0) fbxLayer else engine.addShape3d(i % 3, "3D $i")
                    2 -> engine.addText("AUREA $i")
                    else -> engine.addShape(i % 5)
                }
                assertTrue("Could not create layer $i", id > 0)
                ids += id
                engine.beginCommandBatch()
                val cmd = CommandBatch(engine)
                cmd.setLayerTimeRange(id, 0, 90)
                cmd.setScale(id, .3f, .3f, .3f)
                cmd.setPosition(id, 80f + i % 12 * 155f, 90f + i / 12 * 125f, 0f)
                if (kind == 2) cmd.setTextSize(id, 70f)
                for (frame in listOf(0, 30, 60, 89)) {
                    cmd.insertKeyframe(id, TrackProperty.POSITION_X, -1, 0, frame,
                        80f + i % 12 * 155f + if (frame in 30..60) 100f else 0f)
                    cmd.insertKeyframe(id, TrackProperty.ROTATION_Z, -1, 0, frame, frame * 3f)
                }
                if (kind != 1) {
                    cmd.addEffect(id, effectTypeId("aurea.blur.gaussian"))
                    cmd.addEffect(id, effectTypeId("aurea.light.glow"))
                }
                assertTrue(engine.submitCommands() > 0)
                assertTrue(engine.setMotionBlur(id, true))
                if (kind == 0) assertTrue(engine.setVectorBlur(id, 1f))
            }
            if (kind != 1) {
                val rows = ByteBuffer.allocateDirect(64).order(ByteOrder.nativeOrder())
                val blob = ByteBuffer.allocateDirect(1024)
                compose.waitUntil(15000) { engine.queryLayerEffects(id, rows, 2, blob) == 2 }
                compose.runOnIdle {
                    engine.beginCommandBatch()
                    val cmd = CommandBatch(engine)
                    cmd.setEffectParam(id, rows.getInt(0), 0, 3f)
                    cmd.setEffectParam(id, rows.getInt(32), 0, 25f)
                    assertEquals(2, engine.submitCommands())
                }
            }
            if ((i + 1) % 24 == 0) note("created=${i + 1}")
        }
        compose.runOnIdle {
            store.setCompositionDuration(90)
            engine.setCompositionMotionBlur(true)
            engine.setShutterAngle(360f)
            val group = engine.precompose(ids.takeLast(8).toLongArray())
            assertTrue(group > 0)
            assertTrue(engine.openPrecomp(group))
            assertTrue(engine.closePrecomp())
            store.clearSelection()
        }
        val layerRows = ByteBuffer.allocateDirect(PodLayout.LAYER_ROW_BYTES * 128).order(ByteOrder.nativeOrder())
        val names = ByteBuffer.allocateDirect(16384)
        val expected = engine.queryLayers(layerRows, 128, names)
        assertEquals(89, expected)
        note("ready rootLayers=$expected totalLayers=97 effects=176 keys=768")
        compose.runOnIdle { store.seek(0); store.play() }
        compose.waitUntil(60000) { store.playhead >= 45 }
        compose.runOnIdle { store.pause() }
        compose.waitUntil(10000) { !store.playing }
        for (frame in listOf(89, 0, 45, 15, 75, 30)) {
            compose.runOnIdle { store.seek(frame) }
            compose.waitUntil(15000) { store.playhead == frame }
        }
        note("playback-and-scrub-passed")
        val project = File(folder, "heavy-edit.aurea")
        compose.runOnIdle { assertEquals(0, engine.saveProject(project.absolutePath)) }
        val reclaimed = engine.trimMemory(80)
        note("trimMemory reclaimed=$reclaimed")
        compose.runOnIdle { assertEquals(0, engine.loadProject(project.absolutePath)) }
        assertEquals(expected, engine.queryLayers(layerRows, 128, names))
        note("reopen-passed")
        for (height in listOf(720, 1080)) {
            val progress = ExportProgress()
            val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
            val output = File(folder, "heavy-edit-${height}p.mp4")
            val began = SystemClock.elapsedRealtime()
            compose.runOnIdle { assertEquals(0, engine.startExport(output.absolutePath, height, 30.0, 0, 8)) }
            var lastFrame = -1
            var moved = began
            try {
                compose.waitUntil(1800000) {
                    engine.exportProgress(buffer)
                    progress.readFrom(buffer)
                    if (progress.framesDone != lastFrame) {
                        lastFrame = progress.framesDone
                        moved = SystemClock.elapsedRealtime()
                        if (lastFrame % 15 == 0) note("export=${height}p frame=$lastFrame/${progress.framesTotal}")
                    }
                    assertTrue("No export progress for 150s at $lastFrame", SystemClock.elapsedRealtime() - moved < 150000)
                    progress.finished
                }
                assertEquals(progress.message, 0, progress.result)
                assertEquals(90, progress.framesDone)
                assertFalse("Approximate video frames", progress.frameFallback)
                val media = MediaMetadataRetriever()
                try {
                    media.setDataSource(output.absolutePath)
                    assertEquals(height.toString(), media.extractMetadata(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT))
                    for (time in listOf(0L, 1_500_000L, 2_900_000L)) {
                        val frame = media.getFrameAtTime(time, MediaMetadataRetriever.OPTION_CLOSEST)
                        assertNotNull(frame)
                        if (time == 1_500_000L && frame != null) File(folder, "heavy-edit-${height}p.png").outputStream().use {
                            frame.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it)
                        }
                        frame?.recycle()
                    }
                } finally { media.release() }
                note("export=${height}p passed elapsedMs=${SystemClock.elapsedRealtime() - began} bytes=${output.length()}")
            } finally { if (!progress.finished) engine.cancelExport() }
        }
        note("ALL PASSED")
    }
}
