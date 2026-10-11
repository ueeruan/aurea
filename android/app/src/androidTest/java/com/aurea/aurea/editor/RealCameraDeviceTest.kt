package com.aurea.aurea.editor

import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.engine.TrackProperty
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.cos
import kotlin.math.sin

/** Actual Android JNI/shared capture. Pose injection tests recording and axes;
 * it does not claim that emulator motion proves physical AR camera tracking. */
class RealCameraDeviceTest {
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private lateinit var engine: AureaEngine
    private val folder get() = File(context.filesDir, "real-camera-2175").apply { mkdirs() }
    private var stamp = 1L
    @Before fun initialize() {
        check(context.packageName == "com.aurea.aurea.uitest")
        for (font in listOf("Roboto-Regular.ttf", "NotoSansJP-Regular.otf")) {
            context.assets.open(font).use { input -> File(context.cacheDir, font).outputStream().use { input.copyTo(it) } }
        }
        engine = AureaEngine.create(context)
        assertTrue(engine.startupError(), engine.initialize(60f, context.cacheDir.absolutePath, folder.absolutePath, false))
        assertTrue(engine.newProject(800, 600, 30.0, "Real Camera JNI", floatArrayOf(0f, 0f, 0f, 1f)))
    }
    @After fun close() { if (::engine.isInitialized) engine.destroy() }
    private fun pose(z: Float = 0f, yaw: Float = 0f): FloatArray {
        val angle = Math.toRadians(yaw.toDouble()); val c = cos(angle).toFloat(); val s = sin(angle).toFloat()
        return floatArrayOf(c, 0f, -s, 0f, 0f, 1f, 0f, 0f, s, 0f, c, 0f, 0f, 0f, z, 1f)
    }
    private fun sample(matrix: FloatArray, positional: Boolean = false) {
        assertTrue(engine.realCameraSample(matrix, stamp, positional, 1f, false)); stamp += 33_333_333L
    }
    private fun frame(value: Int) {
        engine.beginCommandBatch(); assertTrue(CommandBatch(engine).seek((value * 1_000_000_000L + 29) / 30)); assertEquals(1, engine.submitCommands())
        assertEquals(0, engine.saveProject(File(folder, "owned-drain.aurea").absolutePath))
    }
    private fun keys(layer: Long): List<KeyframeRow> {
        val buffer = ByteBuffer.allocateDirect(PodLayout.KEYFRAME_ROW_BYTES * 1024).order(ByteOrder.nativeOrder())
        val count = engine.queryKeyframes(layer, buffer, 1024); assertTrue(count >= 0)
        return List(count) { KeyframeRow.read(buffer, it) }
    }
    @Test fun recordingCreatesFirstRotationKeysAndOneUndoGroupWithoutUICachedPlaying() {
        val layer = engine.addCamera(); assertTrue(layer > 0)
        assertTrue(engine.realCameraStart(layer)); assertTrue(engine.realCameraRecord(true))
        assertEquals(1L, engine.realCameraStatus()[1])
        sample(pose())
        val first = keys(layer); assertEquals(3, first.size)
        assertEquals(setOf(TrackProperty.ROTATION_X, TrackProperty.ROTATION_Y, TrackProperty.ROTATION_Z), first.map { it.property }.toSet())
        assertTrue(first.all { it.time == 0 })
        sample(pose(yaw = 10f)); assertEquals(3, keys(layer).size)
        frame(1); sample(pose(yaw = 10f)); assertEquals(6, keys(layer).size)
        assertEquals(1L, engine.realCameraStatus()[2]); assertTrue(engine.realCameraRecord(false))
        engine.beginCommandBatch(); assertTrue(CommandBatch(engine).undo()); assertEquals(1, engine.submitCommands())
        assertEquals(0, engine.saveProject(File(folder, "owned-undo.aurea").absolutePath)); assertTrue(keys(layer).isEmpty())
        engine.beginCommandBatch(); assertTrue(CommandBatch(engine).redo()); assertEquals(1, engine.submitCommands())
        assertEquals(0, engine.saveProject(File(folder, "owned-redo.aurea").absolutePath)); assertEquals(6, keys(layer).size)
        engine.realCameraStop(); assertEquals(0L, engine.realCameraStatus()[0])
    }
    @Test fun forwardAndBackwardPositionKeysPersistInOrdinaryProjectFormat() {
        val layer = engine.addCamera(); assertTrue(layer > 0)
        assertTrue(engine.realCameraStart(layer)); assertTrue(engine.realCameraRecord(true)); sample(pose(), true)
        for (i in 1..30) { frame(i); sample(pose(z = -.5f), true) }
        for (i in 31..60) { frame(i); sample(pose(z = .5f), true) }
        assertTrue(engine.realCameraRecord(false))
        val recorded = keys(layer); assertEquals(61 * 6, recorded.size)
        val z = recorded.filter { it.property == TrackProperty.POSITION_Z }.associateBy { it.time }
        assertEquals(z.getValue(0).value + 150f, z.getValue(30).value, .02f)
        assertEquals(z.getValue(0).value - 150f, z.getValue(60).value, .02f)
        val saved = File(folder, "owned-position-roundtrip.aurea"); assertEquals(0, engine.saveProject(saved.absolutePath))
        assertEquals(0, engine.loadProject(saved.absolutePath)); assertEquals(0L, engine.realCameraStatus()[0])
        assertEquals(recorded, keys(layer))
    }
    @Test fun staleAndInvalidTrackingSamplesCannotWriteKeys() {
        val layer = engine.addCamera(); assertTrue(layer > 0)
        assertTrue(engine.realCameraStart(layer)); assertTrue(engine.realCameraRecord(true)); sample(pose())
        val before = keys(layer)
        assertFalse(engine.realCameraSample(pose(), 1, false, 1f, false))
        val invalid = pose().apply { this[0] = Float.NaN }
        assertFalse(engine.realCameraSample(invalid, stamp, false, 1f, false)); assertEquals(before, keys(layer))
        assertTrue(engine.realCameraRecord(false))
    }
}
