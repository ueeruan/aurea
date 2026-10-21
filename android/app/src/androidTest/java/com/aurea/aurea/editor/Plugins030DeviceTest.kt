package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import android.net.Uri
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.effects.PluginTexts
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File

/**
 * 0.3.0: plugins de efeito no aparelho — os exemplos de docs/plugins instalados
 * pelo caminho da interface (cópia + compilar na GPU do aparelho + quadro de
 * teste), o efeito na categoria Plugins com o nome no idioma do app, aplicado
 * mudando o quadro, e a remoção. Pacote .uitest: a pasta de plugins é a dele.
 */
class Plugins030DeviceTest {
    @get:Rule val compose = createComposeRule()

    private fun asset(name: String): Uri {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val dir = File(instrumentation.targetContext.cacheDir, "plugin-assets").apply { mkdirs() }
        val out = File(dir, name)
        instrumentation.context.assets.open("plugins/$name").use { input -> out.outputStream().use { input.copyTo(it) } }
        return Uri.fromFile(out)
    }

    @Test fun examplePluginsInstallRenderAndRemove() {
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
        // Sobra de outra execução: começa sem os exemplos.
        compose.runOnIdle { store.plugins.map { it.id }.forEach(store::removePlugin) }
        compose.runOnIdle { store.newProject(160, 90, 30f, "Plugins 030") }
        compose.waitUntil(10000) { store.project.title == "Plugins 030" && !store.projectOperationBusy }

        // Shader (rgb-wave), combinação (dream-look) e ISF (.fs).
        for (name in listOf("rgb-wave.aureaplugin", "dream-look.aureaplugin", "pulse-vignette.fs")) {
            val before = store.plugins.size
            compose.runOnIdle { store.installPlugin(asset(name)) }
            compose.waitUntil(30000) { store.plugins.size == before + 1 && store.busyMessage == null }
            assertNull("sem erro ao instalar $name: ${store.errorMessage}", store.errorMessage)
        }
        assertTrue(store.plugins.all { it.ok })
        val wave = store.plugins.first { it.id == "com.aurea.examples.rgbwave" }
        // O catálogo mostra o efeito na categoria Plugins; o nome sai do plugin.json.
        assertTrue(store.catalog.any { it.typeId == wave.effectTypeId && it.category == "Plugins" })
        assertEquals("RGB Wave", PluginTexts.name(wave.effectTypeId, "en"))

        // Um quadrado branco no meio: a onda RGB separa vermelho e azul na borda.
        val buf = java.nio.ByteBuffer.allocateDirect(160 * 90 * 4)
        for (y in 0 until 90) for (x in 0 until 160) {
            val inside = x in 60 until 100 && y in 25 until 65
            val v = if (inside) 255.toByte() else 0
            buf.put(v); buf.put(v); buf.put(v); buf.put(255.toByte())
        }
        buf.rewind()
        val layer = store.engineForStress.importImage(buf, 160, 90, "Quadrado", "test://quadrado-branco")
        assertTrue(layer >= 0)
        compose.runOnIdle { store.select(layer) }
        compose.waitUntil(10000) { store.primary == layer && store.detail != null }
        fun colouredEdge(): Int {
            compose.waitForIdle(); Thread.sleep(400)
            val picture: Bitmap = checkNotNull(store.captureBitmap(160)) { "sem imagem do motor" }
            var n = 0
            for (y in 0 until picture.height) for (x in 0 until picture.width) {
                val p = picture.getPixel(x, y)
                if (kotlin.math.abs(Color.red(p) - Color.blue(p)) > 80) n++
            }
            return n
        }
        val plain = colouredEdge()
        compose.runOnIdle { store.addEffect(wave.effectTypeId) }
        compose.waitUntil(10000) { store.effects.any { it.typeId == wave.effectTypeId } }
        compose.waitUntil(10000) { (store.effectParams[store.effects.first { it.typeId == wave.effectTypeId }.effectId]?.size ?: 0) >= 6 }
        val split = colouredEdge()
        assertTrue("o plugin muda o quadro (franjas coloridas: $plain → $split)", split > plain + 20)

        // Remover: o efeito some do catálogo; o projeto guarda a instância (desconhecida).
        compose.runOnIdle { store.plugins.map { it.id }.forEach(store::removePlugin) }
        compose.waitUntil(5000) { store.plugins.isEmpty() }
        assertFalse(store.catalog.any { it.category == "Plugins" })
    }
}
