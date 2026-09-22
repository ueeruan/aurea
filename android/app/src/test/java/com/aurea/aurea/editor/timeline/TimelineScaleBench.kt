package com.aurea.aurea.editor.timeline

import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.engine.directBuffer
import org.junit.Assert.assertEquals
import org.junit.Test
import java.nio.ByteBuffer

/**
 * Fase 8D — o lado Kotlin da timeline em escala (1000 clipes, 10.000
 * keyframes), medido na JVM do host. A JVM não é o ART do celular: os números
 * servem para o ANTES/DEPOIS e a ordem de grandeza, não como tempo de aparelho.
 * O motor em si é medido em `engine/tests/test_timeline_scale.cpp`.
 *
 * Imprime `BENCH ...` (sai no XML do Gradle, em build/test-results).
 */
class TimelineScaleBench {

    private val layers = 1000
    private val keysPerLayer = 10

    /** Linhas POD como o motor escreve (query_layers). */
    private fun layerPods(moveLayer: Int = -1, delta: Int = 0): Pair<ByteBuffer, ByteBuffer> {
        val buf = directBuffer(layers * PodLayout.LAYER_ROW_BYTES)
        val names = directBuffer(64 * 1024)
        var cursor = 0
        for (i in 0 until layers) {
            val b = i * PodLayout.LAYER_ROW_BYTES
            val name = "Camada ${i + 1}".toByteArray()
            names.position(cursor)
            names.put(name)
            val start = i * 7 + if (i == moveLayer) delta else 0
            buf.putLong(b + PodLayout.LAYER_OFF_ID, (1L shl 32) or i.toLong())
            buf.putInt(b + PodLayout.LAYER_OFF_KIND, if (i % 3 == 0) 4 else 5)
            buf.putInt(b + PodLayout.LAYER_OFF_Z_INDEX, i)
            buf.putInt(b + PodLayout.LAYER_OFF_START, start)
            buf.putInt(b + PodLayout.LAYER_OFF_END, start + 300)
            buf.putFloat(b + PodLayout.LAYER_OFF_OPACITY, 1f)
            buf.putInt(b + PodLayout.LAYER_OFF_FLAGS, PodLayout.FLAG_VISIBLE or PodLayout.FLAG_ANIMATED)
            buf.putInt(b + PodLayout.LAYER_OFF_KEYFRAME_COUNT, keysPerLayer)
            buf.putInt(b + PodLayout.LAYER_OFF_NAME_OFFSET, cursor)
            buf.putInt(b + PodLayout.LAYER_OFF_NAME_LENGTH, name.size)
            buf.putInt(b + PodLayout.LAYER_OFF_PARENT_INDEX, -1)
            cursor += name.size
        }
        names.position(0)
        return buf to names
    }

    /** Keyframes de UMA camada como o motor escreve (query_keyframes). */
    private fun keyPods(layer: Int, shift: Int = 0): ByteBuffer {
        val buf = directBuffer(keysPerLayer * PodLayout.KEYFRAME_ROW_BYTES)
        for (k in 0 until keysPerLayer) {
            val b = k * PodLayout.KEYFRAME_ROW_BYTES
            buf.putInt(b + PodLayout.KF_OFF_PROPERTY, if (k % 2 == 0) 12 else 0)
            buf.putInt(b + PodLayout.KF_OFF_EFFECT_INDEX, -1)
            buf.putInt(b + PodLayout.KF_OFF_TIME, (k / 2) * 6 + (k % 2) * 3 + shift + layer % 2)
            buf.putFloat(b + PodLayout.KF_OFF_VALUE, k.toFloat())
            buf.putInt(b + PodLayout.KF_OFF_INTERPOLATION, 1)
            buf.putInt(b + PodLayout.KF_OFF_PARAM_INDEX, 0)
        }
        return buf
    }

    private inline fun bench(name: String, iterations: Int = 40, block: () -> Unit): Double {
        repeat(8) { block() }
        val samples = DoubleArray(iterations)
        for (i in 0 until iterations) {
            val t0 = System.nanoTime()
            block()
            samples[i] = (System.nanoTime() - t0) / 1e6
        }
        samples.sort()
        val median = samples[iterations / 2]
        println("BENCH %-58s mediana %8.3f ms  (p90 %8.3f)".format(name, median, samples[iterations * 9 / 10]))
        return median
    }

    // ------------------------------------------------------------------------
    // ANTES: o caminho de `EditorStore.refreshModel` até a fase 8 — uma
    // consulta de keyframes POR camada, tudo redecodificado e as linhas da
    // timeline refeitas do zero a cada `modelRevision`.
    // ------------------------------------------------------------------------
    @Test
    fun `antes - releitura completa a cada revisao`() {
        val (layerBuf, names) = layerPods()
        val perLayer = Array(layers) { keyPods(it) }
        val keyBuffer = directBuffer(4096 * PodLayout.KEYFRAME_ROW_BYTES)
        var rows: List<RowModel> = emptyList()
        bench("antes: refreshModel (1000 camadas, 10000 kf) + buildRows") {
            val ls = List(layers) { LayerRow.read(layerBuf, it, names) }
            val kf = ls.associate { l ->
                // "JNI": o motor copia as linhas da camada no buffer reusado.
                val src = perLayer[(l.id and 0xFFFFFFFFL).toInt()]
                keyBuffer.clear()
                src.position(0)
                keyBuffer.put(src)
                l.id to List(keysPerLayer) { KeyframeRow.read(keyBuffer, it) }
            }
            rows = buildRows(ls, kf)
        }
        assertEquals(layers, rows.size)
        assertEquals(keysPerLayer, rows[0].instants.size)
    }
}
