package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.os.SystemClock
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.EngineStatus
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Captures the original heavy acceptance project without reconstructing it.
 * Compare this lossless final-quality image with the MP4 decoded independently;
 * a non-null MediaMetadataRetriever bitmap alone cannot prove export content. */
class HeavyExportFidelityDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun captureTheExactExportedHeavySceneAtFrame45() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        check(context.packageName == "com.aurea.aurea.uitest")
        org.junit.Assume.assumeTrue(InstrumentationRegistry.getArguments().getString("aureaStress") == "true")
        val width = InstrumentationRegistry.getArguments().getString("aureaCaptureWidth")?.toInt() ?: 1280
        require(width == 1280 || width == 1920)
        val height = width * 9 / 16
        val folder = File(context.filesDir, "stress-2126")
        val project = File(folder, "heavy-edit.aurea")
        assertTrue("Run the original dense export acceptance before this comparison", project.isFile)
        assertTrue(File(folder, "heavy-edit-720p.mp4").length() > 0)
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        val engine = store.engineForStress
        assertEquals(0, engine.loadProject(project.absolutePath))
        compose.runOnIdle { store.pause(); store.seek(45) }
        val statusBuffer = ByteBuffer.allocateDirect(PodLayout.STATUS_BYTES).order(ByteOrder.nativeOrder())
        val began = SystemClock.elapsedRealtime()
        while (true) {
            assertTrue(engine.readStatus(statusBuffer))
            val status = EngineStatus().also { it.readFrom(statusBuffer) }
            if (!status.playing && status.playhead == 45L) break
            assertTrue("Native seek to exact export frame did not finish", SystemClock.elapsedRealtime() - began < 30000)
            Thread.sleep(50)
        }
        // Native capture runs on the instrumentation thread, never the UI thread.
        val rgba = ByteBuffer.allocateDirect(width * height * 4)
        val dimensions = IntArray(2)
        assertEquals(rgba.capacity(), engine.captureFrame(width, rgba, dimensions))
        assertArrayEquals(intArrayOf(width, height), dimensions)
        val bytes = ByteArray(rgba.capacity()).also { rgba.rewind(); rgba.get(it) }
        File(folder, "final-capture-$height-frame45.rgba").writeBytes(bytes)
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        try {
            bitmap.copyPixelsFromBuffer(ByteBuffer.wrap(bytes))
            File(folder, "final-capture-$height-frame45.png").outputStream().use {
                bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)
            }
        } finally { bitmap.recycle() }
        val lit = (bytes.indices step 4).count { offset ->
            (0..2).any { channel -> (bytes[offset + channel].toInt() and 255) > 25 }
        }
        File(folder, "final-capture-$height-frame45.txt").writeText("frame=45 width=$width height=$height lit=$lit\n")
        assertTrue("Heavy scene must have visible content", lit > width * height / 10)
    }
}
