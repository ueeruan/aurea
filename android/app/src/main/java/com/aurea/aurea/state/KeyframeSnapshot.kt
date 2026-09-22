package com.aurea.aurea.state

import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.PodLayout
import java.nio.ByteBuffer

/**
 * Keyframes de TODAS as camadas, como a timeline os mostra — relidos a cada
 * `modelRevision` numa consulta só (`queryAllKeyframes`) e decodificados SÓ
 * onde mudaram (fase 8D).
 *
 * Antes: uma consulta JNI por camada e um `KeyframeRow` novo para cada
 * keyframe do projeto a cada revisão — com 1000 clipes e 10.000 keyframes,
 * arrastar UM clipe refazia 10.000 objetos por quadro. Agora cada camada
 * guarda os bytes crus da última leitura; bytes iguais = a MESMA lista de
 * antes (identidade), então o mapa novo compara igual ao velho, o estado do
 * Compose não muda e a timeline reaproveita as linhas (`RowCache`).
 *
 * Pura (só `ByteBuffer`): testada na JVM.
 */
internal class KeyframeSnapshot {
    private class Entry(val raw: IntArray, val list: List<KeyframeRow>)

    private var entries = HashMap<Long, Entry>()
    /** Ordem da última leitura (a timeline itera nela) e a entrada de cada posição. */
    private var order = LongArray(0)
    private var ordered = arrayOfNulls<Entry>(0)

    /** Mapa camada → keyframes (tempo local), na ordem da timeline. */
    var map: Map<Long, List<KeyframeRow>> = emptyMap()
        private set

    /**
     * Lê o resultado de `queryAllKeyframes`: `index` com 16 bytes por camada
     * (id u64, quantidade u32) e `rows` com as linhas concatenadas. Devolve se
     * o [map] mudou (senão ele é o MESMO objeto de antes e nada foi alocado).
     */
    fun update(index: ByteBuffer, layerCount: Int, rows: ByteBuffer): Boolean {
        if (unchanged(index, layerCount, rows)) return false
        val next = HashMap<Long, Entry>(layerCount * 2)
        val out = LinkedHashMap<Long, List<KeyframeRow>>(layerCount * 2)
        val ids = LongArray(layerCount)
        val byPos = arrayOfNulls<Entry>(layerCount)
        var cursor = 0
        for (i in 0 until layerCount) {
            val id = index.getLong(i * INDEX_BYTES)
            val n = index.getInt(i * INDEX_BYTES + 8)
            val prev = entries[id]
            val e = if (prev != null && sameRows(rows, cursor, n, prev.raw)) prev else decode(rows, cursor, n)
            next[id] = e
            out[id] = e.list
            ids[i] = id
            byPos[i] = e
            cursor += n
        }
        entries = next
        order = ids
        ordered = byPos
        map = out
        return true
    }

    /** Mesmas camadas, na mesma ordem, com os mesmos bytes — sem alocar. */
    private fun unchanged(index: ByteBuffer, layerCount: Int, rows: ByteBuffer): Boolean {
        if (layerCount != order.size) return false
        var cursor = 0
        for (i in 0 until layerCount) {
            val id = index.getLong(i * INDEX_BYTES)
            if (id != order[i]) return false
            val n = index.getInt(i * INDEX_BYTES + 8)
            val prev = ordered[i] ?: return false
            if (!sameRows(rows, cursor, n, prev.raw)) return false
            cursor += n
        }
        return true
    }

    fun clear() {
        entries = HashMap()
        order = LongArray(0)
        ordered = arrayOfNulls(0)
        map = emptyMap()
    }

    private fun sameRows(rows: ByteBuffer, first: Int, count: Int, raw: IntArray): Boolean {
        if (raw.size != count * WORDS) return false
        val base = first * PodLayout.KEYFRAME_ROW_BYTES
        for (w in raw.indices) if (rows.getInt(base + w * 4) != raw[w]) return false
        return true
    }

    private fun decode(rows: ByteBuffer, first: Int, count: Int): Entry {
        val base = first * PodLayout.KEYFRAME_ROW_BYTES
        val raw = IntArray(count * WORDS) { rows.getInt(base + it * 4) }
        val list = List(count) { KeyframeRow.read(rows, first + it) }
        return Entry(raw, list)
    }

    companion object {
        /** `bridge::KeyframeIndexRow`. */
        const val INDEX_BYTES = 16
        private const val WORDS = PodLayout.KEYFRAME_ROW_BYTES / 4
    }
}
