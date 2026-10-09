package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import android.net.Uri
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.PanelEnv
import com.aurea.aurea.editor.panels.TransformPanel
import com.aurea.aurea.editor.panels.TransformTab
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import java.io.File
import java.nio.ByteBuffer
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

/** Exercise the production activation button and real Android renderer. */
class Camera3DActivationDeviceTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore

    @Test fun imageAtZeroTiltRespondsToCameraParentedToNull() = checkActivation(false)
    @Test fun videoAtZeroTiltRespondsToCameraParentedToNull() = checkActivation(true)

    private fun capture(): ByteArray {
        val pixels = ByteBuffer.allocateDirect(320 * 180 * 4)
        val dims = IntArray(2)
        assertEquals(320 * 180 * 4, store.engineForStress.captureFrame(320, pixels, dims))
        return ByteArray(pixels.capacity()).also { pixels.rewind(); pixels.get(it) }
    }

    private fun difference(a: ByteArray, b: ByteArray): Double = a.indices
        .filter { it % 4 != 3 }.sumOf { kotlin.math.abs((a[it].toInt() and 255) - (b[it].toInt() and 255)).toDouble() } / (320 * 180 * 3)

    private fun saveCapture(name: String, pixels: ByteArray) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val folder = File(context.getExternalFilesDir(null), "camera-3d-activation").apply { mkdirs() }
        val bitmap = Bitmap.createBitmap(320, 180, Bitmap.Config.ARGB_8888)
        try {
            val colors = IntArray(320 * 180) { index ->
                val i = index * 4
                Color.argb(pixels[i + 3].toInt() and 255, pixels[i].toInt() and 255,
                    pixels[i + 1].toInt() and 255, pixels[i + 2].toInt() and 255)
            }
            bitmap.setPixels(colors, 0, 320, 0, 0, 320, 180)
            File(folder, name).outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        } finally { bitmap.recycle() }
    }

    private fun checkActivation(video: Boolean) {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        check(context.packageName.endsWith(".uitest"))
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            val env = remember(store) { PanelEnv(store, {}, {}, {}, {}, {}, { null }) }
            val ui = remember { EditorUi() }
            AureaTheme {
                Column {
                    PreviewStage(store, ui, Modifier.fillMaxWidth().height(220.dp))
                    Box(Modifier.fillMaxWidth().height(360.dp)) {
                        TransformPanel(env, TransformTab.Girar, onTab = {})
                    }
                }
            }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        val title = "Camera 3D activation ${if (video) "video" else "image"}"
        compose.runOnIdle { store.newProject(320, 180, 30f, title) }
        compose.waitUntil(15000) { store.project.title == title }
        val fixture = File(context.filesDir, if (video) "camera-activation.mp4" else "camera-activation.png")
        val assets = if (video) instrumentation.context.assets else context.assets
        assets.open(if (video) "motion-fixture.mp4" else "review-bars.png").use { input ->
            fixture.outputStream().use { input.copyTo(it) }
        }
        compose.runOnIdle { if (video) store.importVideo(Uri.fromFile(fixture)) else store.importImage(Uri.fromFile(fixture)) }
        compose.waitUntil(20000) { store.layers.any { it.kind == if (video) 1 else 2 } }
        val media = store.layers.first { it.kind == if (video) 1 else 2 }.id
        compose.runOnIdle { store.seek(0); store.addNull(true) }
        compose.waitUntil(10000) { store.layers.any { it.kind == 6 } }
        val nullId = store.layers.first { it.kind == 6 }.id
        compose.runOnIdle { store.addCamera() }
        compose.waitUntil(10000) { store.layers.any { it.kind == 8 } }
        val camera = store.layers.first { it.kind == 8 }.id
        compose.runOnIdle { store.setParent(camera, nullId); store.select(media) }
        compose.waitUntil(10000) { store.detail?.id == media }
        assertEquals(0, store.detail!!.flags and PodLayout.FLAG_THREE_D)
        assertTrue(store.detail!!.rotation.all { it == 0f })
        assertEquals(0f, store.detail!!.position[2], 0f)
        val rest = capture()
        compose.runOnIdle { store.setTransform(7, 25f, nullId) }
        val bypass = capture()
        assertTrue("2D media must remain independent of the scene camera", difference(rest, bypass) < 1)
        compose.onNodeWithTag("transform.enable3d").assertIsDisplayed().performClick()
        compose.waitUntil(10000) { store.detail!!.flags and PodLayout.FLAG_THREE_D != 0 }
        assertTrue(store.detail!!.rotation.all { it == 0f })
        assertEquals(0f, store.detail!!.position[2], 0f)
        val orbit = capture()
        assertTrue("Zero-tilt 3D media must follow the null-driven camera", difference(bypass, orbit) > 5)
        compose.onNodeWithTag("transform.enable3d").assertDoesNotExist()
        compose.runOnIdle { store.undo() }
        compose.waitUntil(10000) { store.detail!!.flags and PodLayout.FLAG_THREE_D == 0 }
        assertTrue(difference(bypass, capture()) < 1)
        compose.onNodeWithTag("transform.enable3d").assertIsDisplayed()
        compose.runOnIdle { store.redo() }
        compose.waitUntil(10000) { store.detail!!.flags and PodLayout.FLAG_THREE_D != 0 }
        assertTrue(difference(orbit, capture()) < 1)
        val path = File(context.filesDir, "camera-activation-${if (video) "video" else "image"}.aurea")
        compose.runOnIdle {
            assertEquals(0, store.engineForStress.saveProject(path.absolutePath))
            assertEquals(0, store.engineForStress.loadProject(path.absolutePath))
            store.select(media)
        }
        compose.waitUntil(10000) { store.detail?.id == media && store.detail!!.flags and PodLayout.FLAG_THREE_D != 0 }
        assertTrue(difference(orbit, capture()) < 1)
        val prefix = if (video) "video" else "image"
        saveCapture("${prefix}-2d.png", bypass)
        saveCapture("${prefix}-3d.png", orbit)
    }
}
