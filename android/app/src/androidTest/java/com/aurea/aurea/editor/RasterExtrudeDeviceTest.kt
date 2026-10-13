package com.aurea.aurea.editor

import android.graphics.Bitmap
import android.graphics.Color
import android.os.Build
import android.os.SystemClock
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.CommandBatch
import com.aurea.aurea.engine.EffectParam
import com.aurea.aurea.engine.LayerDetail
import com.aurea.aurea.engine.LayerEffect
import com.aurea.aurea.engine.PodLayout
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.*
import org.junit.Test

/** Own native raster/capture path; no claim about editor controls or OEM drivers. */
class RasterExtrudeDeviceTest {
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private fun buffer(bytes: Int) = ByteBuffer.allocateDirect(bytes).order(ByteOrder.LITTLE_ENDIAN)
    private fun effects(engine: AureaEngine, layer: Long): List<LayerEffect> {
        val rows = buffer(32 * LayerEffect.ROW_BYTES); val blob = buffer(4096)
        return List(engine.queryLayerEffects(layer, rows, 32, blob)) { LayerEffect.read(rows, it, blob) }
    }
    private fun params(engine: AureaEngine, layer: Long, effect: Int): List<EffectParam> {
        val rows = buffer(16 * EffectParam.ROW_BYTES); val blob = buffer(4096)
        return List(engine.queryEffectParams(layer, effect, rows, 16, blob)) { EffectParam.read(rows, it, blob) }
    }
    private fun detail(engine: AureaEngine, layer: Long): LayerDetail {
        val bytes = buffer(LayerDetail.BYTES); assertTrue(engine.queryLayerDetail(layer, bytes)); return LayerDetail.read(bytes)
    }
    private fun commands(engine: AureaEngine, block: CommandBatch.() -> Unit) {
        engine.beginCommandBatch(); CommandBatch(engine).block(); assertTrue(engine.submitCommands() > 0)
    }
    private fun capture(engine: AureaEngine, name: String, directory: File): ByteArray {
        val pixels = buffer(256 * 192 * 4); val size = IntArray(2)
        val began = SystemClock.elapsedRealtime(); var bytes = 0
        while (bytes == 0 && SystemClock.elapsedRealtime() - began < 20000) {
            bytes = engine.captureFrame(256, pixels, size)
            if (bytes == 0) SystemClock.sleep(30)
        }
        assertEquals(256 * 192 * 4, bytes); assertEquals(256, size[0]); assertEquals(192, size[1])
        val rgba = ByteArray(bytes); pixels.rewind(); pixels.get(rgba)
        val bitmap = Bitmap.createBitmap(256, 192, Bitmap.Config.ARGB_8888)
        bitmap.setPremultiplied(false); pixels.rewind(); bitmap.copyPixelsFromBuffer(pixels)
        File(directory, "$name.png").outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }; bitmap.recycle()
        return rgba
    }
    private fun green(pixels: ByteArray): Int = (pixels.indices step 4).count {
        val r = pixels[it].toInt() and 255; val g = pixels[it + 1].toInt() and 255; val b = pixels[it + 2].toInt() and 255
        g > 120 && g > r + 60 && g > b + 60
    }
    private fun run(label: String, block: (AureaEngine, File) -> Unit) {
        check(context.packageName.endsWith(".uitest"))
        val directory = File(context.getExternalFilesDir(null), "raster-extrude-native/$label").apply { mkdirs() }
        val engine = AureaEngine.create(context)
        try {
            val cache = File(context.cacheDir, "raster-extrude-probe").apply { mkdirs() }
            // Same app-owned font/bootstrap contract as EditorStore, without
            // opening an editor UI just to exercise the native renderer.
            context.assets.open("Roboto-Regular.ttf").use { input -> File(cache, "Roboto-Regular.ttf").outputStream().use { input.copyTo(it) } }
            assertTrue(engine.initialize(60f, cache.absolutePath,
                File(context.filesDir, "raster-extrude-probe").apply { mkdirs() }.absolutePath, true))
            File(directory, "device.txt").writeText("${Build.MANUFACTURER} ${Build.MODEL} API${Build.VERSION.SDK_INT} ${Build.SUPPORTED_ABIS.joinToString()}\n${engine.deviceSummary()}\n")
            assertTrue(engine.newProject(256, 192, 30.0, "Own raster volume $label", floatArrayOf(0f, 0f, 0f, 0f)))
            block(engine, directory)
        } finally { engine.destroy() }
    }
    @Test fun nativeAlphaVolumePreservesCapsAndUsesAllThreeBoundedQualities() = run("alpha") { engine, directory ->
        val pixels = buffer(64 * 48 * 4)
        val source = Bitmap.createBitmap(64, 48, Bitmap.Config.ARGB_8888).apply { setPremultiplied(false) }
        for (y in 0 until 48) for (x in 0 until 64) {
            val a = if (x in 24 until 40 && y in 16 until 32) 0 else 128
            pixels.put(255.toByte()).put(0).put(0).put(a.toByte()); source.setPixel(x, y, Color.argb(a, 255, 0, 0))
        }
        val sourcePath = File(directory, "own-alpha.png")
        sourcePath.outputStream().use { assertTrue(source.compress(Bitmap.CompressFormat.PNG, 100, it)) }; source.recycle(); pixels.rewind()
        val layer = engine.importImage(pixels, 64, 48, "Own alpha hole", sourcePath.absolutePath); assertTrue(layer > 0)
        commands(engine) { addEffect(layer, effectTypeId("aurea.generate.raster_extrude")) }
        val effect = effects(engine, layer).single(); assertEquals(4, effect.paramCount)
        assertTrue(detail(engine, layer).flags and PodLayout.FLAG_THREE_D != 0)
        val declarations = params(engine, layer, effect.effectId)
        assertEquals(32f, declarations[0].defaultValue[0], .0001f); assertEquals(4096f, declarations[0].hardMax, .0001f); assertEquals(3, declarations[3].enumLabels.size)
        commands(engine) { setEffectVector(layer, effect.effectId, 1, 0f, 1f, 0f, 1f); setEffectParam(layer, effect.effectId, 2, 0f) }
        val cap = capture(engine, "cap", directory)
        assertEquals(0, cap[(96 * 256 + 128) * 4 + 3].toInt() and 255)
        assertTrue((cap[(96 * 256 + 112) * 4 + 3].toInt() and 255) in 126..130)
        val report = File(directory, "result.txt"); report.writeText("Native Vulkan/GLES selected by engine; straight RGBA cap/alpha hole\n")
        for (quality in 0..2) {
            commands(engine) { setRotation(layer, 0f, 60f, 0f); setEffectParam(layer, effect.effectId, 3, quality.toFloat()) }
            val frame = capture(engine, "quality-$quality", directory); val sides = green(frame)
            report.appendText("quality=$quality greenSides=$sides\n"); assertTrue(sides > 350)
        }
    }
    @Test fun nativeTextAndStrokeActivationShareTheUndoAndParameterRules() = run("text-stroke") { engine, directory ->
        val layer = engine.addText("B8"); assertTrue(layer > 0)
        commands(engine) { addEffect(layer, effectTypeId("aurea.stylize.stroke_outline")) }
        val effect = effects(engine, layer).single(); assertEquals(6, effect.paramCount)
        assertFalse(detail(engine, layer).flags and PodLayout.FLAG_THREE_D != 0)
        commands(engine) { setEffectVector(layer, effect.effectId, 1, 0f, 1f, 0f, 1f); setEffectParam(layer, effect.effectId, 5, 32f) }
        assertTrue(detail(engine, layer).flags and PodLayout.FLAG_THREE_D != 0)
        commands(engine) { undo() }
        assertFalse(detail(engine, layer).flags and PodLayout.FLAG_THREE_D != 0)
        assertEquals(0f, params(engine, layer, effect.effectId).single { it.index == 5 }.value[0], .0001f)
        commands(engine) { setEffectParam(layer, effect.effectId, 5, 32f); setRotation(layer, 0f, 55f, 0f) }
        val image = capture(engine, "glyph-volume", directory); val sides = green(image)
        assertTrue(sides > 200)
        File(directory, "result.txt").writeText("Text kind=${detail(engine, layer).kind}; Stroke depth5 append-only, undo restored2D; greenSides=$sides\n")
    }
}
