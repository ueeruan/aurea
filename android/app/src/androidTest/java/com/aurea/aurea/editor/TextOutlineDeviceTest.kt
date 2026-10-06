package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.ui.test.junit4.createComposeRule
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
import java.nio.ByteBuffer

class TextOutlineDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun outlineKeepsManualAndAnimatedPivotsUnderPerspective() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var initialized = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            initialized = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { initialized && store.engineReady }
        compose.runOnIdle { store.newProject(640, 360, 30f, "Outline pivot regression") }
        compose.waitUntil(15000) { store.project.title == "Outline pivot regression" }
        compose.runOnIdle { store.addText(); store.dismissTextContentEditor() }
        compose.waitUntil(5000) { store.textDetail != null }
        compose.runOnIdle {
            store.setTextContent("AVAVA"); store.setTextSize(88f)
            store.setTextColor(1f, 1f, 1f, 1f)
            store.setTextStrokeColor(1f, 0f, 0f, 1f)
        }
        compose.waitUntil(5000) { store.textDetail?.content == "AVAVA" && store.textDetail?.size == 88f }
        val engine = store.engineForStress
        val layer = store.layers.single { it.kind == 4 }.id
        val anchor = checkNotNull(store.detail).anchor
        fun commands(block: CommandBatch.() -> Unit) = compose.runOnIdle {
            engine.beginCommandBatch()
            CommandBatch(engine).block()
            assertTrue(engine.submitCommands() > 0)
        }
        fun capture(): ByteArray {
            val pixels = ByteBuffer.allocateDirect(640 * 360 * 4)
            val count = engine.captureFrame(640, pixels, IntArray(2))
            assertEquals(640 * 360 * 4, count)
            return ByteArray(count).also { pixels.rewind(); pixels.get(it) }
        }
        fun stroke(width: Float) {
            compose.runOnIdle { store.setTextStrokeWidth(width) }
            compose.waitUntil(5000) { store.textDetail?.strokeWidth == width }
        }
        for (animated in listOf(false, true)) {
            commands {
                setAnchor(layer, anchor[0] + 24, anchor[1] - 15, 0f)
                setRotation(layer, 14f, 48f, -9f)
                if (animated) {
                    insertKeyframe(layer, TrackProperty.ANCHOR_X, -1, 0, 0, anchor[0] + 24)
                    insertKeyframe(layer, TrackProperty.ANCHOR_X, -1, 0, 30, anchor[0] + 48)
                    insertKeyframe(layer, TrackProperty.ANCHOR_Y, -1, 0, 0, anchor[1] - 15)
                    insertKeyframe(layer, TrackProperty.ANCHOR_Y, -1, 0, 30, anchor[1] - 30)
                }
            }
            compose.runOnIdle { store.seek(12) }
            for (scale in listOf(.55f, 1f, 1.75f)) {
                commands { setScale(layer, scale, scale, 1f) }
                val before = capture()
                stroke(18f)
                val after = capture()
                var filled = 0; var lost = 0; var red = 0
                fun green(image: ByteArray, x: Int, y: Int) = image[(y * 640 + x) * 4 + 1].toInt() and 255
                for (y in 1 until 359) for (x in 1 until 639) {
                    val i = (y * 640 + x) * 4
                    if ((after[i].toInt() and 255) > 200 && green(after, x, y) < 80) ++red
                    if (green(before, x, y) < 250) continue
                    ++filled
                    var remains = false
                    for (dy in -1..1) for (dx in -1..1) remains = remains || green(after, x + dx, y + dy) >= 235
                    if (!remains) ++lost
                }
                val diagnostic = "animated=$animated scale=$scale filled=$filled movedOrCovered=$lost red=$red"
                android.util.Log.i("AureaOutline", diagnostic)
                assertTrue(diagnostic, filled > 100 && red > 100)
                assertTrue("Outline moved the text: $diagnostic", lost * 20 <= filled)
                stroke(0f)
                val restored = capture()
                val difference = before.indices.maxOf { kotlin.math.abs((before[it].toInt() and 255) - (restored[it].toInt() and 255)) }
                assertTrue("Removing outline changed the pivot: $diagnostic difference=$difference", difference <= 2)
            }
        }
        stabilityScreenshot("community-outline-perspective.png")
    }
}
