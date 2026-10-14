package com.aurea.aurea.editor

import android.os.SystemClock
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.AureaEngine
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
 * Abre um pacote .aureaproj real (enviado pelo dono) e exporta como o app faz.
 * Uso: adb push Projeto.aureaproj /sdcard/Android/data/com.aurea.aurea.uitest/files/
 *      -e aureaPackage <nome-do-arquivo> [-e aureaShortSide 1080] [-e aureaCodec 0|1]
 * O resultado (código, motivo, quadros) fica em files/package-export/result.txt.
 */
class ProjectPackageExportDeviceTest {
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext
    private val args get() = InstrumentationRegistry.getArguments()
    private lateinit var engine: AureaEngine
    private val folder get() = File(context.filesDir, "package-export").apply { mkdirs() }

    @Before fun initializeHeadlessNativeEngine() {
        org.junit.Assume.assumeTrue(args.getString("aureaPackage") != null)
        check(context.packageName == "com.aurea.aurea.uitest")
        for (font in listOf("Roboto-Regular.ttf", "NotoSansJP-Regular.otf")) {
            context.assets.open(font).use { input -> File(context.cacheDir, font).outputStream().use { input.copyTo(it) } }
        }
        engine = AureaEngine.create(context)
        assertTrue(engine.startupError(), engine.initialize(60f, context.cacheDir.absolutePath, folder.absolutePath, false))
    }

    @After fun releaseNativeEngine() { if (::engine.isInitialized) engine.destroy() }

    @Test fun packageOpensAndExports() {
        val log = File(folder, "result.txt").apply { writeText("") }
        fun note(line: String) { log.appendText(line + "\n"); android.util.Log.i("AureaPackageTest", line) }
        val source = File(context.getExternalFilesDir(null), args.getString("aureaPackage")!!)
        assertTrue("Pacote ausente: $source", source.isFile)
        val project = File(folder, "pacote.aurea"); project.delete()
        val media = File(folder, "media").apply { deleteRecursively(); mkdirs() }
        val imported = engine.importProjectPackage(source.absolutePath, project.absolutePath, media.absolutePath)
        note("import=${imported.joinToString("|")}")
        assertEquals("Importar o pacote", "0", imported[0])
        val loaded = engine.loadProject(project.absolutePath)
        note("load=$loaded")
        assertEquals("Abrir o projeto", 0, loaded)
        // Modelos 3D e mídias terminam de carregar em segundo plano.
        SystemClock.sleep(8000)
        val comp = DoubleArray(8); engine.queryComposition(comp)
        note("composicao=${comp.joinToString(",")}")
        val shortSide = args.getString("aureaShortSide")?.toInt() ?: 1080
        val codec = args.getString("aureaCodec")?.toInt() ?: 0
        val movie = File(folder, "pacote.mp4"); movie.delete()
        val started = engine.startExport(movie.absolutePath, shortSide, 0.0, codec, 0)
        note("startExport=$started")
        val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        val progress = ExportProgress()
        if (started != 0) {
            if (engine.exportProgress(buffer)) { progress.readFrom(buffer); note("motivo=${progress.message} falha=${progress.failure}") }
        }
        assertEquals("O export precisa abrir", 0, started)
        val began = SystemClock.elapsedRealtime()
        var last = -1; var moved = began
        try {
            while (true) {
                assertTrue(engine.exportProgress(buffer)); progress.readFrom(buffer)
                if (progress.framesDone != last) { last = progress.framesDone; moved = SystemClock.elapsedRealtime() }
                if (progress.finished) break
                if (SystemClock.elapsedRealtime() - moved > 180000) {
                    note("TRAVADO quadro=${progress.framesDone}/${progress.framesTotal} msg=${progress.message}")
                    break
                }
                assertTrue("Export excedeu 20 minutos", SystemClock.elapsedRealtime() - began < 1200000)
                SystemClock.sleep(100)
            }
            note("fim resultado=${progress.result} falha=${progress.failure} quadros=${progress.framesDone}/${progress.framesTotal} " +
                "flags=${progress.flags} msg=${progress.message} bytes=${movie.length()} s=${(SystemClock.elapsedRealtime() - began) / 1000}")
            assertTrue("Export terminou", progress.finished)
            assertEquals(progress.message, 0, progress.result)
            assertTrue(movie.isFile && movie.length() > 0)
        } finally { if (!progress.finished) engine.cancelExport() }
    }
}
