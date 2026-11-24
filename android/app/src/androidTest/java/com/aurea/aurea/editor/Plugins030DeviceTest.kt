package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import android.net.Uri
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.effects.PluginTexts
import com.aurea.aurea.effects.effectCardId
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

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

    @Test fun sceneMenuRunsAUserScriptAgainstTheSelectedModel() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        val id = "aurea.tests.scene-panel"
        val archive = File(context.cacheDir, "scene-action.aureaplugin")
        ZipOutputStream(archive.outputStream()).use { zip ->
            fun add(name: String, content: String) {
                zip.putNextEntry(ZipEntry(name))
                zip.write(content.toByteArray(Charsets.UTF_8))
                zip.closeEntry()
            }
            add("plugin.json", """
                {"aureaPlugin":3,"id":"$id","name":"Scene action","version":"1.0.0","kind":"panel","script":"main.js",
                 "ui":{"panels":[{"id":"tools","title":"Scene tools","placement":["scene.panel"],
                 "controls":[{"id":"applyScene","type":"button","label":"Apply","onClick":"runScene"}]}],
                 "actions":[{"id":"open","title":"Open scene tools","placement":"scene.menu","needsLayer":true,"panel":"tools"}]}}
            """.trimIndent())
            add("main.js", """
                function runScene() {
                  const layer = app.project.activeItem.selectedLayers[0];
                  const effect = layer.addEffect("aurea.3d.replicator");
                  effect.property("count").setValue(7);
                }
            """.trimIndent())
        }
        compose.runOnIdle {
            if (store.plugins.any { it.id == id }) store.removePlugin(id)
            store.newProject(320, 240, 30f, "Scene plugin test")
        }
        compose.waitUntil(20000) { store.project.title == "Scene plugin test" && !store.projectOperationBusy }
        compose.runOnIdle { store.installPlugin(Uri.fromFile(archive)) }
        compose.waitUntil(20000) { store.plugins.any { it.id == id && it.ok } && store.busyMessage == null }
        compose.runOnIdle {
            assertTrue(store.addShape3D(0) > 0)
            store.enterSceneEditor()
        }
        compose.waitUntil(10000) { store.sceneEditor && store.detail?.kind == 10 }
        compose.onNodeWithTag("scene.addons").performScrollTo().performClick()
        compose.onNodeWithTag("scene.plugin.action.$id.open").performScrollTo().performClick()
        compose.onNodeWithTag("plugin.ctl.applyScene").assertIsDisplayed().performClick()
        val repeatType = effectTypeId("aurea.3d.replicator")
        compose.waitUntil(10000) {
            val effect = store.effects.firstOrNull { it.typeId == repeatType }
            effect != null && store.effectParams[effect.effectId]?.firstOrNull()?.value?.firstOrNull() == 7f
        }
        assertEquals(1, store.layers.size)
        compose.runOnIdle { store.removePlugin(id) }
    }

    @Test fun textureParametersAtSlot1023ReachTheGpuAndPresetsUpdateTheEditor() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        check(context.packageName.endsWith(".uitest"))
        fun captureBrowser(name: String) {
            if (InstrumentationRegistry.getArguments().getString("aureaCaptureEffectsUi") != "true") return
            val image = checkNotNull(instrumentation.uiAutomation.takeScreenshot())
            try {
                File(context.filesDir, "$name.png").outputStream().use { image.compress(Bitmap.CompressFormat.PNG, 100, it) }
            } finally { image.recycle() }
        }
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        val pluginId = "aurea.tests.texture-parameters"
        val params = List(31) { """{"id":"gradient$it","type":"gradient"}""" } +
            """{"id":"timing","type":"curve"}""" +
            List(14) { """{"id":"unused$it","type":"float"}""" } +
            """{"id":"colorOn","type":"bool","default":false}""" +
            """{"id":"intensity","type":"float","default":0.25,"visible_if":{"param":"colorOn","equals":true}}"""
        val pluginFile = File(context.cacheDir, "texture-parameters.aureaplugin")
        ZipOutputStream(pluginFile.outputStream()).use { zip ->
            fun add(name: String, content: String) {
                zip.putNextEntry(ZipEntry(name))
                zip.write(content.toByteArray(Charsets.UTF_8))
                zip.closeEntry()
            }
            add("plugin.json", """
                {"aureaPlugin":3.1,"id":"$pluginId","name":"Texture parameters","version":"1.0.0",
                 "kind":"shader","parameterStorage":"texture","uses":["history"],
                 "params":[${params.joinToString(",")}],
                 "presets":[{"id":"bright","name":"Bright","set":{"colorOn":true,"intensity":0.75}}],
                 "passes":[{"shader":"bright.frag","target":"glow","scale":0.5},{"shader":"final.frag","inputs":{"glow":"glow"}}]}
            """.trimIndent())
            add("bright.frag", "vec4 aurea_main(vec2 uv) { return vec4(4.0 * intensity, 0.0, 0.0, 1.0); }")
            add("final.frag", "vec4 aurea_main(vec2 uv) { return vec4(texture(glow, uv).r * 0.25, texture(u_prev_frame, uv).g, 0.0, 1.0); }")
        }
        compose.runOnIdle {
            store.plugins.firstOrNull { it.id == pluginId }?.let { store.removePlugin(it.id) }
            store.newProject(160, 90, 30f, "Plugin V3.1")
        }
        compose.waitUntil(15000) { store.project.title == "Plugin V3.1" && !store.projectOperationBusy }
        val source = java.nio.ByteBuffer.allocateDirect(160 * 90 * 4)
        repeat(160 * 90) { source.put(0); source.put(0); source.put(0); source.put(255.toByte()) }
        source.rewind()
        val layer = store.engineForStress.importImage(source, 160, 90, "Source", "test://plugin-v31")
        assertTrue(layer >= 0)
        compose.runOnIdle { store.select(layer) }
        compose.waitUntil(10000) { store.primary == layer && store.detail != null }
        compose.onNodeWithTag("layer.panel.effects").performClick()
        compose.onNodeWithTag("aurea.effects.add").performClick()
        compose.onNodeWithTag("effects.tab.catalog").assertIsDisplayed()
        captureBrowser("audit-effects-catalog")
        compose.onNodeWithTag("effects.plugins").assertIsDisplayed().performClick()
        compose.onNodeWithTag("plugins.install").assertIsDisplayed()
        compose.runOnIdle { store.installPlugin(Uri.fromFile(pluginFile)) }
        compose.waitUntil(45000) { store.plugins.any { it.id == pluginId } && store.busyMessage == null }
        val plugin = store.plugins.single { it.id == pluginId }
        assertTrue(plugin.ok)
        val pluginAction = "plugins.effect.$pluginId"
        compose.onNodeWithTag("plugins.sheet").performScrollToNode(hasTestTag(pluginAction))
        compose.onNodeWithTag(pluginAction).assertIsDisplayed()
        captureBrowser("audit-effects-plugins")
        compose.onNodeWithTag(pluginAction).performClick()

        compose.waitUntil(15000) {
            val effect = store.effects.firstOrNull { it.typeId == plugin.effectTypeId }
            effect != null && store.effectParams[effect.effectId]?.size == params.size
        }
        val effect = store.effects.single { it.typeId == plugin.effectTypeId }.effectId
        fun intensity() = store.effectParams.getValue(effect).last()
        fun redChannel(): Int {
            compose.waitForIdle()
            val image = checkNotNull(store.captureBitmap(160))
            return Color.red(image.getPixel(image.width / 2, image.height / 2))
        }
        val hiddenFlag = com.aurea.aurea.engine.ParamType.FLAG_HIDDEN
        assertTrue(intensity().flags and hiddenFlag != 0)
        val before = redChannel()
        assertTrue("Slot 1023, linear red 0.25 → $before", before in 125..150)
        compose.runOnIdle {
            assertEquals("bright", store.effectPresets(plugin.effectTypeId).single().first)
            store.applyEffectPreset(effect, 0)
        }
        compose.waitUntil(10000) { intensity().flags and hiddenFlag == 0 && intensity().value[0] == 0.75f }
        val after = redChannel()
        assertTrue("Slot 1023, HDR intermediate 3.0 → linear red 0.75 → $after", after in 210..237)
    }

    @Test fun audioGraphInstallsPersistsAndExportsThroughTheNativeMixer() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        check(context.packageName.endsWith(".uitest"))
        lateinit var store: EditorStore
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
        compose.runOnIdle { store.newProject(320, 180, 30f, "Audio DSP 1.0") }
        compose.waitUntil(15000) { store.project.title == "Audio DSP 1.0" && !store.projectOperationBusy }
        val video = File(context.filesDir, "audio-plugin-source.mp4")
        instrumentation.context.assets.open("video-with-tone.mp4").use { input -> video.outputStream().use { input.copyTo(it) } }
        compose.runOnIdle { store.importVideo(Uri.fromFile(video)) }
        compose.waitUntil(30000) { store.layers.any { it.kind == 1 } && store.busyMessage == null }
        val layer = store.layers.first { it.kind == 1 }.id
        compose.runOnIdle { store.setCompositionDuration(30); store.select(layer) }
        compose.waitUntil(10000) { store.project.durationFrames == 30 }

        fun export(name: String) {
            val output = File(context.filesDir, name)
            if (output.exists()) check(output.delete())
            var start = -1
            compose.runOnIdle { start = store.engineForStress.startExport(output.absolutePath, 180, 30.0, 0, 8) }
            assertEquals("Native export failed to start", 0, start)
            val bytes = java.nio.ByteBuffer.allocateDirect(128).order(java.nio.ByteOrder.nativeOrder())
            val progress = com.aurea.aurea.engine.ExportProgress()
            compose.waitUntil(120000) { store.engineForStress.exportProgress(bytes) && progress.readFrom(bytes).let { progress.finished } }
            assertEquals("Native export failed", 0, progress.result)
            assertTrue(output.length() > 500)
        }
        export("audio-plugin-dry.mp4")
        compose.runOnIdle {
            store.plugins.firstOrNull { it.id == "aurea.example.audio-delay" }?.let { store.removePlugin(it.id) }
            store.installPlugin(asset("audio-delay.aureaplugin"))
        }
        compose.waitUntil(30000) { store.plugins.any { it.id == "aurea.example.audio-delay" } && store.busyMessage == null }
        val plugin = store.plugins.single { it.id == "aurea.example.audio-delay" }
        assertTrue(plugin.ok)
        assertEquals("audio", plugin.kind)
        compose.runOnIdle { store.select(layer); store.addEffect(plugin.effectTypeId) }
        compose.waitUntil(10000) { store.effects.any { it.typeId == plugin.effectTypeId } }
        val effect = store.effects.single { it.typeId == plugin.effectTypeId }
        compose.waitUntil(10000) { store.effectParams[effect.effectId]?.size == 3 }
        compose.runOnIdle { store.setEffectParam(effect.effectId, store.effectParams.getValue(effect.effectId).first { it.index == 2 }, .8f) }
        compose.waitUntil(10000) { store.effectParams[effect.effectId]?.firstOrNull { it.index == 2 }?.value?.firstOrNull() == .8f }
        val saved = File(context.filesDir, "audio-plugin-roundtrip.aurea")
        compose.runOnIdle {
            assertEquals(0, store.engineForStress.saveProject(saved.absolutePath))
            assertEquals(0, store.engineForStress.loadProject(saved.absolutePath))
            store.select(layer)
        }
        compose.waitUntil(10000) { store.effects.any { it.typeId == plugin.effectTypeId } }
        assertEquals(.8f, store.effectParams.getValue(effect.effectId).first { it.index == 2 }.value[0], 1e-6f)
        export("audio-plugin-wet.mp4")
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
