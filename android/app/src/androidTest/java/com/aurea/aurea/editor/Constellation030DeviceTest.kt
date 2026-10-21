package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer

/**
 * 0.3.0: Constelação (3D) no aparelho — catálogo, os 65 parâmetros na pilha
 * (antes a interface lia só 64), os textos traduzidos e o render Vulkan.
 * Captura em files/effects-030/ (pacote .uitest).
 */
class Constellation030DeviceTest {
    @get:Rule val compose = createComposeRule()

    @Test fun constellationDrawsTheNetworkAndShowsAllItsControls() {
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
        val directory = File(context.getExternalFilesDir(null), "effects-030").apply { mkdirs() }
        compose.runOnIdle { store.newProject(320, 180, 30f, "Constelação 030") }
        compose.waitUntil(10000) { store.project.title == "Constelação 030" }
        val black = ByteBuffer.allocateDirect(320 * 180 * 4)
        for (i in 0 until 320 * 180) { black.put(0); black.put(0); black.put(0); black.put(255.toByte()) }
        black.rewind()
        val layer = store.engineForStress.importImage(black, 320, 180, "Fundo", "test://constelacao-fundo")
        assertTrue(layer >= 0)
        compose.runOnIdle { store.select(layer) }
        compose.waitUntil(10000) { store.primary == layer && store.detail != null }

        val type = effectTypeId("aurea.generate.constellation")
        assertTrue("Constelação no catálogo", store.catalog.any { it.typeId == type && it.category == "3D" })
        compose.runOnIdle { store.addEffect(type) }
        compose.waitUntil(10000) { store.effects.any { it.typeId == type && it.known } }
        val fx = store.effects.first { it.typeId == type }
        compose.waitUntil(10000) { (store.effectParams[fx.effectId]?.size ?: 0) >= 65 }
        val params = store.effectParams[fx.effectId].orEmpty()
        // O último (64, campo de visão) chega e os rótulos vêm do idioma do app.
        assertNotNull(params.firstOrNull { it.index == 64 })
        assertEquals(context.getString(R.string.fx030c_count), params.first { it.index == 1 }.label)
        compose.waitForIdle()
        Thread.sleep(500)
        val picture = checkNotNull(store.captureBitmap(320)) { "sem imagem do motor" }
        File(directory, "constelacao.png").outputStream().use { picture.compress(Bitmap.CompressFormat.PNG, 100, it) }
        // Pontos e linhas claros sobre o preto: bastante pixel aceso, mas não o quadro todo.
        var lit = 0; var n = 0
        for (y in 0 until picture.height step 2) for (x in 0 until picture.width step 2) {
            val p = picture.getPixel(x, y); if (Color.red(p) + Color.green(p) + Color.blue(p) > 120) ++lit; ++n
        }
        assertTrue("a rede aparece (acesos=$lit de $n)", lit > n / 200)
        assertTrue("não cobre o quadro inteiro (acesos=$lit de $n)", lit < n / 2)
    }
}
