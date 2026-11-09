package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import java.io.File
import java.nio.ByteBuffer
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

/**
 * Mesma folha da comp "CMP" do After Effects: sete animações com os mesmos
 * valores, tempos e fonte. Precisa da fonte em filesDir/arialbd.ttf; os
 * quadros saem em files/aecmp/ para a comparação feita fora do app
 * (build/reference/ae-medida).
 */
class AeTextCompareDeviceTest {
    @get:Rule val compose = createComposeRule()
    private lateinit var store: EditorStore

    private fun selector(field: Int) = 16 + field

    private fun row(i: Int, props: Int, font: File): Long {
        val engine = store.engineForStress
        compose.runOnIdle { store.pause(); store.seek(0) }
        compose.waitUntil(5000) { store.playhead == 0 }
        Thread.sleep(250)
        val id = engine.addText("Animate Text")
        assertTrue(id != 0L)
        val size = 100f * 1.11719f
        compose.runOnIdle {
            assertTrue(engine.setTextFont(id, "Arial", 700, false, font.absolutePath))
            store.select(id, openOptions = false)
            store.setTextSize(size)
            store.setTransform(TrackProperty.ANCHOR_X, 2f, id)
            store.setTransform(TrackProperty.ANCHOR_Y, 2f + size * 0.81031f, id)
            store.setTransform(TrackProperty.POSITION_X, if (i % 2 == 1) 860f else 60f, id)
            store.setTransform(TrackProperty.POSITION_Y, 200f + 260f * (i / 2), id)
            assertEquals(0, engine.addTextAnimator(id, props))
        }
        return id
    }

    private fun keys(id: Long, param: Int, values: List<Pair<Int, Float>>, easy: Boolean) {
        val engine = store.engineForStress
        for ((frame, value) in values) {
            compose.runOnIdle { store.pause(); store.seek(frame) }
            compose.waitUntil(5000) { store.playhead == frame }
            Thread.sleep(250)
            compose.runOnIdle {
                assertTrue(engine.toggleTextAnimKey(id, 0, param))
                assertTrue(engine.setTextAnimParam(id, 0, param, value))
            }
        }
        compose.runOnIdle {
            for ((frame, value) in values) {
                val k = KeyframeRow(TrackProperty.TEXT_ANIM_PARAM, 0, frame, value, if (easy) 2 else 1, param)
                if (easy) store.setKeyframeEasing(id, k, 2, 0.3333f, 0f, 0.6667f, 1f, 1, linkAxes = true)
                else store.setKeyframeEasing(id, k, 1, 0f, 0f, 1f, 1f, 1, linkAxes = true)
            }
        }
    }

    @Test fun rendersTheSameSheetAsAfterEffects() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val folder = File(context.getExternalFilesDir(null), "aecmp").apply { mkdirs() }
        val font = File(context.filesDir, "arialbd.ttf")
        assertTrue("sem a fonte da comparação em ${font.absolutePath}", font.exists())
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(1600, 1040, 30f, "CMP") }
        compose.waitUntil(10000) { store.project.title == "CMP" && !store.projectOperationBusy }
        val engine = store.engineForStress
        val position = 1; val scale = 2; val rotation = 4; val opacity = 8; val tracking = 16; val blur = 32
        fun set(id: Long, param: Int, value: Float) = compose.runOnIdle { assertTrue(engine.setTextAnimParam(id, 0, param, value)) }
        fun field(id: Long, f: Int, value: Float) = compose.runOnIdle { assertTrue(engine.setTextAnimatorField(id, 0, f, value)) }
        val start = 0; val end = 1; val offset = 2; val amount = 3; val easeHigh = 4; val easeLow = 5
        val posY = 11; val posZ = 12; val scaleX = 13; val scaleY = 14; val rotY = 16; val op = 18; val trk = 19; val blr = 20; val smooth = 32
        val shape = selector(5)
        val easyStart = listOf(0 to 0f, 45 to 100f)

        // Ponte de velocidade e influência: Easy Ease do AE (33,33 %, velocidade 0).
        val handles = FloatArray(4)
        assertTrue(com.aurea.aurea.engine.AureaEngine.nativeTemporalEase(true, 0f, 33.3333f, 0f, 33.3333f, 1.5f, 100f, handles))
        assertEquals(0.3333f, handles[0], 1e-3f); assertEquals(0f, handles[1], 1e-5f)
        assertEquals(0.6667f, handles[2], 1e-3f); assertEquals(1f, handles[3], 1e-5f)
        assertFalse(com.aurea.aurea.engine.AureaEngine.nativeTemporalEase(true, 10f, 50f, 10f, 50f, 1.5f, 0f, handles))

        var id = row(0, position or opacity, font)              // Fade Up
        set(id, posY, 60f); set(id, op, 0f); field(id, shape, 1f); keys(id, start, easyStart, true)
        // Agrupamento e alinhamento: campos novos do painel.
        val grouped = row(7, scale, font)
        field(grouped, 3, 1f)
        set(grouped, 56, 50f); set(grouped, 57, -25f)
        assertEquals(1f, engine.textAnimatorFields(grouped, 0)!![3], 0f)
        assertEquals(50f, engine.textAnimValues(grouped, 0, intArrayOf(56))!![0], 1e-3f)
        assertEquals(-25f, engine.textAnimValues(grouped, 0, intArrayOf(57))!![0], 1e-3f)
        id = row(1, opacity, font)                               // Typewriter
        set(id, op, 0f); field(id, shape, 0f); set(id, smooth, 0f); keys(id, start, easyStart, false)
        id = row(2, opacity or blur or scale, font)              // Smooth Reveal
        set(id, op, 0f); set(id, blr, 12f); set(id, scaleX, 130f); set(id, scaleY, 130f); field(id, shape, 1f)
        set(id, easeHigh, 50f); set(id, easeLow, 50f); keys(id, start, easyStart, true)
        id = row(3, tracking or opacity, font)                   // Tracking Reveal
        set(id, op, 0f); set(id, trk, 150f); field(id, shape, 0f); keys(id, amount, listOf(0 to 100f, 45 to 0f), true)
        id = row(4, position, font)                              // Wave
        set(id, posY, -60f); field(id, shape, 4f); set(id, end, 25f); keys(id, offset, listOf(0 to -25f, 45 to 100f), false)
        id = row(5, position, font)                              // Bounce por expressão
        set(id, posY, -150f)
        assertEquals(1, engine.addTextSelector(id, 0, 3))
        val source = "t = time - (textIndex - 1) * 0.04;\nif (t < 0) { 100 } else { Math.abs(Math.cos(t * 10)) * Math.exp(-3 * t) * 100 }"
        val diag = engine.setExpression(id, (38..40).map { TrackKey(TrackProperty.TEXT_ANIM_PARAM, 0, 64 + it) }, source)
        File(folder, "expr.txt").writeText("$diag\n")
        assertTrue("$diag", diag?.ok == true)
        id = row(6, position or rotation or opacity, font)       // 3D por caractere
        set(id, posZ, -200f); set(id, rotY, 90f); set(id, op, 0f); field(id, shape, 1f); keys(id, start, easyStart, true)
        compose.runOnIdle { store.select(id, openOptions = false) }

        // Chave fora de 0 e 45: sobra do cabeçote ainda a caminho. A folha precisa ser igual à do AE.
        val log = StringBuilder()
        for (layer in store.layers) for (k in store.keyframes[layer.id].orEmpty().filter { it.property == TrackProperty.TEXT_ANIM_PARAM && it.time != 0 && it.time != 45 }) {
            log.appendLine("removida ${layer.id} $k")
            compose.runOnIdle { store.pause(); store.seek(k.time) }
            compose.waitUntil(5000) { store.playhead == k.time }
            Thread.sleep(250)
            compose.runOnIdle { assertTrue(engine.toggleTextAnimKey(layer.id, 0, k.paramIndex)) }
        }
        File(folder, "values.txt").writeText(log.toString())
        for (frame in intArrayOf(0, 8, 15, 23, 30, 38, 45, 59)) {
            compose.runOnIdle { store.pause(); store.seek(frame) }
            compose.waitForIdle()
            Thread.sleep(700)
            val pixels = ByteBuffer.allocateDirect(1600 * 1040 * 4)
            val dims = IntArray(2)
            assertEquals(1600 * 1040 * 4, engine.captureFrame(1600, pixels, dims))
            val bytes = ByteArray(pixels.capacity()).also { pixels.rewind(); pixels.get(it) }
            val bitmap = Bitmap.createBitmap(1600, 1040, Bitmap.Config.ARGB_8888)
            val colors = IntArray(1600 * 1040) { index ->
                val i = index * 4
                Color.argb(255, bytes[i].toInt() and 255, bytes[i + 1].toInt() and 255, bytes[i + 2].toInt() and 255)
            }
            bitmap.setPixels(colors, 0, 1600, 0, 0, 1600, 1040)
            File(folder, "aurea_%02d.png".format(frame)).outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
            bitmap.recycle()
        }
    }
}
