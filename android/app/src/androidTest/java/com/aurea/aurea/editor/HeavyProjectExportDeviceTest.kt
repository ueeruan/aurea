package com.aurea.aurea.editor

import android.os.SystemClock
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.ExportProgress
import com.aurea.aurea.engine.MotionBlurSettings
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
 * Projeto PESADO como o dono usa: um .aureaproj real com modelos 3D grandes,
 * mais 24 malhas 3D animadas, partículas, texto 3D e texto, motion blur em
 * TODAS as camadas (32 amostras, adaptativo até 64) e efeitos empilhados
 * (desfoque gaussiano, glow, deep glow, desfoque direcional). O export precisa
 * abrir, nunca parar de progredir e terminar com todos os quadros.
 * Uso: -e aureaHeavyPackage <arquivo em files/ externo> [-e aureaShortSide 1080]
 */
class HeavyProjectExportDeviceTest {
    private val instrumentation get() = InstrumentationRegistry.getInstrumentation()
    private val context get() = instrumentation.targetContext
    private val args get() = InstrumentationRegistry.getArguments()
    private lateinit var engine: AureaEngine
    private val folder get() = File(context.filesDir, "heavy-export").apply { mkdirs() }

    @Before fun initializeHeadlessNativeEngine() {
        org.junit.Assume.assumeTrue(args.getString("aureaHeavyPackage") != null)
        check(context.packageName == "com.aurea.aurea.uitest")
        for (font in listOf("Roboto-Regular.ttf", "NotoSansJP-Regular.otf")) {
            context.assets.open(font).use { input -> File(context.cacheDir, font).outputStream().use { input.copyTo(it) } }
        }
        engine = AureaEngine.create(context)
        assertTrue(engine.startupError(), engine.initialize(60f, context.cacheDir.absolutePath, folder.absolutePath, false))
    }

    @After fun releaseNativeEngine() { if (::engine.isInitialized) engine.destroy() }

    private fun fnv1a(key: String): Int {
        var h = 0x811C9DC5.toInt()
        for (b in key.toByteArray()) { h = h xor (b.toInt() and 0xFF); h *= 0x01000193 }
        return h
    }

    private fun layerIds(): List<Long> {
        val rows = ByteBuffer.allocateDirect(256 * 72).order(ByteOrder.nativeOrder())
        val names = ByteBuffer.allocateDirect(64 * 1024).order(ByteOrder.nativeOrder())
        val count = engine.queryLayers(rows, 256, names)
        // Cada linha (LayerRowPOD, 72 bytes = PodLayout.LAYER_ROW_BYTES) começa pelo id u64.
        return (0 until maxOf(0, count)).map { rows.getLong(it * 72) }
    }

    @Test fun heavyMotionBlur3DEffectsProjectExports() {
        val log = File(folder, "result.txt").apply { writeText("") }
        fun note(line: String) { log.appendText(line + "\n"); android.util.Log.i("AureaHeavyTest", line) }
        val source = File(context.getExternalFilesDir(null), args.getString("aureaHeavyPackage")!!)
        assertTrue("Pacote ausente: $source", source.isFile)
        val project = File(folder, "pesado.aurea"); project.delete()
        val media = File(folder, "media").apply { deleteRecursively(); mkdirs() }
        val imported = engine.importProjectPackage(source.absolutePath, project.absolutePath, media.absolutePath)
        note("import=${imported.joinToString("|")}")
        assertEquals("0", imported[0])
        assertEquals(0, engine.loadProject(project.absolutePath))
        SystemClock.sleep(6000)
        val comp = DoubleArray(8); val compId = engine.queryComposition(comp); assertTrue(compId > 0)
        val w = comp[0].toFloat(); val h = comp[1].toFloat(); val frames = comp[3].toInt().coerceAtLeast(60)
        // Carga extra: 24 malhas 3D girando, partículas, texto 3D e texto.
        val meshes = (0 until 24).map { engine.addShape3d(it % 3, "Malha $it").also { id -> assertTrue(id > 0) } }
        val particles = listOf(engine.addParticles(20), engine.addParticles(1)).filter { it > 0 }
        val text3d = engine.addText3d("AUREA PESADO", FloatArray(0))
        val text = engine.addText("MOTION BLUR + 3D + EFEITOS")
        engine.beginCommandBatch(); val c = CommandBatch(engine)
        meshes.forEachIndexed { i, id ->
            c.setLayerTimeRange(id, 0, frames); c.setScale(id, .25f, .25f, .25f)
            c.setPosition(id, w * (.1f + (i % 6) * .16f), h * (.15f + (i / 6) * .2f), 0f)
            c.insertKeyframe(id, TrackProperty.ROTATION_Y, -1, 0, 0, 0f)
            c.insertKeyframe(id, TrackProperty.ROTATION_Y, -1, 0, frames - 1, 720f)
            c.insertKeyframe(id, TrackProperty.POSITION_X, -1, 0, 0, w * .1f)
            c.insertKeyframe(id, TrackProperty.POSITION_X, -1, 0, frames - 1, w * .9f)
        }
        for (id in particles + listOf(text3d, text).filter { it > 0 }) c.setLayerTimeRange(id, 0, frames)
        if (text > 0) {
            c.insertKeyframe(text, TrackProperty.POSITION_Y, -1, 0, 0, h * .1f)
            c.insertKeyframe(text, TrackProperty.POSITION_Y, -1, 0, frames - 1, h * .9f)
        }
        c.setCompositionDuration(compId, frames)
        assertTrue(engine.submitCommands() > 0)
        val all = layerIds()
        note("camadas=${all.size} malhas=${meshes.size} particulas=${particles.size} texto3d=$text3d texto=$text")
        // Motion blur em TUDO + efeitos empilhados em cada camada visual.
        assertTrue(engine.setMotionBlurSettings(MotionBlurSettings(true, 360f, -180f, 32, 64, 8)))
        var blurred = 0
        all.forEach { if (engine.setMotionBlur(it, true)) blurred++ }
        val effects = listOf("aurea.blur.gaussian", "aurea.light.glow", "aurea.light.deep_glow", "aurea.blur.directional").map { fnv1a(it) }
        engine.beginCommandBatch(); val fx = CommandBatch(engine)
        var added = 0
        all.forEach { id -> effects.forEach { type -> if (fx.addEffect(id, type)) added++ } }
        engine.submitCommands()
        note("motionBlur=$blurred/${all.size} efeitos=$added")
        SystemClock.sleep(2000)
        val movie = File(folder, "pesado.mp4"); movie.delete()
        val shortSide = args.getString("aureaShortSide")?.toInt() ?: 1080
        val began = SystemClock.elapsedRealtime()
        val started = engine.startExport(movie.absolutePath, shortSide, 0.0, 0, 0)
        note("startExport=$started")
        val buffer = ByteBuffer.allocateDirect(128).order(ByteOrder.nativeOrder())
        val progress = ExportProgress()
        if (started != 0 && engine.exportProgress(buffer)) { progress.readFrom(buffer); note("motivo=${progress.message}") }
        assertEquals("O export precisa abrir", 0, started)
        var last = -1; var moved = SystemClock.elapsedRealtime()
        try {
            while (true) {
                assertTrue(engine.exportProgress(buffer)); progress.readFrom(buffer)
                if (progress.framesDone != last) {
                    last = progress.framesDone; moved = SystemClock.elapsedRealtime()
                    if (last % 30 == 0) note("quadro $last/${progress.framesTotal} t=${(moved - began) / 1000}s ${progress.message}")
                }
                if (progress.finished) break
                assertTrue("TRAVOU em ${progress.framesDone}/${progress.framesTotal}: ${progress.message}",
                    SystemClock.elapsedRealtime() - moved < 300000)
                SystemClock.sleep(200)
            }
            note("fim resultado=${progress.result} falha=${progress.failure} quadros=${progress.framesDone}/${progress.framesTotal} " +
                "flags=${progress.flags} msg=${progress.message} bytes=${movie.length()} s=${(SystemClock.elapsedRealtime() - began) / 1000}")
            assertEquals(progress.message, 0, progress.result)
            assertEquals(progress.framesTotal, progress.framesDone)
            assertTrue(movie.isFile && movie.length() > 0)
        } finally { if (!progress.finished) engine.cancelExport() }
    }
}
