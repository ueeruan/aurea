package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ExportOptions
import com.aurea.aurea.state.ExportPhase
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.ui.theme.AureaTheme
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

class ExportLifecycleStabilityTest {
    @get:Rule val compose = createComposeRule()

    @Test fun previewWorkerSleepsWhileVideoExportOwnsTheRenderer() {
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
        compose.runOnIdle { store.newProject(1280, 720, 30f, "Export worker CPU") }
        compose.waitUntil(10000) { store.project.title == "Export worker CPU" && !store.projectOperationBusy }
        compose.runOnIdle {
            repeat(12) { store.addShape(it % 8) }
            store.setCompositionDuration(240)
        }
        compose.waitUntil(10000) { store.layers.size == 12 && store.project.durationFrames == 240 }
        val thread = checkNotNull(File("/proc/self/task").listFiles()?.firstOrNull {
            runCatching { File(it, "comm").readText().trim() == "aurea-render" }.getOrDefault(false)
        }) { "Native preview thread must exist" }
        fun ticks(): Long {
            val fields = File(thread, "stat").readText().substringAfterLast(')').trim().split(Regex("\\s+"))
            return fields[11].toLong() + fields[12].toLong() // proc fields 14 + 15: utime + stime
        }
        val output = File(context.filesDir, "export-preview-idle.mp4")
        val progress = ExportProgress()
        val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        fun poll(): Boolean = store.engineForStress.exportProgress(buffer).also { if (it) progress.readFrom(buffer) }
        compose.runOnIdle {
            store.seek(17) // Leave a fresh preview refinement request before handing ownership to export.
            assertEquals(0, store.engineForStress.startExport(output.absolutePath, 720, 30.0, 0, 8))
        }
        try {
            assertTrue(poll())
            val started = android.os.SystemClock.elapsedRealtime()
            val before = ticks()
            while (!progress.finished && android.os.SystemClock.elapsedRealtime() - started < 2000) {
                android.os.SystemClock.sleep(100)
                assertTrue(poll())
            }
            val elapsed = android.os.SystemClock.elapsedRealtime() - started
            val cpuTicks = ticks() - before
            val clockHz = android.system.Os.sysconf(android.system.OsConstants._SC_CLK_TCK)
            val cpuMillis = cpuTicks * 1000.0 / clockHz
            File(context.filesDir, "export-preview-idle.txt").writeText(
                "elapsedMs=$elapsed previewCpuMs=$cpuMillis clockHz=$clockHz frames=${progress.framesDone}/${progress.framesTotal} finished=${progress.finished}\n")
            assertTrue("Export ended too quickly to establish a CPU observation: ${elapsed}ms", elapsed >= 500)
            assertTrue("Preview worker consumed ${cpuMillis}ms CPU during ${elapsed}ms export", cpuMillis < elapsed * .25)
        } finally {
            store.engineForStress.cancelExport()
            compose.waitUntil(60000) { poll() && progress.finished }
        }
    }

    @Test fun cancellingDuringStartupAllowsRetryAndPublishesReadableVideo() {
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
        compose.runOnIdle { store.newProject(320, 180, 30f, "Export cancellation retry") }
        compose.waitUntil(10000) { store.project.title == "Export cancellation retry" }
        compose.runOnIdle { store.addShape(1); store.setCompositionDuration(30) }
        compose.waitUntil(5000) { store.layers.isNotEmpty() && store.project.durationFrames == 30 }
        val options = ExportOptions(shortSide = 180, fps = 30.0, trimToContent = false)
        compose.runOnIdle {
            store.exporter.start("Cancelled stability test", 320, 180, 30.0, options)
            store.exporter.cancel()
        }
        compose.waitUntil(60000) { !store.exporter.busy }
        assertEquals(store.exporter.state.message, ExportPhase.Cancelled, store.exporter.state.phase)
        compose.runOnIdle { store.exporter.start("Published stability test", 320, 180, 30.0, options) }
        compose.waitUntil(120000) { !store.exporter.busy }
        val state = store.exporter.state
        assertEquals(state.message, ExportPhase.Done, state.phase)
        val uri = checkNotNull(state.outputUri)
        try {
            assertEquals("content", uri.scheme)
            context.contentResolver.openInputStream(uri)!!.use { assertTrue("Published MP4 must be readable", it.readBytes().size > 1000) }
            assertNotNull(store.exporter.shareIntent())
        } finally {
            context.contentResolver.delete(uri, null, null)
        }
    }
}
