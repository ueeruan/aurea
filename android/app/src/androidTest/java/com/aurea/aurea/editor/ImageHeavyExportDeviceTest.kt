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
 * Beta 0.1.5: projetos antigos com muitas fotos não exportavam com o motor
 * novo — a largada copiava TODAS as imagens do projeto para o snapshot offline
 * e recusava com BudgetExceeded (erro 9) quando a cópia não cabia no espaço de
 * mídia restante. Aqui as fotos ocupam mais da metade desse espaço; o export
 * precisa abrir (0) e terminar com todos os quadros.
 * Roda só no app isolado de teste: -e aureaImageHeavy true.
 */
class ImageHeavyExportDeviceTest {
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext
    private lateinit var engine: AureaEngine
    private val folder get() = File(context.filesDir, "image-heavy-export").apply { mkdirs() }

    @Before fun initializeHeadlessNativeEngine() {
        org.junit.Assume.assumeTrue(InstrumentationRegistry.getArguments().getString("aureaImageHeavy") == "true")
        check(context.packageName == "com.aurea.aurea.uitest")
        for (font in listOf("Roboto-Regular.ttf", "NotoSansJP-Regular.otf")) {
            context.assets.open(font).use { input -> File(context.cacheDir, font).outputStream().use { input.copyTo(it) } }
        }
        engine = AureaEngine.create(context)
        assertTrue(engine.startupError(), engine.initialize(60f, context.cacheDir.absolutePath, folder.absolutePath, false))
    }

    @After fun releaseNativeEngine() { if (::engine.isInitialized) engine.destroy() }

    @Test fun projectWithManyLargePhotosExports() {
        val frames = 6
        assertTrue(engine.newProject(320, 180, 30.0, "Fotos pesadas", floatArrayOf(.03f, .03f, .03f, 1f)))
        val composition = engine.queryComposition(DoubleArray(8)); assertTrue(composition > 0)
        engine.beginCommandBatch()
        assertTrue(CommandBatch(engine).setCompositionDuration(composition, frames))
        assertEquals(1, engine.submitCommands())
        // ~37,7 MB por foto; o espaço de mídia do motor é no máximo 256 MB, então
        // quatro fotos (~151 MB) passam da metade dele — o cenário do erro 9.
        val w = 4096; val h = 2304
        val pixels = ByteBuffer.allocateDirect(w * h * 4).order(ByteOrder.nativeOrder())
        for (i in 0 until w * h) { pixels.put((i ushr 10).toByte()); pixels.put(90); pixels.put(200.toByte()); pixels.put(255.toByte()) }
        // Enche o espaço de mídia até o motor recusar a próxima foto: aí o que
        // sobra é menor que UMA foto, e a cópia antiga de todas não caberia.
        var imported = 0
        var refused = false
        for (n in 0 until 8) {
            pixels.rewind()
            if (engine.importImage(pixels, w, h, "foto-$n", "") > 0) imported++ else { refused = true; break }
        }
        File(folder, "imported.txt").writeText("imported=$imported refused=$refused\n")
        assertTrue("Ao menos duas fotos grandes entram no projeto (importadas: $imported)", imported >= 2)
        assertTrue("O espaço de mídia precisa ficar cheio (a próxima foto recusada)", refused)
        assertEquals(0, engine.saveProject(File(folder, "fotos-pesadas.aurea").absolutePath))
        val movie = File(folder, "fotos-pesadas.mp4")
        val started = engine.startExport(movie.absolutePath, 180, 30.0, 0, 4)
        assertEquals("O export precisa abrir (antes: 9 = BudgetExceeded no snapshot de imagens)", 0, started)
        val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        val progress = ExportProgress()
        val began = SystemClock.elapsedRealtime()
        try {
            while (true) {
                assertTrue(engine.exportProgress(buffer)); progress.readFrom(buffer)
                if (progress.finished) break
                assertTrue("Export excedeu 10 minutos", SystemClock.elapsedRealtime() - began < 600000)
                SystemClock.sleep(50)
            }
            assertEquals(progress.message, 0, progress.result)
            assertEquals(frames, progress.framesDone)
            assertTrue(movie.isFile && movie.length() > 0)
            File(folder, "result.txt").writeText("PASS imported=$imported frames=${progress.framesDone} bytes=${movie.length()}\n")
        } finally { if (!progress.finished) engine.cancelExport() }
    }
}
