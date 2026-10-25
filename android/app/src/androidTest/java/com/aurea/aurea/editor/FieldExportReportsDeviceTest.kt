package com.aurea.aurea.editor

import android.os.SystemClock
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.ExportProgress
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Os relatos de export da 0.3.0 (lista de bugs críticos), um por teste, com o
 * motor de verdade: clipe de celular 1080p60 cortado no meio (travava sempre no
 * mesmo quadro), projeto a 60 qps, saída a 200 qps e sequência de imagens.
 * Cada resultado vai para files/field-export/result.txt e para o logcat
 * (AureaFieldExport).
 *
 * Uso: adb push clipe.mp4 seq/ /data/local/tmp/, depois
 *      run-as com.aurea.aurea.uitest cp -r /data/local/tmp/… files/field-export/Media/
 *      e -e aureaFieldVideo clipe.mp4 [-e aureaFieldSequence seq]
 */
class FieldExportReportsDeviceTest {
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext
    private val args get() = InstrumentationRegistry.getArguments()
    private lateinit var engine: AureaEngine
    private val folder get() = File(context.filesDir, "field-export").apply { mkdirs() }
    private val log get() = File(folder, "result.txt")

    private fun note(line: String) { log.appendText(line + "\n"); android.util.Log.i("AureaFieldExport", line) }

    @Before fun initializeHeadlessNativeEngine() {
        org.junit.Assume.assumeTrue(args.getString("aureaFieldVideo") != null || args.getString("aureaFieldSequence") != null)
        check(context.packageName == "com.aurea.aurea.uitest")
        for (font in listOf("Roboto-Regular.ttf", "NotoSansJP-Regular.otf")) {
            context.assets.open(font).use { input -> File(context.cacheDir, font).outputStream().use { input.copyTo(it) } }
        }
        engine = AureaEngine.create(context)
        assertTrue(engine.startupError(), engine.initialize(60f, context.cacheDir.absolutePath, folder.absolutePath, false))
    }

    @After fun releaseNativeEngine() { if (::engine.isInitialized) engine.destroy() }

    private fun video(): File {
        val name = args.getString("aureaFieldVideo")
        org.junit.Assume.assumeTrue(name != null)
        val file = File(folder, "Media/$name")
        assertTrue("Video ausente: $file", file.isFile)
        return file
    }

    /** Exporta e devolve o progresso final; falha se nada andar por 4 min. */
    private fun export(case: String, shortSide: Int, fps: Double, safeMode: Int = 0): ExportProgress {
        val movie = File(folder, "$case.mp4"); movie.delete()
        val began = SystemClock.elapsedRealtime()
        val started = engine.startExport(movie.absolutePath, shortSide, fps, 0, 0, 0, true, 1, 1, safeMode)
        note("$case startExport=$started")
        assertEquals("$case: o export precisa abrir", 0, started)
        val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        val progress = ExportProgress()
        var last = -1
        var lastMessage = ""
        var moved = SystemClock.elapsedRealtime()
        try {
            while (true) {
                assertTrue(engine.exportProgress(buffer)); progress.readFrom(buffer)
                if (progress.framesDone != last || progress.message != lastMessage) {
                    last = progress.framesDone; lastMessage = progress.message; moved = SystemClock.elapsedRealtime()
                }
                if (progress.finished) break
                assertTrue("$case travou em ${progress.framesDone}/${progress.framesTotal}: ${progress.message}",
                    SystemClock.elapsedRealtime() - moved < 240_000)
                SystemClock.sleep(100)
            }
        } finally { if (!progress.finished) engine.cancelExport() }
        note("$case fim resultado=${progress.result} motivo=${progress.failure} quadros=${progress.framesDone}/${progress.framesTotal} " +
            "aproximado=${progress.frameFallback} msg=${progress.message} bytes=${movie.length()} " +
            "s=${(SystemClock.elapsedRealtime() - began) / 1000}")
        return progress
    }

    private fun assertComplete(case: String, p: ExportProgress) {
        assertEquals("$case: ${p.message}", 0, p.result)
        assertEquals("$case: todos os quadros", p.framesTotal, p.framesDone)
    }

    /** saturn (Poco X6 Pro): vídeo só cortado, parava sempre no mesmo quadro. */
    @Test fun cutPhoneClipExportsEveryFrame() {
        val file = video()
        assertTrue(engine.newProject(1080, 1920, 30.0, "Corte", floatArrayOf(0f, 0f, 0f, 1f)))
        val layer = engine.importVideo(file.absolutePath, file.nameWithoutExtension)
        note("corte importVideo=$layer")
        assertTrue(layer > 0)
        engine.beginCommandBatch()
        assertTrue(CommandBatch(engine).splitLayer(layer, 151))
        assertEquals(1, engine.submitCommands())
        SystemClock.sleep(1500)
        assertComplete("corte", export("corte", 1080, 0.0))
    }

    /** Kauz (Poco M8): clipe 1080p60 numa timeline a 60 qps. */
    @Test fun sixtyFpsProjectExports() {
        val file = video()
        assertTrue(engine.newProject(1920, 1080, 60.0, "Sessenta", floatArrayOf(0f, 0f, 0f, 1f)))
        assertTrue(engine.importVideo(file.absolutePath, file.nameWithoutExtension) > 0)
        SystemClock.sleep(1500)
        assertComplete("sessenta", export("sessenta", 1080, 0.0))
    }

    /** Export a 200 qps (projeto a 30). */
    @Test fun twoHundredFpsExport() {
        val file = video()
        assertTrue(engine.newProject(1280, 720, 30.0, "Duzentos", floatArrayOf(0f, 0f, 0f, 1f)))
        val layer = engine.importVideo(file.absolutePath, file.nameWithoutExtension)
        assertTrue(layer > 0)
        val comp = engine.queryComposition(DoubleArray(8)); assertTrue(comp > 0)
        engine.beginCommandBatch()
        assertTrue(CommandBatch(engine).setCompositionDuration(comp, 90))   // 3 s = 600 quadros a 200 qps
        assertEquals(1, engine.submitCommands())
        SystemClock.sleep(1500)
        val p = export("duzentos", 720, 200.0)
        assertComplete("duzentos", p)
    }

    /** Sequência de imagens copiada para a pasta do projeto, como o app faz. */
    @Test fun imageSequenceExports() {
        val name = args.getString("aureaFieldSequence")
        org.junit.Assume.assumeTrue(name != null)
        val source = File(folder, "Media/$name")
        val frames = source.listFiles { f -> f.isFile }?.sortedBy { it.name }.orEmpty()
        assertTrue("Sequencia vazia: $source", frames.isNotEmpty())
        val target = File(folder, "Media/sequences/seq-teste").apply { deleteRecursively(); mkdirs() }
        val copied = frames.map { f -> File(target, f.name).also { f.copyTo(it, overwrite = true) }.absolutePath }
        assertTrue(engine.newProject(1920, 1080, 30.0, "Sequencia", floatArrayOf(0f, 0f, 0f, 1f)))
        val layer = engine.importImageSequence(copied.toTypedArray(), 0.0, 0, 0)
        note("sequencia import=$layer quadros=${copied.size}")
        assertTrue(layer > 0)
        SystemClock.sleep(1500)
        assertComplete("sequencia", export("sequencia", 1080, 0.0))
    }
}
