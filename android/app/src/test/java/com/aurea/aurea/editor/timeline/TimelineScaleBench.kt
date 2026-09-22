package com.aurea.aurea.editor.timeline

import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.engine.directBuffer
import com.aurea.aurea.state.KeyframeSnapshot
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

    private inline fun bench(name: String, iterations: Int = 200, block: () -> Unit): Double {
        repeat(100) { block() }
        val samples = DoubleArray(iterations)
        val mx = java.lang.management.ManagementFactory.getThreadMXBean() as com.sun.management.ThreadMXBean
        val tid = Thread.currentThread().id
        val a0 = mx.getThreadAllocatedBytes(tid)
        for (i in 0 until iterations) {
            val t0 = System.nanoTime()
            block()
            samples[i] = (System.nanoTime() - t0) / 1e6
        }
        val kb = (mx.getThreadAllocatedBytes(tid) - a0) / iterations / 1024.0
        samples.sort()
        val median = samples[iterations / 2]
        println("BENCH %-58s mediana %8.3f ms  (p90 %8.3f)  aloca %8.1f KB".format(name, median, samples[iterations * 9 / 10], kb))
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

    // ------------------------------------------------------------------------
    // DEPOIS: uma consulta para todos os keyframes (índice + linhas), só o que
    // mudou é decodificado (`KeyframeSnapshot`) e só as linhas que mudaram são
    // refeitas (`RowCache`).
    // ------------------------------------------------------------------------
    private fun allKeyPods(shiftLayer: Int = -1): Pair<ByteBuffer, ByteBuffer> {
        val index = directBuffer(layers * KeyframeSnapshot.INDEX_BYTES)
        val rows = directBuffer(layers * keysPerLayer * PodLayout.KEYFRAME_ROW_BYTES)
        for (i in 0 until layers) {
            index.putLong(i * KeyframeSnapshot.INDEX_BYTES, (1L shl 32) or i.toLong())
            index.putInt(i * KeyframeSnapshot.INDEX_BYTES + 8, keysPerLayer)
            val src = keyPods(i, if (i == shiftLayer) 1 else 0)
            src.position(0)
            rows.position(i * keysPerLayer * PodLayout.KEYFRAME_ROW_BYTES)
            rows.put(src)
        }
        rows.position(0)
        return index to rows
    }

    @Test
    fun `depois - releitura incremental`() {
        val (layerBuf, names) = layerPods()
        val (movedBuf, movedNames) = layerPods(moveLayer = 500, delta = 3)
        val (index, keyRows) = allKeyPods()
        val (index2, keyRows2) = allKeyPods(shiftLayer = 10)
        // Revisão típica de um arrasto: um clipe andou, nenhum keyframe mudou.
        val snap = KeyframeSnapshot()
        val cache = RowCache()
        var flip = false
        var rows: List<RowModel> = emptyList()
        snap.update(index, layers, keyRows)
        cache.build(List(layers) { LayerRow.read(layerBuf, it, names) }, snap.map)
        bench("depois: arrastar 1 clipe (1000 camadas, 10000 kf)") {
            flip = !flip
            val buf = if (flip) movedBuf else layerBuf
            val nm = if (flip) movedNames else names
            val ls = List(layers) { LayerRow.read(buf, it, nm) }
            val changed = snap.update(index, layers, keyRows)
            check(!changed)
            rows = cache.build(ls, snap.map)
        }
        assertEquals(layers, rows.size)
        // Revisão que move um keyframe de uma camada.
        bench("depois: mover 1 keyframe (1000 camadas, 10000 kf)") {
            flip = !flip
            val ls = List(layers) { LayerRow.read(layerBuf, it, names) }
            snap.update(if (flip) index2 else index, layers, if (flip) keyRows2 else keyRows)
            rows = cache.build(ls, snap.map)
        }
        // Frio: projeto recém-aberto (tudo novo).
        bench("depois: frio (abrir projeto, 1000 camadas, 10000 kf)") {
            val s = KeyframeSnapshot()
            val c = RowCache()
            val ls = List(layers) { LayerRow.read(layerBuf, it, names) }
            s.update(index, layers, keyRows)
            rows = c.build(ls, s.map)
        }
        assertEquals(keysPerLayer, rows[0].instants.size)
        // Partes do caminho quente (para saber onde está o custo).
        bench("  parte: ler 1000 LayerRow") { rows = emptyList(); List(layers) { LayerRow.read(layerBuf, it, names) } }
        bench("  parte: snapshot sem mudança (10000 kf)") { snap.update(index, layers, keyRows) }
        val ls0 = List(layers) { LayerRow.read(layerBuf, it, names) }
        bench("  parte: RowCache sem mudança (1000 linhas)") { cache.build(ls0, snap.map) }
    }

    @Test
    fun `snapshot devolve a mesma lista quando nada mudou e a nova quando mudou`() {
        val (index, keyRows) = allKeyPods()
        val (index2, keyRows2) = allKeyPods(shiftLayer = 10)
        val snap = KeyframeSnapshot()
        assertEquals(true, snap.update(index, layers, keyRows))
        val first = snap.map
        assertEquals(false, snap.update(index, layers, keyRows))
        assert(first === snap.map)
        assertEquals(true, snap.update(index2, layers, keyRows2))
        val id10 = (1L shl 32) or 10L
        val id11 = (1L shl 32) or 11L
        assert(first[id10] !== snap.map[id10])
        assert(first[id11] === snap.map[id11])
        assertEquals(first[id10]!![0].time + 1, snap.map[id10]!![0].time)
        // Linhas: só a camada do keyframe mudado é refeita.
        val (layerBuf, names) = layerPods()
        val ls = List(layers) { LayerRow.read(layerBuf, it, names) }
        val cache = RowCache()
        val a = cache.build(ls, first)
        val b = cache.build(List(layers) { LayerRow.read(layerBuf, it, names) }, snap.map)
        assert(a[11] === b[11])
        assert(a[10] !== b[10])
        assertEquals(buildRows(ls, snap.map)[10].instants.toList(), b[10].instants.toList())
        // Camada some: o mapa muda.
        assertEquals(true, snap.update(index, layers - 1, keyRows))
        assertEquals(layers - 1, snap.map.size)
    }
}
