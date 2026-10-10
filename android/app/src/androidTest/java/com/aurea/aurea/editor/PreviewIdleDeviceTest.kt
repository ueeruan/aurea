package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import android.os.SystemClock
import android.util.Log
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import kotlin.math.abs

class PreviewIdleDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun pausedAnimationWarmsFutureFramesWithoutChangingThePresentedPicture() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        check(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(320, 180, 30f, "Idle preview proof") }
        compose.waitUntil(10000) { store.project.title == "Idle preview proof" }
        compose.runOnIdle { store.addShape(1); store.setCompositionDuration(180) }
        compose.waitUntil(5000) { store.layers.size == 1 }
        val layer = store.layers.single().id
        val engine = store.engineForStress
        fun commands(block: CommandBatch.() -> Unit) = compose.runOnIdle {
            engine.beginCommandBatch()
            CommandBatch(engine).block()
            assertTrue(engine.submitCommands() > 0)
        }
        commands {
            setPreviewScale(false, 1, 1)
            setShapeParam(layer, 5, 36f); setShapeParam(layer, 6, 36f)
            setShapeFill(layer, 1f, 0f, 0f, 1f)
            setPosition(layer, 64f, 90f, 0f)
            insertKeyframe(layer, TrackProperty.POSITION_X, -1, 0, 0, 64f)
            insertKeyframe(layer, TrackProperty.POSITION_X, -1, 0, 29, 256f)
        }
        compose.runOnIdle { store.clearSelection(); store.seek(0) }
        compose.waitUntil(5000) { store.playhead == 0 && !store.playing }
        val stage = compose.onNodeWithTag("editor.stage").fetchSemanticsNode().boundsInWindow
        val directory = File(context.getExternalFilesDir(null), "preview-idle").apply { mkdirs() }
        data class Presented(val count: Int, val x: Double, val y: Double)
        fun picture(name: String, green: Boolean = false): Presented {
            val bitmap = checkNotNull(instrumentation.uiAutomation.takeScreenshot())
            try {
                var count = 0; var xSum = 0L; var ySum = 0L
                for (y in stage.top.toInt().coerceAtLeast(0) until stage.bottom.toInt().coerceAtMost(bitmap.height)) {
                    for (x in stage.left.toInt().coerceAtLeast(0) until stage.right.toInt().coerceAtMost(bitmap.width)) {
                        val pixel = bitmap.getPixel(x, y)
                        val primary = if (green) Color.green(pixel) else Color.red(pixel)
                        val secondary = if (green) Color.red(pixel) else Color.green(pixel)
                        if (primary > 180 && secondary < 70 && Color.blue(pixel) < 70) {
                            ++count; xSum += x; ySum += y
                        }
                    }
                }
                File(directory, "$name.png").outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
                return Presented(count, xSum.toDouble() / count.coerceAtLeast(1), ySum.toDouble() / count.coerceAtLeast(1))
            } finally { bitmap.recycle() }
        }
        fun visible(name: String, green: Boolean = false): Presented {
            val deadline = SystemClock.elapsedRealtime() + 5000
            var sample: Presented
            do {
                sample = picture(name, green)
                if (sample.count > 100) return sample
                Thread.sleep(50)
            } while (SystemClock.elapsedRealtime() < deadline)
            fail("The animated ${if (green) "green" else "red"} layer is absent from the presented preview: $sample")
            return sample
        }
        val before = visible("paused-before")
        assertTrue("Frame zero belongs on the left, not at a speculative future position", before.x < stage.center.x - stage.width * .1)
        compose.waitUntil(5000) { store.preview.bufferTarget >= 2 }
        val target = store.preview.bufferTarget.coerceAtMost(30)
        val startupTarget = minOf(6, target)
        val warmStart = SystemClock.elapsedRealtime()
        compose.waitUntil(12000) {
            store.playhead == 0 && !store.playing && store.previewBufferRanges.any { it.startFrame == 0L && it.endFrame >= startupTarget }
        }
        val warmedMs = SystemClock.elapsedRealtime() - warmStart
        // Give the whole small composition window time to finish. A warm-up
        // accidentally presenting future frames would move this circle right.
        compose.waitUntil(12000) { store.previewBufferRanges.any { it.startFrame == 0L && it.endFrame >= target.toLong() } }
        val warmed = visible("paused-warmed")
        assertEquals(0, store.playhead)
        assertFalse(store.playing)
        assertTrue("Warm-up moved the displayed animation: $before -> $warmed", abs(before.x - warmed.x) <= 2 && abs(before.y - warmed.y) <= 2)
        assertTrue("Warm-up removed visible content", warmed.count >= before.count * .98)

        val requested = SystemClock.elapsedRealtime()
        compose.runOnIdle { store.play() }
        compose.waitUntil(5000) { store.playing && store.playhead > 0 && !store.preview.buffering }
        val startMs = SystemClock.elapsedRealtime() - requested
        assertTrue("Prepared simple preview took $startMs ms to start advancing", startMs < 2500)
        compose.runOnIdle { store.pause(); store.seek(0) }
        compose.waitUntil(5000) { !store.playing && store.playhead == 0 }
        commands { setShapeFill(layer, 0f, 1f, 0f, 1f) }
        val changed = visible("edited-green", green = true)
        assertTrue("Edited frame did not replace the cached red picture", abs(changed.x - before.x) <= 2)
        compose.waitUntil(12000) { store.previewBufferRanges.any { it.startFrame == 0L && it.endFrame >= startupTarget } }
        val report = "idle future frames=$target warmStartupMs=$warmedMs playAdvanceMs=$startMs before=$before warmed=$warmed edited=$changed ranges=${store.previewBufferRanges}\n"
        File(directory, "result.txt").writeText(report)
        Log.i("AureaPreviewIdle", report)
    }
}
