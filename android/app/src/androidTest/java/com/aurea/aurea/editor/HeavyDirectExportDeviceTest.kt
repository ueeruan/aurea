package com.aurea.aurea.editor

import android.app.Application
import android.os.Debug
import android.os.SystemClock
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Diagnostic discriminator: final1080 export of the saved heavy fixture in a
 * fresh process, without a preceding720 export or MediaMetadataRetriever.
 * The original sequential-export acceptance remains unchanged and required. */
class HeavyDirectExportDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun exportTheSavedHeavySceneDirectlyAt1080p() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        check(context.packageName == "com.aurea.aurea.uitest")
        org.junit.Assume.assumeTrue(InstrumentationRegistry.getArguments().getString("aureaStress") == "true")
        val folder = File(context.filesDir, "stress-2126")
        val project = File(folder, "heavy-edit.aurea")
        assertTrue("Original heavy fixture must already exist", project.isFile)
        val report = File(folder, "direct-1080-progress.txt")
        report.writeText("DIAGNOSTIC: same saved heavy scene; no preceding720/MMR; MP4 pixels require independent decoding\n")
        val began = SystemClock.elapsedRealtime()
        fun note(message: String) {
            val memory = Debug.MemoryInfo().also { Debug.getMemoryInfo(it) }
            report.appendText("t=${SystemClock.elapsedRealtime() - began} $message pssKB=${memory.totalPss} nativeBytes=${Debug.getNativeHeapAllocatedSize()}\n")
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
        note("before-load")
        assertEquals(0, engine.loadProject(project.absolutePath))
        note("loaded")
        val output = File(folder, "heavy-edit-direct-1080p.mp4")
        val progress = ExportProgress()
        val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        compose.runOnIdle { assertEquals(0, engine.startExport(output.absolutePath, 1080, 30.0, 0, 8)) }
        var lastFrame = -1
        var moved = SystemClock.elapsedRealtime()
        try {
            while (true) {
                engine.exportProgress(buffer)
                progress.readFrom(buffer)
                if (progress.framesDone != lastFrame) {
                    lastFrame = progress.framesDone
                    moved = SystemClock.elapsedRealtime()
                    // Every frame, so cold work is distinguishable from a stall.
                    note("frame=$lastFrame/${progress.framesTotal}")
                }
                if (progress.finished) break
                assertTrue("No export progress for150s at$lastFrame", SystemClock.elapsedRealtime() - moved < 150000)
                assertTrue("Export exceeded30min", SystemClock.elapsedRealtime() - began < 1800000)
                Thread.sleep(250)
            }
            note("finished result=${progress.result} message=${progress.message} approximate=${progress.frameFallback}")
            assertEquals(progress.message, 0, progress.result)
            assertEquals(90, progress.framesDone)
            assertFalse("Approximate video frames", progress.frameFallback)
            assertTrue(output.length() > 0)
            note("90 EXACT FRAMES ENCODED bytes=${output.length()}; independent pixel validation still required")
        } finally {
            if (!progress.finished) engine.cancelExport()
        }
    }
}
