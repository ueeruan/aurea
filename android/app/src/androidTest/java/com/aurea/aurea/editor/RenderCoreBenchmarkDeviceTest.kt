package com.aurea.aurea.editor

import android.media.MediaExtractor
import android.media.MediaFormat
import android.os.Debug
import android.os.SystemClock
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.engine.TrackProperty
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Benchmark do export (Missão ASTRA / RenderCore): mede o export REAL do motor
 * (composição offline → NV12 na GPU → encoder do aparelho → MP4) em cenários
 * reproduzíveis e em projetos reais. Cada cenário roda num processo novo
 * (tools/rendercore_bench.py chama um teste por vez), então VmHWM é o pico do
 * cenário. O motor escreve as etapas no logcat ("export N/N ... ms/quadro" e
 * "export perfil"); este teste escreve as linhas AureaBench.
 *
 * Uso: -e aureaBench true [-e aureaBenchFrames N] [-e aureaBenchShortSide 1080]
 * Fixtures em getExternalFilesDir(null): galeria-teste.mp4, *.aureaproj.
 */
class RenderCoreBenchmarkDeviceTest {
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext
    private val args get() = InstrumentationRegistry.getArguments()
    private lateinit var engine: AureaEngine
    private val folder get() = File(context.filesDir, "rendercore-bench").apply { mkdirs() }
    private val external get() = context.getExternalFilesDir(null)!!

    @Before fun initializeHeadlessNativeEngine() {
        org.junit.Assume.assumeTrue(args.getString("aureaBench") == "true")
        check(context.packageName == "com.aurea.aurea.uitest")
        for (font in listOf("Roboto-Regular.ttf", "NotoSansJP-Regular.otf")) {
            context.assets.open(font).use { input -> File(context.cacheDir, font).outputStream().use { input.copyTo(it) } }
        }
        engine = AureaEngine.create(context)
        // A MESMA sondagem do app (memória + tabela de codecs): sem ela o motor
        // usa a política conservadora e o benchmark não mediria o app real.
        val probe = runCatching { com.aurea.aurea.engine.DeviceProfile.probe(context) }.getOrNull()
        assertTrue(engine.startupError(), engine.initialize(60f, context.cacheDir.absolutePath, folder.absolutePath, false,
            probe?.memory, probe?.codecs))
    }

    @After fun releaseNativeEngine() { if (::engine.isInitialized) engine.destroy() }

    private fun note(line: String) { android.util.Log.i("AureaBench", line) }
    private fun fnv1a(key: String): Int {
        var h = 0x811C9DC5.toInt()
        for (b in key.toByteArray()) { h = h xor (b.toInt() and 0xFF); h *= 0x01000193 }
        return h
    }
    private val frameLimit get() = args.getString("aureaBenchFrames")?.toInt() ?: 0
    private val shortSide get() = args.getString("aureaBenchShortSide")?.toInt() ?: 1080

    private fun project(width: Int, height: Int, frames: Int): Long {
        assertTrue(engine.newProject(width, height, 30.0, "RenderCore bench", floatArrayOf(.02f, .02f, .03f, 1f)))
        val comp = engine.queryComposition(DoubleArray(8)); assertTrue(comp > 0)
        engine.beginCommandBatch(); assertTrue(CommandBatch(engine).setCompositionDuration(comp, frames)); assertEquals(1, engine.submitCommands())
        return comp
    }

    private fun setDuration(comp: Long, frames: Int) {
        engine.beginCommandBatch()
        assertTrue(CommandBatch(engine).setCompositionDuration(comp, frames))
        assertEquals(1, engine.submitCommands())
        // Sem prévia, a fila de comandos só é drenada ao salvar (como no AstraExportDeviceTest).
        assertEquals(0, engine.saveProject(File(folder, "bench-fixture.aurea").absolutePath))
    }

    private fun waitComposition(frames: Int) {
        val until = SystemClock.elapsedRealtime() + 30000
        while (true) {
            val values = DoubleArray(8)
            if (engine.queryComposition(values) != 0L && values[3].toInt() == frames) return
            assertTrue("composição não aplicou a duração (${values[3]} != $frames)", SystemClock.elapsedRealtime() < until)
            SystemClock.sleep(20)
        }
    }

    private fun gradient(width: Int, height: Int, seed: Int): ByteBuffer {
        val buffer = ByteBuffer.allocateDirect(width * height * 4)
        val row = ByteArray(width * 4)
        for (y in 0 until height) {
            for (x in 0 until width) {
                val o = x * 4
                row[o] = ((x * 255 / width + seed * 40) and 255).toByte()
                row[o + 1] = ((y * 255 / height + seed * 90) and 255).toByte()
                row[o + 2] = (((x + y) * 128 / (width + height) + seed * 60) and 255).toByte()
                row[o + 3] = 255.toByte()
            }
            buffer.put(row)
        }
        buffer.rewind()
        return buffer
    }

    private fun vmHwmKb(): Long = File("/proc/self/status").readLines()
        .firstOrNull { it.startsWith("VmHWM:") }?.filter { it.isDigit() }?.toLongOrNull() ?: -1

    /** Exporta a composição atual e registra tempo, quadros, bytes e pico de memória. */
    private fun exportAndMeasure(name: String) {
        val values = DoubleArray(8); engine.queryComposition(values)
        val frames = values[3].toInt()
        val movie = File(external, "bench-$name.mp4"); movie.delete()
        val hwmBefore = vmHwmKb()
        note("BEGIN scenario=$name frames=$frames comp=${values[0].toInt()}x${values[1].toInt()} hwmBeforeKb=$hwmBefore")
        val began = SystemClock.elapsedRealtime()
        val started = engine.startExport(movie.absolutePath, shortSide, 0.0, 0, 0)
        val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        val progress = ExportProgress()
        if (started != 0 && engine.exportProgress(buffer)) progress.readFrom(buffer)
        assertEquals("export precisa abrir: ${progress.message}", 0, started)
        var last = -1; var moved = SystemClock.elapsedRealtime(); var firstFrameMs = -1L; var allFramesMs = -1L
        var peakPss = 0L; var samples = 0
        try {
            while (true) {
                assertTrue(engine.exportProgress(buffer)); progress.readFrom(buffer)
                if (progress.framesDone != last) {
                    last = progress.framesDone; moved = SystemClock.elapsedRealtime()
                    if (firstFrameMs < 0 && last > 0) firstFrameMs = moved - began
                    if (allFramesMs < 0 && last == progress.framesTotal) allFramesMs = moved - began
                }
                if (progress.finished) break
                if (++samples % 10 == 0) peakPss = maxOf(peakPss, Debug.getPss())
                assertTrue("TRAVOU em ${progress.framesDone}/${progress.framesTotal}: ${progress.message}",
                    SystemClock.elapsedRealtime() - moved < 300000)
                SystemClock.sleep(50)
            }
        } finally { if (!progress.finished) engine.cancelExport() }
        val wallMs = SystemClock.elapsedRealtime() - began
        val decoded = countVideoSamples(movie)
        note("END scenario=$name result=${progress.result} frames=${progress.framesDone}/${progress.framesTotal} decoded=$decoded " +
            "wallMs=$wallMs firstFrameMs=$firstFrameMs framesMs=$allFramesMs finalizeMs=${if (allFramesMs >= 0) wallMs - allFramesMs else -1} fps=${"%.2f".format(progress.framesDone * 1000.0 / wallMs)} " +
            "bytes=${movie.length()} hwmKb=${vmHwmKb()} peakPssKb=$peakPss flags=${progress.flags} msg=${progress.message}")
        assertEquals(progress.message, 0, progress.result)
        assertEquals(progress.framesTotal, progress.framesDone)
        assertEquals("o MP4 precisa ter todos os quadros", progress.framesTotal, decoded)
    }

    private fun countVideoSamples(movie: File): Int {
        val extractor = MediaExtractor()
        try {
            extractor.setDataSource(movie.absolutePath)
            val track = (0 until extractor.trackCount).first {
                extractor.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("video/") == true
            }
            extractor.selectTrack(track)
            var count = 0
            while (extractor.sampleTrackIndex >= 0) { count++; extractor.advance() }
            return count
        } finally { extractor.release() }
    }

    /** Título: texto que entra em 1 s (opacidade) e fica parado 9 s sobre um fundo e uma forma. */
    @Test fun staticTitle() {
        val frames = if (frameLimit > 0) frameLimit else 300
        val comp = project(1080, 1920, frames)
        val shape = engine.addShape(1); assertTrue(shape > 0)
        val text = engine.addText("AUREA RENDERCORE"); assertTrue(text > 0)
        engine.beginCommandBatch(); val c = CommandBatch(engine)
        c.setLayerTimeRange(shape, 0, frames); c.setLayerTimeRange(text, 0, frames)
        c.setScale(shape, .6f, .3f, 1f); c.setShapeFill(shape, .1f, .3f, .8f, 1f); c.setPosition(shape, 540f, 960f, 0f)
        c.setPosition(text, 540f, 960f, 0f); c.setTextSize(text, 96f)
        c.insertKeyframe(text, TrackProperty.OPACITY, -1, 0, 0, 0f)
        c.insertKeyframe(text, TrackProperty.OPACITY, -1, 0, 30, 1f)
        note("comandos=${engine.submitCommands()}")
        setDuration(comp, frames)   // camadas novas esticam a composição
        waitComposition(frames); SystemClock.sleep(500)
        exportAndMeasure("staticTitle")
        check(comp > 0)
    }

    /** Slideshow: três fotos 1080x1920, as duas primeiras com zoom lento, a terceira parada. */
    @Test fun photoSlideshow() {
        val frames = if (frameLimit > 0) frameLimit else 300
        val comp = project(1080, 1920, frames)
        val third = frames / 3
        val photos = (0 until 3).map { i ->
            engine.importImage(gradient(1080, 1920, i), 1080, 1920, "foto$i", "bench://foto$i").also { assertTrue("foto $i: $it", it > 0) }
        }
        engine.beginCommandBatch(); val c = CommandBatch(engine)
        photos.forEachIndexed { i, id ->
            c.setLayerTimeRange(id, i * third, if (i == 2) frames else (i + 1) * third)
            if (i < 2) {
                c.insertKeyframe(id, TrackProperty.SCALE_X, -1, 0, i * third, 1f)
                c.insertKeyframe(id, TrackProperty.SCALE_X, -1, 0, (i + 1) * third - 1, 1.15f)
                c.insertKeyframe(id, TrackProperty.SCALE_Y, -1, 0, i * third, 1f)
                c.insertKeyframe(id, TrackProperty.SCALE_Y, -1, 0, (i + 1) * third - 1, 1.15f)
            }
        }
        note("comandos=${engine.submitCommands()}")
        setDuration(comp, frames)
        waitComposition(frames); SystemClock.sleep(500)
        exportAndMeasure("photoSlideshow")
    }

    private fun importBenchVideo(): Long {
        val source = File(external, "galeria-teste.mp4")
        assertTrue("fixture ausente: $source", source.isFile)
        val id = engine.importVideo(source.absolutePath, "galeria-teste")
        assertTrue("importVideo=$id", id > 0)
        return id
    }

    /** Vídeo 1080x1920 30 fps com áudio AAC, 10 s, legenda parada por cima. */
    @Test fun videoClip() {
        val frames = if (frameLimit > 0) frameLimit else 300
        val comp = project(1080, 1920, frames)
        val video = importBenchVideo()
        val text = engine.addText("legenda")
        engine.beginCommandBatch(); val c = CommandBatch(engine)
        // O vídeo de 20 s estica a composição: volta para a duração do cenário.
        c.setLayerTimeRange(video, 0, frames); if (text > 0) { c.setLayerTimeRange(text, 0, frames); c.setPosition(text, 540f, 1700f, 0f) }
        note("comandos=${engine.submitCommands()}")
        setDuration(comp, frames)
        waitComposition(frames); SystemClock.sleep(1500)
        exportAndMeasure("videoClip")
    }

    /** Vídeo com desfoque gaussiano + glow + desfoque direcional (efeitos de tela cheia). */
    @Test fun videoEffects() {
        val frames = if (frameLimit > 0) frameLimit else 300
        val comp = project(1080, 1920, frames)
        val video = importBenchVideo()
        engine.beginCommandBatch(); val c = CommandBatch(engine)
        c.setLayerTimeRange(video, 0, frames)
        note("comandos=${engine.submitCommands()}")
        engine.beginCommandBatch(); val fx = CommandBatch(engine)
        var added = 0
        for (key in listOf("aurea.blur.gaussian", "aurea.light.glow", "aurea.blur.directional"))
            if (fx.addEffect(video, fnv1a(key))) added++
        engine.submitCommands()
        setDuration(comp, frames)
        note("efeitos=$added")
        waitComposition(frames); SystemClock.sleep(1500)
        exportAndMeasure("videoEffects")
    }

    private fun realPackage(name: String, label: String) {
        val source = File(external, name)
        assertTrue("pacote ausente: $source", source.isFile)
        val project = File(folder, "$label.aurea"); project.delete()
        val media = File(folder, "$label-media").apply { deleteRecursively(); mkdirs() }
        val imported = engine.importProjectPackage(source.absolutePath, project.absolutePath, media.absolutePath)
        assertEquals("import ${imported.joinToString("|")}", "0", imported[0])
        assertEquals(0, engine.loadProject(project.absolutePath))
        val values = DoubleArray(8); val comp = engine.queryComposition(values); assertTrue(comp > 0)
        val total = values[3].toInt()
        if (frameLimit in 1 until total) {
            setDuration(comp, frameLimit)
            waitComposition(frameLimit)
        }
        note("projeto=$label quadrosOriginais=$total")
        SystemClock.sleep(4000)
        exportAndMeasure(label)
    }

    @Test fun project8() = realPackage(args.getString("aureaBenchProject8") ?: "Project8-Test-copy.aureaproj", "project8")
    @Test fun project13() = realPackage(args.getString("aureaBenchProject13") ?: "Project 13.aureaproj", "project13")
}
