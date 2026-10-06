package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import kotlin.math.abs

/** Real Vulkan captures and persisted projects; the main installed app is untouched. */
class PaperEffectsDeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun fourDeformationsRenderDisableAndReopenWithoutChangingTheirPixels() {
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
        val directory = File(context.getExternalFilesDir(null), "paper-effects").apply { mkdirs() }
        val report = StringBuilder()
        val engine = store.engineForStress
        fun commands(block: CommandBatch.() -> Unit) = compose.runOnIdle {
            engine.beginCommandBatch(); CommandBatch(engine).block()
            assertTrue(engine.submitCommands() > 0)
        }
        fun picture(name: String): Bitmap {
            val result = checkNotNull(store.captureBitmap(320)) { "Missing native image: $name" }
            assertEquals(320, result.width); assertEquals(180, result.height)
            File(directory, "$name.png").outputStream().use { result.compress(Bitmap.CompressFormat.PNG, 100, it) }
            return result
        }
        fun changed(a: Bitmap, b: Bitmap): Int {
            var count = 0
            for (y in 0 until 180) for (x in 0 until 320) {
                val p = a.getPixel(x, y); val q = b.getPixel(x, y)
                if (abs(Color.red(p) - Color.red(q)) + abs(Color.green(p) - Color.green(q)) + abs(Color.blue(p) - Color.blue(q)) > 8) ++count
            }
            return count
        }
        for ((name, amount) in listOf("bender" to 35f, "bend" to 65f, "curl" to 145f, "page_turn" to 50f)) {
            compose.runOnIdle { store.newProject(320, 180, 30f, "Paper $name") }
            compose.waitUntil(10000) { store.project.title == "Paper $name" }
            val source = ByteBuffer.allocateDirect(320 * 180 * 4)
            for (y in 0 until 180) for (x in 0 until 320) {
                source.put((if (x < 160) 240 else 32).toByte())
                source.put((if ((x / 20 + y / 20) % 2 == 0) 40 else 200).toByte())
                source.put((if (x < 160) 32 else 240).toByte())
                source.put((if (y in 80..99) 0 else 255).toByte())
            }
            source.rewind()
            // Reopening resolves media by its persisted source, just like an
            // image imported from the gallery. An anonymous RAM image has none.
            val sourceFile = File(directory, "$name-source.png")
            val sourceBitmap = Bitmap.createBitmap(320, 180, Bitmap.Config.ARGB_8888)
            sourceBitmap.copyPixelsFromBuffer(source)
            sourceFile.outputStream().use { sourceBitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
            sourceBitmap.recycle(); source.rewind()
            val layer = engine.importImage(source, 320, 180, "Paper source", sourceFile.absolutePath)
            assertTrue(layer >= 0)
            compose.runOnIdle { store.select(layer) }
            compose.waitUntil(10000) { store.primary == layer && store.detail != null }
            val original = picture("$name-original")
            val type = effectTypeId("aurea.distort.$name")
            assertTrue("Effect missing from the native catalog: $name", store.catalog.any { it.typeId == type })
            compose.runOnIdle { store.addEffect(type) }
            compose.waitUntil(10000) { store.effects.any { it.typeId == type && it.known } }
            val effect = store.effects.first { it.typeId == type }.effectId
            commands {
                setEffectParam(layer, effect, 0, amount)
                if (name == "curl") setEffectParam(layer, effect, 1, 32f)
                if (name == "page_turn") setEffectParam(layer, effect, 2, 16f)
            }
            val project = File(directory, "$name.aurea")
            assertEquals(0, engine.saveProject(project.absolutePath))
            val deformed = picture("$name-deformed")
            val difference = changed(original, deformed)
            assertTrue("Effect did not change the native picture: $name ($difference pixels)", difference > 400)
            assertEquals(0, engine.loadProject(project.absolutePath))
            val reopened = picture("$name-reopened")
            assertEquals("Persisted $name changed its pixels", 0, changed(deformed, reopened))
            commands { setEffectParam(layer, effect, 0, 0f) }
            val identity = picture("$name-zero")
            assertEquals("Zero must preserve the source for $name", 0, changed(original, identity))
            report.append("$name changedPixels=$difference persistence=PASS zero=PASS\n")
            original.recycle(); deformed.recycle(); reopened.recycle(); identity.recycle()
        }
        File(directory, "result.txt").writeText(report.toString())
    }
}
