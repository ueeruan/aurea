package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import android.net.Uri
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.*
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer

class Beta007EditingTest {
    @get:Rule val compose = createComposeRule()

    @Test fun letterSpacingKeysChangeRenderedTextAndSurviveUndo() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName == "com.aurea.aurea.uitest")
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(640, 360, 30f, "Beta 007 spacing") }
        compose.waitUntil(10000) { store.project.title == "Beta 007 spacing" && !store.projectOperationBusy }
        compose.runOnIdle { store.addText(); store.dismissTextContentEditor() }
        compose.waitUntil(5000) { store.textStyle != null }
        compose.runOnIdle { store.setTextContent("AAAA"); store.setTextSize(64f); store.setTextColor(1f, 0f, 0f, 1f) }
        val id = checkNotNull(store.primary)
        compose.onNodeWithTag("dock.tool.TextOptions").performScrollTo().performClick()
        compose.onNodeWithTag("text.tracking.key").performScrollTo().assertIsEnabled().performClick()
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().any { it.property == 29 && it.time == 0 } }
        compose.runOnIdle { store.seek(60) }
        compose.waitUntil(5000) { store.detail?.localPlayhead == 60 }
        compose.runOnIdle { store.setTextStyleValue(19, 60f) }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().any { it.property == 29 && it.time == 60 } }
        fun widthAt(frame: Int): Int {
            compose.runOnIdle { store.seek(frame) }
            compose.waitUntil(5000) { store.detail?.localPlayhead == frame }
            val buffer = ByteBuffer.allocateDirect(640 * 360 * 4)
            val size = IntArray(2)
            assertEquals(640 * 360 * 4, store.engineForStress.captureFrame(640, buffer, size))
            var left = 640; var right = -1
            for (y in 0 until 360) for (x in 0 until 640) {
                val p = (y * 640 + x) * 4
                val red = buffer.get(p).toInt() and 255
                val green = buffer.get(p + 1).toInt() and 255
                val blue = buffer.get(p + 2).toInt() and 255
                if (red > 100 && red > maxOf(green, blue) + 70) { left = minOf(left, x); right = maxOf(right, x) }
            }
            assertTrue("Text is visible at frame $frame", right >= left)
            return right - left + 1
        }
        val before = widthAt(0)
        val middle = widthAt(30)
        val after = widthAt(60)
        // Tracking is in thousandths of an em: three gaps * 64px * 60/1000.
        assertEquals("Interpolated tracking changes rendered glyph positions", before + 5.76, middle.toDouble(), 3.0)
        assertEquals("Final tracking changes all three gaps", before + 11.52, after.toDouble(), 3.0)
        assertTrue("Widths grow at each sampled time", before < middle && middle < after)
        compose.runOnIdle { store.undo() }
        compose.waitUntil(5000) { store.keyframes[id].orEmpty().none { it.property == 29 && it.time == 60 } }
        assertEquals(before, widthAt(60))
        File(context.filesDir, "beta007-spacing-result.txt").writeText("PASS: native spacing key control, rendered widths $before/$middle/$after, undo\n")
    }

    @Test fun mixedBatchKeepsAllMediaAndSelectionAndExtendsTrimmedSource() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        check(context.packageName == "com.aurea.aurea.uitest")
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(320, 180, 30f, "Beta 007 batch") }
        compose.waitUntil(10000) { store.project.title == "Beta 007 batch" && !store.projectOperationBusy }
        val video = File(context.filesDir, "beta007-video.mp4")
        instrumentation.context.assets.open("motion-fixture.mp4").use { input -> video.outputStream().use { input.copyTo(it) } }
        fun image(name: String, color: Int): File {
            val file = File(context.filesDir, name)
            val bitmap = Bitmap.createBitmap(32, 24, Bitmap.Config.ARGB_8888)
            try { bitmap.eraseColor(color); file.outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) } }
            finally { bitmap.recycle() }
            return file
        }
        val first = Uri.fromFile(image("beta007-first.png", Color.RED))
        val second = Uri.fromFile(image("beta007-second.png", Color.BLUE))
        compose.runOnIdle { store.importMediaBatch(listOf(first, Uri.fromFile(video), second, first)) }
        compose.waitUntil(30000) { store.busyMessage == null && store.layers.size == 3 && store.selection.size == 3 }
        compose.runOnIdle {
            assertEquals(2, store.layers.count { it.kind == 2 })
            assertEquals(1, store.layers.count { it.kind == 1 })
            assertEquals(3, store.selection.size)
            assertNull(store.errorMessage)
        }
        val clip = store.layers.single { it.kind == 1 }
        val originalEnd = clip.endFrame
        assertTrue(originalEnd > 20)
        compose.runOnIdle { store.setLayerRanges(longArrayOf(clip.id), intArrayOf(0), intArrayOf(20)); store.select(clip.id) }
        compose.waitUntil(10000) { store.layers.single { it.id == clip.id }.endFrame == 20 && store.primary == clip.id }
        compose.runOnIdle { store.editClipTime(7, 0) }
        compose.waitUntil(10000) { store.layers.single { it.id == clip.id }.endFrame == originalEnd }
        compose.runOnIdle { store.undo() }
        compose.waitUntil(10000) { store.layers.single { it.id == clip.id }.endFrame == 20 }
        File(context.filesDir, "beta007-batch-result.txt").writeText("PASS: mixed batch, duplicate URI, all selection, extend source and undo\n")
    }
}
