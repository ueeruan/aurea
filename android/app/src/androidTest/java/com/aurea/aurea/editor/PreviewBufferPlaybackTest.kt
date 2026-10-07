package com.aurea.aurea.editor

import android.app.Application
import android.os.SystemClock
import android.util.Log
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.graphics.toPixelMap
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

class PreviewBufferPlaybackTest {
    @get:Rule val compose = createComposeRule()

    @Test fun playbackWarmsRenderedFramesAndPauseCancelsPendingPlayback() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(320, 180, 30f, "Rendered preview buffer") }
        compose.waitUntil(10000) { store.project.title == "Rendered preview buffer" }
        compose.runOnIdle { store.addShape(1); store.setCompositionDuration(300) }
        compose.waitUntil(5000) { store.layers.isNotEmpty() }
        compose.runOnIdle { store.seek(0) }
        compose.waitUntil(5000) { store.playhead == 0 }
        val playRequested = SystemClock.elapsedRealtime()
        compose.runOnIdle { store.play() }
        // The store can report the requested play state before native warm-up
        // starts. Require real timeline progress, not the old paused cache.
        compose.waitUntil(15000) { store.playing && store.playhead > 0 && !store.preview.buffering && store.preview.bufferedFrames > 0 }
        val startupMs = SystemClock.elapsedRealtime() - playRequested
        Log.i("AureaDeviceTest", "preview startup=$startupMs ms cached=${store.preview.bufferedFrames}")
        assertTrue("A simple composition must not wait seconds for its entire cache ($startupMs ms)", startupMs < 2500)
        assertTrue(store.preview.bufferedFrames <= 255)
        compose.waitForIdle()
        compose.onNodeWithTag("preview.buffer.status").assertDoesNotExist()
        compose.onNodeWithTag("timeline.preview.buffer").assertIsDisplayed()
        Thread.sleep(120) // Let SurfaceFlinger display the freshly composed badge before the raw capture.
        stabilityScreenshot("preview-buffer.png")
        compose.runOnIdle { store.pause() }
        compose.waitUntil(5000) { !store.playing && !store.preview.buffering }
        compose.waitUntil(5000) { store.previewBufferRanges.sumOf { it.endFrame - it.startFrame } > 1 }
        val cached = store.previewBufferRanges
        assertTrue(cached.all { it.startFrame >= 0 && it.endFrame > it.startFrame })
        assertTrue(cached.zipWithNext().all { (a, b) -> a.endFrame < b.startFrame })
        val pixels = compose.onNodeWithTag("editor.timeline").captureToImage().toPixelMap()
        val rulerBottom = (com.aurea.aurea.ui.theme.AureaTimeline.RulerTicks.value * context.resources.displayMetrics.density).toInt() + 1
        var blue = 0
        for (y in 0 until minOf(rulerBottom, pixels.height)) for (x in 0 until pixels.width) {
            if (pixels[x, y].toArgb() == 0xFF4DA3FF.toInt()) ++blue
        }
        assertTrue("Native cached ranges must appear as blue pixels on the ruler", blue > 0)
        // Editing invalidates all earlier completed frames; a paused render can cache only its current frame.
        val x = store.detail!!.position[0] + 20f
        compose.runOnIdle { store.setTransform2(0, x, 1, store.detail!!.position[1]) }
        compose.waitUntil(5000) {
            store.detail?.position?.get(0) == x && store.previewBufferRanges.sumOf { it.endFrame - it.startFrame } <= 1
        }
        // Both commands enter the same native queue: pause must cancel warm-up as well as playback.
        compose.runOnIdle { store.seek(0); store.play(); store.pause() }
        compose.waitUntil(5000) { !store.playing && !store.preview.buffering && store.playhead == 0 }
        Thread.sleep(500)
        compose.runOnIdle { assertEquals(0, store.playhead); assertFalse(store.playing) }
    }
}
