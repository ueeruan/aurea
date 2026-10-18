package com.aurea.aurea.editor

import android.content.ContentValues
import android.os.SystemClock
import android.provider.MediaStore
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
 * Vídeo da GALERIA (content://), como o app importa no celular: o export
 * precisa abrir, terminar e NÃO copiar o vídeo para o app antes (a cópia,
 * com o editor travado e prazo de 120 s, travava a tela ou falhava com
 * vídeo grande). Uso: adb push video.mp4 /sdcard/Android/data/com.aurea.aurea.uitest/files/
 *      -e aureaGalleryVideo video.mp4
 */
class GalleryVideoExportDeviceTest {
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext
    private val args get() = InstrumentationRegistry.getArguments()
    private lateinit var engine: AureaEngine
    private val folder get() = File(context.filesDir, "gallery-export").apply { mkdirs() }

    @Before fun initializeHeadlessNativeEngine() {
        org.junit.Assume.assumeTrue(args.getString("aureaGalleryVideo") != null)
        check(context.packageName == "com.aurea.aurea.uitest")
        for (font in listOf("Roboto-Regular.ttf", "NotoSansJP-Regular.otf")) {
            context.assets.open(font).use { input -> File(context.cacheDir, font).outputStream().use { input.copyTo(it) } }
        }
        File(folder, "export-sources").deleteRecursively()
        engine = AureaEngine.create(context)
        assertTrue(engine.startupError(), engine.initialize(60f, context.cacheDir.absolutePath, folder.absolutePath, false))
    }

    @After fun releaseNativeEngine() { if (::engine.isInitialized) engine.destroy() }

    @Test fun galleryVideoExportsWithoutCopyingTheSource() {
        val log = File(folder, "result.txt").apply { writeText("") }
        fun note(line: String) { log.appendText(line + "\n"); android.util.Log.i("AureaGalleryTest", line) }
        val name = args.getString("aureaGalleryVideo")!!
        val source = File(context.getExternalFilesDir(null), name)
        assertTrue("Video ausente: $source", source.isFile)
        // Publica na galeria (MediaStore) e usa a URI content:// como o seletor do app entrega.
        val resolver = context.contentResolver
        val values = ContentValues().apply {
            put(MediaStore.Video.Media.DISPLAY_NAME, "aurea-teste-${SystemClock.elapsedRealtime()}.mp4")
            put(MediaStore.Video.Media.MIME_TYPE, "video/mp4")
            put(MediaStore.Video.Media.RELATIVE_PATH, "Movies/AureaTeste")
        }
        val uri = resolver.insert(MediaStore.Video.Media.EXTERNAL_CONTENT_URI, values)!!
        try {
            resolver.openOutputStream(uri)!!.use { out -> source.inputStream().use { it.copyTo(out) } }
            note("uri=$uri bytes=${source.length()}")
            assertTrue(engine.newProject(1080, 1920, 30.0, "Galeria", floatArrayOf(0f, 0f, 0f, 1f)))
            val video = engine.importVideo(uri.toString(), "galeria")
            note("importVideo=$video")
            assertTrue("Importar o vídeo da galeria", video > 0)
            val comp = engine.queryComposition(DoubleArray(8)); assertTrue(comp > 0)
            engine.beginCommandBatch()
            assertTrue(CommandBatch(engine).setCompositionDuration(comp, 600))
            assertEquals(1, engine.submitCommands())
            SystemClock.sleep(3000)
            val movie = File(folder, "galeria.mp4"); movie.delete()
            val began = SystemClock.elapsedRealtime()
            val started = engine.startExport(movie.absolutePath, 1080, 0.0, 0, 0)
            note("startExport=$started largadaMs=${SystemClock.elapsedRealtime() - began}")
            assertEquals("O export precisa abrir", 0, started)
            val copies = File(folder, "export-sources").listFiles()?.size ?: 0
            note("copias=$copies")
            assertEquals("O vídeo da galeria não pode ser copiado antes do export", 0, copies)
            val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
            val progress = ExportProgress()
            var last = -1; var moved = SystemClock.elapsedRealtime()
            try {
                while (true) {
                    assertTrue(engine.exportProgress(buffer)); progress.readFrom(buffer)
                    if (progress.framesDone != last) { last = progress.framesDone; moved = SystemClock.elapsedRealtime() }
                    if (progress.finished) break
                    assertTrue("Export travou em ${progress.framesDone}/${progress.framesTotal}: ${progress.message}",
                        SystemClock.elapsedRealtime() - moved < 180000)
                    SystemClock.sleep(100)
                }
                note("fim resultado=${progress.result} quadros=${progress.framesDone}/${progress.framesTotal} " +
                    "msg=${progress.message} bytes=${movie.length()} s=${(SystemClock.elapsedRealtime() - began) / 1000}")
                assertEquals(progress.message, 0, progress.result)
                assertEquals(600, progress.framesDone)
                assertTrue(movie.isFile && movie.length() > 0)
            } finally { if (!progress.finished) engine.cancelExport() }
        } finally { resolver.delete(uri, null, null) }
    }
}
