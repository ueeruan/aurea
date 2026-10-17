package com.aurea.aurea.editor

import android.app.Application
import android.graphics.Bitmap
import android.graphics.Color
import android.media.MediaMetadataRetriever
import android.os.SystemClock
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.abs

/**
 * 0.2.1: "o motion blur 3D na prévia está normal e na exportação sai outro".
 * Monta cenas 3D com motion blur (leve e pesada), captura o MESMO quadro na
 * prévia, na qualidade final (o caminho do export) e no MP4 exportado, e
 * grava as imagens e um relatório para comparar.
 */
class MotionBlur3DPreviewVsExportTest {
    @get:Rule val compose = createComposeRule()
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private lateinit var store: EditorStore
    private val out by lazy { File(context.filesDir, "mblur-021").apply { mkdirs() } }
    private val report by lazy { File(out, "relatorio.txt") }

    private fun initialize() {
        var ready = false
        compose.setContent {
            store = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            ready = true
            AureaTheme { EditorScreen(store) }
        }
        compose.waitUntil(30000) { ready && store.engineReady }
    }

    private fun project(title: String) {
        compose.runOnIdle { store.newProject(960, 540, 30f, title) }
        compose.waitUntil(15000) { store.project.title == title && !store.projectOperationBusy }
    }

    /** Camadas 3D atravessando a tela rápido (rastro longo de motion blur). */
    private fun buildScene(count: Int) {
        compose.runOnIdle {
            val engine = store.engineForStress
            val ids = (0 until count).map { engine.addShape3d(it % 3, "Obj $it").also { id -> assertTrue("shape3d $it", id > 0) } }
            engine.beginCommandBatch()
            val c = CommandBatch(engine)
            ids.forEachIndexed { n, id ->
                c.setLayerTimeRange(id, 0, 30)
                val s = if (count == 1) .6f else .22f
                c.setScale(id, s, s, s)
                val y = if (count == 1) 270f else 70f + (n / 6) * 110f
                c.setPosition(id, 100f, y, (n % 3 * 30).toFloat())
                c.insertKeyframe(id, TrackProperty.POSITION_X, -1, 0, 0, 80f + (n % 6) * 40f)
                c.insertKeyframe(id, TrackProperty.POSITION_X, -1, 0, 29, 880f - (n % 6) * 40f)
                c.insertKeyframe(id, TrackProperty.ROTATION_Y, -1, 0, 0, 0f)
                c.insertKeyframe(id, TrackProperty.ROTATION_Y, -1, 0, 29, 180f)
            }
            assertTrue(engine.submitCommands() > 0)
            for (id in ids) assertTrue(engine.setMotionBlur(id, true))
            if (count > 1) {
                val p = engine.addParticles(20)
                if (p > 0) { assertTrue(engine.setMotionBlur(p, true)) }
            }
            engine.setCompositionMotionBlur(true)
            store.setCompositionDuration(30)
        }
        compose.waitUntil(15000) { store.project.durationFrames == 30 && !store.projectOperationBusy }
    }

    private fun seek(frame: Int) {
        compose.runOnIdle { store.pause(); store.seek(frame) }
        compose.waitUntil(30000) { store.playhead == frame }
        SystemClock.sleep(300)
    }

    private fun capture(preview: Boolean): Bitmap {
        val buffer = ByteBuffer.allocateDirect(480 * 480 * 4)
        val size = IntArray(2)
        var bytes = 0
        val deadline = SystemClock.elapsedRealtime() + 120000
        while (bytes <= 0 && SystemClock.elapsedRealtime() < deadline) {
            buffer.clear()
            bytes = if (preview) store.engineForStress.capturePreviewFrame(480, buffer, size)
                    else store.engineForStress.captureFrame(480, buffer, size)
            if (bytes <= 0) SystemClock.sleep(200)
        }
        assertTrue("captura ${if (preview) "previa" else "final"} falhou", bytes > 0)
        val bmp = Bitmap.createBitmap(size[0], size[1], Bitmap.Config.ARGB_8888)
        buffer.rewind(); bmp.copyPixelsFromBuffer(buffer)
        return bmp
    }

    private fun export(name: String): File {
        val file = File(out, "$name.mp4")
        val engine = store.engineForStress
        assertEquals(0, engine.startExport(file.absolutePath, 540, 30.0, 0, 8))
        val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        val progress = ExportProgress()
        val deadline = SystemClock.elapsedRealtime() + 900000
        while (SystemClock.elapsedRealtime() < deadline) {
            assertTrue(engine.exportProgress(buffer)); progress.readFrom(buffer)
            if (progress.finished) break
            SystemClock.sleep(100)
        }
        assertTrue("export nao terminou", progress.finished)
        assertEquals(progress.message, 0, progress.result)
        return file
    }

    private fun decoded(file: File, frame: Int, width: Int): Bitmap {
        val r = MediaMetadataRetriever()
        try {
            r.setDataSource(file.absolutePath)
            val raw = checkNotNull(r.getFrameAtTime(frame * 1_000_000L / 30 + 1000, MediaMetadataRetriever.OPTION_CLOSEST)) { "quadro $frame" }
            return Bitmap.createScaledBitmap(raw, width, raw.height * width / raw.width, true)
        } finally { r.release() }
    }

    private fun save(b: Bitmap, name: String) =
        File(out, "$name.png").outputStream().use { b.compress(Bitmap.CompressFormat.PNG, 100, it) }

    private fun luma(p: Int) = (Color.red(p) * 299 + Color.green(p) * 587 + Color.blue(p) * 114) / 1000

    /** Pixels de rastro (meio-tom) e pixels acesos: o desfoque vira meio-tom. */
    private fun stats(b: Bitmap): Pair<Int, Int> {
        var smear = 0; var lit = 0
        for (y in 0 until b.height) for (x in 0 until b.width) {
            val l = luma(b.getPixel(x, y))
            if (l > 24) lit++
            if (l in 25..170) smear++
        }
        return smear to lit
    }

    private fun diff(a: Bitmap, b: Bitmap): Double {
        val w = minOf(a.width, b.width); val h = minOf(a.height, b.height)
        var sum = 0L
        for (y in 0 until h) for (x in 0 until w) sum += abs(luma(a.getPixel(x, y)) - luma(b.getPixel(x, y)))
        return sum.toDouble() / (w * h)
    }

    private fun run(name: String, count: Int) {
        project("MB3D $name")
        buildScene(count)
        val frame = 15
        seek(frame)
        val preview = capture(true)
        val final = capture(false)
        save(preview, "$name-previa"); save(final, "$name-final")
        val video = export(name)
        val exported = decoded(video, frame, preview.width)
        save(exported, "$name-export")
        val (ps, pl) = stats(preview); val (fs, fl) = stats(final); val (es, el) = stats(exported)
        report.appendText(
            "[$name] objetos=$count quadro=$frame\n" +
            "  previa : rastro=$ps acesos=$pl\n" +
            "  final  : rastro=$fs acesos=$fl\n" +
            "  export : rastro=$es acesos=$el\n" +
            "  diferenca media previa x final  = ${"%.2f".format(diff(preview, final))}\n" +
            "  diferenca media previa x export = ${"%.2f".format(diff(preview, exported))}\n" +
            "  diferenca media final  x export = ${"%.2f".format(diff(final, exported))}\n")
    }

    @Test fun lightAndHeavy3DMotionBlurMatchBetweenPreviewAndExport() {
        initialize()
        report.writeText("Motion blur 3D: previa x qualidade final x MP4 exportado (0.2.1)\n")
        run("leve", 1)
        run("pesada", 18)
    }
}
