package com.aurea.aurea.editor

import android.app.Application
import android.os.Debug
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.engine.CommandBatch
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

/** Diagnostic A/B only: never substitutes for the original 96-layer acceptance
 * battery. Keep its layer count, animation, effect slots and shutter, changing
 * one producer at a time to identify a GPU/interop hang. No exports here. */
class HeavyGpuIsolationDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun replaceVideoProducersWithShapes() = runScene("no-video")
    @Test fun replace3dProducersWithShapes() = runScene("no-3d")
    @Test fun keepAllProducersWithoutOpticalFlow() = runScene("no-vector")
    @Test fun keepAllProducersWithout3dMotionBlur() = runScene("no-3d-mb")
    @Test fun replaceOnlyAnimatedFbxWithPrimitive() = runScene("no-fbx")
    @Test fun keep3dMotionBlurAtSingleSampleQuality() = runScene("scene-low")
    @Test fun keep3dMotionBlurWithoutShadowCasting() = runScene("no-shadow")
    @Test fun keepOnlyOnePrimitive3dWithMotionBlur() = runScene("one-3d")
    @Test fun keepEightMotionBlurredObjectsInOne3dGroup() = runScene("one-3d-group")

    private fun runScene(mode: String) {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        check(context.packageName == "com.aurea.aurea.uitest")
        org.junit.Assume.assumeTrue(InstrumentationRegistry.getArguments().getString("aureaStress") == "true")
        val folder = File(context.filesDir, "stress-isolation-$mode").apply { mkdirs() }
        val report = File(folder, "progress.txt")
        fun note(text: String) {
            val mem = Debug.MemoryInfo().also { Debug.getMemoryInfo(it) }
            report.appendText("$text pssKB=${mem.totalPss} nativeBytes=${Debug.getNativeHeapAllocatedSize()}\n")
        }
        report.writeText("DIAGNOSTIC ONLY mode=$mode; original acceptance remains required\n")
        if (mode == "scene-low") note("Scene Low diagnostic: MSAA 1 + FXAA; IBL, bloom and other tier settings also differ")
        val video = File(folder, "preview-vfr.mp4")
        instrumentation.context.assets.open(video.name).use { input -> video.outputStream().use { input.copyTo(it) } }
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
        val title = "Diagnostic $mode 96 layers"
        compose.runOnIdle { store.newProject(1920, 1080, 30f, title) }
        compose.waitUntil(15000) { store.project.title == title }
        val engine = store.engineForStress
        val ids = mutableListOf<Long>()
        for (i in 0 until 96) {
            val kind = i % 12
            var id = 0L
            val fbxLayer = if (i == 1 && mode != "no-3d" && mode != "no-fbx" && mode != "one-3d") {
                val detail = arrayOfNulls<String>(1)
                engine.importModel(fbx.absolutePath, "Animated FBX", detail).also {
                    assertTrue("FBX import: ${detail[0]}", it > 0)
                    assertTrue(engine.modelMissingTextures(it).isEmpty())
                }
            } else 0L
            compose.runOnIdle {
                id = when (kind) {
                    0 -> if (mode == "no-video") engine.addShape(0) else engine.importVideo(video.absolutePath, "Video $i")
                    1 -> if (mode == "no-3d" || (mode == "one-3d" && i != 1)) engine.addShape(1)
                        else if (fbxLayer > 0) fbxLayer else engine.addShape3d(i % 3, "3D $i")
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
                assertTrue(engine.setMotionBlur(id, kind != 1 || mode != "no-3d-mb"))
                if (kind == 1 && mode == "no-shadow") assertTrue(engine.setModelShadows(id, false, true))
                if (kind == 0 && mode != "no-vector" && mode != "no-video") assertTrue(engine.setVectorBlur(id, 1f))
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
        if (mode == "one-3d-group") {
            compose.runOnIdle {
                engine.beginCommandBatch()
                val cmd = CommandBatch(engine)
                ids.filterIndexed { index, _ -> index % 12 == 1 }.forEachIndexed { index, id -> cmd.reorderLayer(id, index) }
                assertEquals(8, engine.submitCommands())
            }
            // Opening a precomp changes the command destination. Confirm the
            // queued reorders reached the root before navigating into it.
            val objects = ids.filterIndexed { index, _ -> index % 12 == 1 }.toSet()
            val checkRows = ByteBuffer.allocateDirect(PodLayout.LAYER_ROW_BYTES * 128).order(ByteOrder.nativeOrder())
            val checkNames = ByteBuffer.allocateDirect(16384)
            compose.waitUntil(15000) {
                val count = engine.queryLayers(checkRows, 128, checkNames)
                val positions = (0 until count).filter { checkRows.getLong(it * PodLayout.LAYER_ROW_BYTES) in objects }
                positions.size == 8 && positions.last() - positions.first() == 7
            }
        }
        compose.runOnIdle {
            store.setCompositionDuration(90)
            if (mode == "scene-low") assertTrue(engine.setSceneSetting(2, 1f))
            engine.setCompositionMotionBlur(true)
            engine.setShutterAngle(360f)
            val group = engine.precompose(ids.takeLast(8).toLongArray())
            assertTrue(group > 0)
            assertTrue(engine.openPrecomp(group))
            if (mode == "scene-low") assertTrue(engine.setSceneSetting(2, 1f))
            assertTrue(engine.closePrecomp())
            store.clearSelection()
        }
        val layerRows = ByteBuffer.allocateDirect(PodLayout.LAYER_ROW_BYTES * 128).order(ByteOrder.nativeOrder())
        val names = ByteBuffer.allocateDirect(16384)
        assertEquals(89, engine.queryLayers(layerRows, 128, names))
        if (mode == "one-3d-group") {
            val objects = ids.filterIndexed { index, _ -> index % 12 == 1 }.toSet()
            val positions = (0 until 89).filter { layerRows.getLong(it * PodLayout.LAYER_ROW_BYTES) in objects }
            assertEquals(8, positions.size)
            assertEquals("3D objects must be contiguous for this diagnostic", 7, positions.last() - positions.first())
        }
        note("ready rootLayers=89 totalLayers=97 effects=176 keys=768")
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
        assertEquals(0, engine.saveProject(project.absolutePath))
        note("trimMemory reclaimed=${engine.trimMemory(80)}")
        assertEquals(0, engine.loadProject(project.absolutePath))
        assertEquals(89, engine.queryLayers(layerRows, 128, names))
        note("reopen-passed")
        val pixels = ByteBuffer.allocateDirect(320 * 180 * 4)
        val dimensions = IntArray(2)
        assertEquals(320 * 180 * 4, engine.captureFrame(320, pixels, dimensions))
        assertTrue("diagnostic capture should have visible content", (0 until pixels.capacity() step 4).count {
            (pixels.get(it).toInt() and 255) + (pixels.get(it + 1).toInt() and 255) + (pixels.get(it + 2).toInt() and 255) > 45
        } > 100)
        note("DIAGNOSTIC PASSED: original acceptance still required")
    }
}
