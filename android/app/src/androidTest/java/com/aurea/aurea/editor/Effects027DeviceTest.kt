package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer

/**
 * 0.2.7: Aurora (Gerar) e Edições aleatórias (Tempo) no aparelho — catálogo,
 * pilha da camada e o render Vulkan de verdade. Captura em
 * files/effects-027/ (o app instalado não é tocado: pacote .uitest).
 */
class Effects027DeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun auroraLightsTheSkyAndRandomEditsJoinsTheStack() {
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
        val directory = File(context.getExternalFilesDir(null), "effects-027").apply { mkdirs() }
        compose.runOnIdle { store.newProject(320, 180, 30f, "Aurora 027") }
        compose.waitUntil(10000) { store.project.title == "Aurora 027" }
        val black = ByteBuffer.allocateDirect(320 * 180 * 4)
        for (i in 0 until 320 * 180) { black.put(0); black.put(0); black.put(0); black.put(255.toByte()) }
        black.rewind()
        val sourceFile = File(directory, "noite.png")
        Bitmap.createBitmap(320, 180, Bitmap.Config.ARGB_8888).also { bmp ->
            bmp.copyPixelsFromBuffer(black); black.rewind()
            sourceFile.outputStream().use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
            bmp.recycle()
        }
        val layer = store.engineForStress.importImage(black, 320, 180, "Noite", sourceFile.absolutePath)
        assertTrue(layer >= 0)
        compose.runOnIdle { store.select(layer) }
        compose.waitUntil(10000) { store.primary == layer && store.detail != null }

        val aurora = effectTypeId("aurea.generate.aurora")
        assertTrue("Aurora no catálogo", aurora != 0)
        compose.runOnIdle { store.addEffect(aurora) }
        compose.waitUntil(10000) { store.effects.any { it.typeId == aurora && it.known } }
        compose.waitForIdle()
        Thread.sleep(500)
        val picture = checkNotNull(store.captureBitmap(320)) { "sem imagem do motor" }
        File(directory, "aurora.png").outputStream().use { picture.compress(Bitmap.CompressFormat.PNG, 100, it) }
        // Céu (metade de cima): verde-ciano aceso, não branco nem preto.
        var red = 0L; var green = 0L; var blue = 0L; var n = 0
        for (y in 5 until picture.height / 2 - 5) for (x in 0 until picture.width step 2) {
            val p = picture.getPixel(x, y); red += Color.red(p); green += Color.green(p); blue += Color.blue(p); ++n
        }
        val r = red / n; val g = green / n; val b = blue / n
        assertTrue("o céu acende (g=$g)", g > 12)
        assertTrue("verde-ciano, não branco (r=$r g=$g b=$b)", g > r * 2)

        val random = effectTypeId("aurea.time.random_edits")
        assertTrue("Edições aleatórias no catálogo", random != 0)
        compose.runOnIdle { store.addEffect(random) }
        compose.waitUntil(10000) { store.effects.any { it.typeId == random && it.known } }
        assertEquals(2, store.effects.size)
    }
}
