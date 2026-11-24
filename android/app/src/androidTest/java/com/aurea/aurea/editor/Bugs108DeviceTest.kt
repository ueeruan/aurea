package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.presets.fitsText
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.Screen
import com.aurea.aurea.ui.theme.AureaTheme
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import java.io.File

/**
 * 1.0.8, relatos da 1.0.7.1:
 *  - "não importa projeto do After Effects": o .aep era lido, mas a publicação
 *    do projeto novo usava hard link, que o Android nega a apps ("Não foi
 *    possível salvar o novo projeto do After Effects").
 *  - "criar projeto não abre" (Infinix Smart 9): com o motor ainda iniciando,
 *    criar projeto era recusado em silêncio; agora espera e abre sozinho.
 */
class Bugs108DeviceTest {
    @get:Rule val compose = createComposeRule()
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private lateinit var store: EditorStore

    private fun start(waitEngine: Boolean) {
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30_000) { ready && (!waitEngine || store.engineReady) }
    }

    /** O .aep de teste: `adb push rain.aep /data/local/tmp/` e `run-as … cp` para files/ae108/. */
    @Test fun aepProjectImportPublishesTheNewProject() {
        start(waitEngine = true)
        val input = File(context.filesDir, "ae108/rain.aep")
        assertTrue("faltou o .aep de teste em ${input.absolutePath}", input.isFile)
        val output = File(context.filesDir, "ae108/out-${System.nanoTime()}.aurea")
        val report = JSONObject(store.engineForStress.importAeProjectFile(input.absolutePath, output.absolutePath, "Rain 108"))
        assertTrue("importação: $report", report.optBoolean("ok"))
        assertTrue("o projeto novo não foi gravado: ${output.absolutePath}", output.isFile && output.length() > 0)
        // O nome já usado não é sobrescrito.
        val again = JSONObject(store.engineForStress.importAeProjectFile(input.absolutePath, output.absolutePath, "Rain 108"))
        assertTrue("um destino existente não pode ser sobrescrito", !again.optBoolean("ok"))
        output.delete()
    }

    /** "O salto suave sumiu": o preset 26 (0.4.4, palavra a palavra) vale de novo no texto 2D. */
    @Test fun classicSmoothBounceAppliesToTwoDText() {
        start(waitEngine = true)
        compose.runOnIdle { store.newProject(1080, 1920, 30f, "Salto 108") }
        compose.waitUntil(20_000) { store.screen == Screen.Editor && !store.projectOperationBusy }
        compose.runOnIdle { store.addText(); store.dismissTextContentEditor() }
        compose.waitUntil(10_000) { store.primary != null }
        val layer = checkNotNull(store.primary)
        assertTrue("o motor recusou o preset 26 no texto 2D",
            store.engineForStress.applyTextPreset(layer, com.aurea.aurea.presets.SMOOTH_BOUNCE_CLASSIC_PRESET))
        val animators = store.engineForStress.queryTextAnimators(layer)
        assertTrue("sem animadores depois do salto suave clássico", animators != null && animators.size >= 40)
    }

    /**
     * "Adicione aqueles presets de texto de novo (que eram do Juan)": os 8
     * presets do Juan (motor 11..18) servem de novo no texto 2D (fitsText) e,
     * aplicados pela loja, criam animadores na camada de texto 2D.
     */
    @Test fun juanTextPresetsApplyToTwoDTextThroughTheStore() {
        start(waitEngine = true)
        compose.runOnIdle { store.newProject(1080, 1920, 30f, "Juan 108") }
        compose.waitUntil(20_000) { store.screen == Screen.Editor && !store.projectOperationBusy }
        compose.runOnIdle { store.addText(); store.dismissTextContentEditor() }
        compose.waitUntil(10_000) { store.primary != null }
        val layer = checkNotNull(store.primary)
        val catalog = com.aurea.aurea.presets.BuiltinPresets(context).of(com.aurea.aurea.presets.PresetKind.Text)
        assertEquals(8, com.aurea.aurea.presets.JUAN_TEXT_PRESETS.count())
        for (preset in com.aurea.aurea.presets.JUAN_TEXT_PRESETS) {
            val entry = catalog.firstOrNull { it.textPreset == preset }
            assertTrue("o preset $preset do Juan sumiu do catálogo", entry != null)
            assertTrue("o preset $preset do Juan não aparece no texto 2D", entry!!.fitsText(false))
            assertTrue("o preset $preset do Juan não aparece no texto 3D", entry.fitsText(true))
            compose.runOnIdle { store.applyTextPreset(preset) }
            compose.waitForIdle()
            val raw = store.engineForStress.queryTextAnimators(layer)
            assertTrue("o preset $preset do Juan não criou animadores", raw != null && raw.size >= 40)
            val expected = raw!!.size / 40
            compose.waitUntil(5_000) { store.textAnimators.size == expected }
            assertEquals("a loja não leu os animadores do preset $preset", expected, store.textAnimators.size)
        }
    }

    /** 1.0.8: os presets de formas (borboletas … flocos, 29..36) criam a camada do Particular. */
    @Test fun shapeParticlePresetsCreateParticularLayers() {
        start(waitEngine = true)
        compose.runOnIdle { store.newProject(1080, 1920, 30f, "Formas 108") }
        compose.waitUntil(20_000) { store.screen == Screen.Editor && !store.projectOperationBusy }
        for (preset in 29..36) {
            var id = 0L
            compose.runOnIdle { id = store.addParticles(preset) }
            assertTrue("o preset $preset não criou camada", id > 0)
        }
    }

    @Test fun newProjectRequestedBeforeTheEngineIsReadyOpensWhenReady() {
        start(waitEngine = false)
        val title = "Antes do motor ${System.nanoTime() % 100000}"
        compose.runOnIdle { store.newProject(1080, 1920, 30f, title) }
        compose.waitUntil(90_000) { store.engineReady && store.screen == Screen.Editor && store.project.title == title }
        assertEquals(title, store.project.title)
        assertEquals(Screen.Editor, store.screen)
    }

    /** Um arquivo no cache do app (onde seletores, MCP e câmera deixam a mídia). */
    private fun tempMedia(video: Boolean): File {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val file = File(context.cacheDir, "temp108-${System.nanoTime()}" + if (video) ".mp4" else ".png")
        if (video) {
            instrumentation.context.assets.open("motion-fixture.mp4").use { input -> file.outputStream().use { input.copyTo(it) } }
        } else {
            val bitmap = android.graphics.Bitmap.createBitmap(48, 32, android.graphics.Bitmap.Config.ARGB_8888)
            try {
                bitmap.eraseColor(android.graphics.Color.MAGENTA)
                file.outputStream().use { assertTrue(bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it)) }
            } finally { bitmap.recycle() }
        }
        return file
    }

    private fun underCache(path: String?): Boolean {
        if (path == null) return false
        val canonical = File(path).canonicalPath
        return canonical.startsWith(context.cacheDir.canonicalPath + File.separator)
    }

    /**
     * 1.0.8 ("A mídia original não está mais no aparelho", erro 18): vídeo e
     * foto importados de um arquivo TEMPORÁRIO viram cópia do app. O temporário
     * é apagado (como o sistema faz com o cache), o projeto é reaberto e as
     * camadas continuam com mídia: nenhuma ausente na abertura, nenhuma camada
     * marcada e o quadro do vídeo ainda sai (a prévia toca).
     */
    @Test fun mediaImportedFromATempFileSurvivesItsDeletionAndReopen() {
        start(waitEngine = true)
        val title = "Midia 108 ${System.nanoTime() % 100000}"
        compose.runOnIdle { store.newProject(320, 180, 30f, title) }
        compose.waitUntil(20_000) { store.screen == Screen.Editor && store.project.title == title && !store.projectOperationBusy }
        val video = tempMedia(video = true)
        val photo = tempMedia(video = false)
        compose.runOnIdle { store.importMediaBatch(listOf(android.net.Uri.fromFile(video), android.net.Uri.fromFile(photo))) }
        compose.waitUntil(60_000) { store.busyMessage == null && store.layers.size == 2 }
        val imported = store.layers.map { it.id to store.layerSourcePath(it.id) }
        for ((id, source) in imported) {
            assertTrue("a camada $id ficou sem arquivo", source != null && File(source).isFile)
            assertTrue("a camada $id ainda aponta para o temporário: $source", !underCache(source))
        }
        val path = checkNotNull(store.project.path) { "projeto sem arquivo" }
        var saved = false
        compose.runOnIdle { store.saveProject { saved = true } }
        compose.waitUntil(20_000) { saved }
        // O sistema limpa o cache: o temporário some.
        assertTrue(video.delete() && photo.delete())
        compose.runOnIdle { store.openProject(path) }
        compose.waitUntil(30_000) { store.screen == Screen.Editor && !store.projectOperationBusy && store.layers.size == 2 }
        compose.waitForIdle()
        val notice = store.engineForStress.loadNotice()
        assertEquals("a abertura achou mídia ausente (aviso 0x${Integer.toHexString(notice)})", 0, notice and 8)
        for (layer in store.layers) {
            val source = store.layerSourcePath(layer.id)
            assertTrue("a camada ${layer.id} perdeu a mídia depois de reabrir: $source", source != null && File(source).isFile)
            assertEquals("a camada ${layer.id} ficou marcada como mídia ausente", 0,
                store.layerPanelBits(layer.id) and LayerPanelBits.MISSING_MEDIA)
            assertTrue("a camada ${layer.id} ficou como URI morta", !store.layerMediaUnreadable(layer.id))
        }
        val missingText = context.getString(com.aurea.aurea.R.string.msg_a_midia_original_nao_esta_mais)
        assertTrue("erro 18 depois de reabrir: ${store.errorMessage}", store.errorMessage?.contains(missingText) != true)
        // O vídeo ainda entrega quadro (o "toca"): a prévia do primeiro quadro sai.
        val clip = store.layers.single { it.kind == 1 }
        compose.runOnIdle { store.select(clip.id) }
        compose.waitForIdle()
        assertTrue("o vídeo não tem mais duração", clip.endFrame > clip.startFrame)
    }

    /**
     * 1.0.8: um projeto antigo que guardava a mídia no cache do app (até a
     * 1.0.7.1 o arquivo `file:` ficava onde estava) é migrado ao abrir: a mídia
     * vira cópia do app e o `.aurea` passa a apontar para ela.
     */
    @Test fun openingAnOldProjectCopiesCachedMediaIntoTheApp() {
        start(waitEngine = true)
        val title = "Migrar 108 ${System.nanoTime() % 100000}"
        compose.runOnIdle { store.newProject(320, 180, 30f, title) }
        compose.waitUntil(20_000) { store.screen == Screen.Editor && store.project.title == title && !store.projectOperationBusy }
        val photo = tempMedia(video = false)
        // Como a 1.0.7.1 gravava: o caminho do cache direto no motor.
        val bitmap = android.graphics.BitmapFactory.decodeFile(photo.absolutePath)
        val buffer = java.nio.ByteBuffer.allocateDirect(bitmap.width * bitmap.height * 4)
        bitmap.copyPixelsToBuffer(buffer); buffer.rewind()
        val id = store.engineForStress.importImage(buffer, bitmap.width, bitmap.height, "foto", photo.absolutePath, 1)
        bitmap.recycle()
        assertTrue("o motor recusou a foto ($id)", id >= 0)
        val path = checkNotNull(store.project.path)
        var saved = false
        compose.runOnIdle { store.saveProject { saved = true } }
        compose.waitUntil(20_000) { saved }
        val copied = store.migrateProjectMedia(path)
        assertEquals("a foto do cache não foi religada à cópia do app", 1, copied)
        val refs = checkNotNull(store.engineForStress.projectFileMedia(path))
        assertTrue("o projeto ainda aponta para o cache: ${refs.toList()}", (0 until refs.size / 4).none { underCache(refs[it * 4]) })
        assertTrue(photo.delete())
        compose.runOnIdle { store.openProject(path) }
        compose.waitUntil(30_000) { store.screen == Screen.Editor && !store.projectOperationBusy && store.layers.isNotEmpty() }
        compose.waitForIdle()
        assertEquals(0, store.engineForStress.loadNotice() and 8)
    }
    /**
     * 1.0.8 Edição automática: um clipe curto (vídeo com tom, 3 s) e a fala
     * injetada no lugar do Whisper. O motor monta uma composição NOVA, mais
     * curta, com zoom (keyframes), sons, a música da biblioteca do usuário e
     * legendas; o clipe original continua com o mesmo tempo, efeitos e keys.
     */
    @Test fun autoEditBuildsShorterCompositionWithZoomSoundsMusicAndCaptions() {
        start(waitEngine = true)
        // A música vem da biblioteca do próprio usuário (gerada aqui: 20 s, 100 BPM).
        val music = File(context.cacheDir, "autoedit-trilha.wav")
        val samples = 48000 * 20
        music.writeBytes(java.nio.ByteBuffer.allocate(44 + samples * 2).order(java.nio.ByteOrder.LITTLE_ENDIAN).apply {
            put("RIFF".toByteArray()); putInt(capacity() - 8); put("WAVEfmt ".toByteArray())
            putInt(16); putShort(1); putShort(1); putInt(48000); putInt(96000); putShort(2); putShort(16)
            put("data".toByteArray()); putInt(samples * 2)
            repeat(samples) { i ->
                val t = i / 48000.0; val beat = t % 0.6
                val v = 0.15 * (kotlin.math.sin(2 * Math.PI * 220 * t) + kotlin.math.sin(2 * Math.PI * 277 * t)) +
                    0.5 * kotlin.math.exp(-beat * 18) * kotlin.math.sin(2 * Math.PI * 60 * beat)
                putShort((v * 20000).toInt().coerceIn(-32767, 32767).toShort())
            }
        }.array())
        SoundLibrary.importFiles(context, listOf(android.net.Uri.fromFile(music)), "Músicas 108") { _, _ -> }
        compose.runOnIdle { store.newProject(1080, 1920, 30f, "AutoEdit 108") }
        compose.waitUntil(20_000) { store.screen == Screen.Editor && !store.projectOperationBusy }
        val video = File(context.filesDir, "autoedit-108.mp4")
        InstrumentationRegistry.getInstrumentation().context.assets.open("video-with-tone.mp4").use { input -> video.outputStream().use { input.copyTo(it) } }
        compose.runOnIdle { store.importVideo(android.net.Uri.fromFile(video)) }
        compose.waitUntil(20_000) { store.layers.any { it.kind == 1 } }
        val original = store.layers.first { it.kind == 1 }
        // A fala: "Olá pessoal (ahn) … vamos lá." — a hesitação e a pausa longa saem.
        val words = listOf(
            com.aurea.aurea.captions.Word("Olá", 0.20, 0.50), com.aurea.aurea.captions.Word("pessoal", 0.55, 1.00),
            com.aurea.aurea.captions.Word("ahn", 1.20, 1.45), com.aurea.aurea.captions.Word("vamos", 2.10, 2.45),
            com.aurea.aurea.captions.Word("lá.", 2.50, 2.80),
        )
        compose.runOnIdle { store.autoEdit.open(original.id); store.autoEdit.start(words) }
        compose.waitUntil(120_000) { !store.autoEdit.running && (store.autoEdit.report != null || store.autoEdit.error != null) }
        val report = store.autoEdit.report
        assertTrue("a edição automática falhou: ${store.autoEdit.error}", report != null)
        report!!
        assertTrue("a cópia não ficou mais curta: $report", report.getDouble("editSeconds") < report.getDouble("sourceSeconds"))
        assertTrue("sem zoom: $report", report.getInt("zooms") >= 1)
        assertTrue("sem sons: $report", report.getInt("sounds") >= 1)
        assertTrue("sem música: $report", report.optString("music").isNotEmpty())
        assertTrue("sem legendas: $report", report.getInt("captions") >= 1)
        compose.runOnIdle { store.autoEdit.close() }
        // A composição nova aparece como uma camada no lugar do clipe; o original fica igual.
        compose.waitUntil(10_000) { store.layers.any { it.kind == 12 } }
        val group = store.layers.first { it.kind == 12 }
        assertTrue("nome da composição: ${group.name}", group.name.contains("edição automática"))
        assertTrue("a camada da edição não é mais curta", group.durationFrames < original.durationFrames)
        val after = store.layers.first { it.id == original.id }
        assertEquals(original.startFrame, after.startFrame)
        assertEquals(original.endFrame, after.endFrame)
        assertEquals(original.offsetFrames, after.offsetFrames)
        assertEquals(original.effectCount, after.effectCount)
        assertEquals(original.keyframeCount, after.keyframeCount)
        // Dentro: pedaços de vídeo com keyframes de zoom, sons, a música e a faixa de legendas.
        compose.runOnIdle { store.openPrecomp(group.id) }
        compose.waitUntil(10_000) { store.layers.any { it.kind == 3 } }
        val inner = store.layers
        assertTrue("sem keyframes de zoom no vídeo", inner.any { it.kind == 1 && it.keyframeCount > 0 })
        assertTrue("sem camadas de som", inner.any { it.kind == 3 && it.name.startsWith("Som: ") })
        assertTrue("sem a música", inner.any { it.kind == 3 && it.name.startsWith("autoedit-trilha") })
        assertTrue("sem a faixa de legendas", inner.any { it.kind == 4 })
        compose.runOnIdle { store.closePrecomp() }
    }
}
