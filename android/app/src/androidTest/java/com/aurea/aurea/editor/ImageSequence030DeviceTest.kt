package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import android.net.Uri
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File

/**
 * 0.3.0: sequência de imagens no aparelho — quadros PNG com alfa (como um render
 * do Blender), um buraco na numeração, a sondagem, a cópia para
 * projetos/Media/sequences/ e o clipe na timeline mostrando cada quadro por cima
 * do fundo (a transparência fica). Cancelar apaga a cópia.
 */
class ImageSequence030DeviceTest {
    @get:Rule val compose = createComposeRule()

    /** Quadro i: fundo transparente e um quadrado vermelho que anda para a direita. */
    private fun frame(dir: File, i: Int) {
        val bmp = Bitmap.createBitmap(160, 90, Bitmap.Config.ARGB_8888)
        bmp.eraseColor(Color.TRANSPARENT)
        for (y in 30 until 60) for (x in 0 until 30) bmp.setPixel(10 + i * 10 + x, y, Color.RED)
        File(dir, "render_%04d.png".format(i)).outputStream().use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
        bmp.recycle()
    }

    @Test fun pngSequenceWithAlphaImportsAsAMovingClipAndCancelCleansUp() {
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
        compose.runOnIdle { store.newProject(160, 90, 24f, "Sequência 030") }
        compose.waitUntil(10000) { store.project.title == "Sequência 030" && !store.projectOperationBusy }
        // Fundo azul opaco embaixo: a transparência da sequência deixa ele aparecer.
        val blue = java.nio.ByteBuffer.allocateDirect(160 * 90 * 4)
        repeat(160 * 90) { blue.put(0); blue.put(0); blue.put(255.toByte()); blue.put(255.toByte()) }
        blue.rewind()
        assertTrue(store.engineForStress.importImage(blue, 160, 90, "Fundo", "test://fundo-azul") >= 0)

        val source = File(context.cacheDir, "seq-src-030").apply { deleteRecursively(); mkdirs() }
        for (i in 1..8) if (i != 5) frame(source, i)   // o 5 falta
        val uris = source.listFiles()!!.sortedBy { it.name }.map { Uri.fromFile(it) }

        // Cancelar: a cópia sai e nada entra.
        compose.runOnIdle { store.prepareImageSequence(uris) }
        compose.waitUntil(20000) { store.sequenceDraft != null }
        val first = store.sequenceDraft!!
        assertEquals(8, first.info.optInt("frameCount"))
        assertEquals(1, first.info.optInt("missingCount"))
        assertTrue(first.info.optBoolean("hasAlpha"))
        val copied = File(first.paths.single())
        assertTrue(copied.isDirectory && copied.listFiles()!!.size == 7)
        compose.runOnIdle { store.cancelImageSequence() }
        compose.waitUntil(5000) { !copied.exists() }
        assertEquals(1, store.layers.size)

        // Importar: um clipe de 8 quadros a 24 qps.
        compose.runOnIdle { store.prepareImageSequence(uris) }
        compose.waitUntil(20000) { store.sequenceDraft != null }
        compose.runOnIdle { store.importImageSequence(24.0, 0, 0) }
        compose.waitUntil(20000) { store.layers.size == 2 && store.busyMessage == null }
        val clip = store.layers.first { it.name != "Fundo" }
        assertEquals(8, clip.endFrame - clip.startFrame)

        fun redColumn(): Int {
            compose.waitForIdle(); Thread.sleep(400)
            val picture = checkNotNull(store.captureBitmap(160)) { "sem imagem do motor" }
            var sum = 0; var n = 0
            for (x in 0 until picture.width) {
                val p = picture.getPixel(x, picture.height / 2)
                if (Color.red(p) > 150 && Color.blue(p) < 100) { sum += x; n++ }
            }
            // Fora do quadrado, o azul do fundo aparece: o alfa foi respeitado.
            val corner = picture.getPixel(2, 2)
            assertTrue("fundo visível pela transparência (${Integer.toHexString(corner)})", Color.blue(corner) > 150 && Color.red(corner) < 100)
            return if (n == 0) -1 else sum / n
        }
        compose.runOnIdle { store.seek(clip.startFrame) }
        val atStart = redColumn()
        compose.runOnIdle { store.seek(clip.startFrame + 6) }
        val later = redColumn()
        assertTrue("o quadrado anda com os quadros ($atStart → $later)", atStart in 0 until later)
        source.deleteRecursively()
    }
}
