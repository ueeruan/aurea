package com.aurea.aurea.editor.timeline

import android.graphics.Bitmap
import android.util.LongSparseArray
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableDoubleStateOf
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.aurea.aurea.state.ThumbnailCache

/**
 * Estado de APRESENTAÇÃO da timeline — nada aqui é do projeto (spec 03 §8.3).
 * Tempo, camadas, seleção e keyframes vêm SEMPRE do store; aqui fica zoom,
 * rolagem, a vista enquanto um gesto a segura e as prévias do gesto.
 */
@Stable
internal class TimelineState {
    /** Zoom em dp por segundo. */
    var pps by mutableFloatStateOf(Zoom.DEFAULT_PPS)
    /** Rolagem vertical das linhas (px). */
    var scrollY by mutableFloatStateOf(0f)
    /**
     * Vista presa por um gesto (scrub, inércia, pinça, auto-rolagem), em frames
     * fracionários para o conteúdo andar liso sob o dedo. NaN = a vista É o
     * playhead do motor (um só "agora"; spec §8.3-1).
     */
    var heldView by mutableDoubleStateOf(Double.NaN)
    var compact by mutableStateOf(false)
    /** Fio do ímã (frame), [Snap.NONE] sem guia. */
    var guideFrame by mutableIntStateOf(Snap.NONE)
    /** Reordenar: linha segurada e destino sob o dedo (−1 = sem reordenar). */
    var reorderSource by mutableIntStateOf(-1)
    var reorderTarget by mutableIntStateOf(-1)
    /** Losango na mão (cresce e mostra o tempo). */
    var dragKeyLayer by mutableLongStateOf(0L)
    var dragKeyFrame by mutableIntStateOf(Snap.NONE)
    /** Tamanho medido no layout (nunca escrito na composição; spec bug 10.24). */
    var width by mutableIntStateOf(0)
    var height by mutableIntStateOf(0)
    /** Projeto já enquadrado pelo auto-zoom da A.01 (uma vez por projeto). */
    var fittedPath: String? = null
}

/**
 * Miniaturas da tira, indexadas pelo balde de 250 ms da MÍDIA (não pelo frame
 * da timeline): mover ou aparar o clipe não troca a chave, então a tira não
 * pisca nem pede de novo ao motor. LRU limitado — as imagens são do cache do
 * store; aqui só se guarda a referência.
 */
internal class ThumbStrip {
    private class Key(var layer: Long = 0L, var bucket: Int = 0, var height: Int = 0) {
        fun set(layer: Long, bucket: Int, height: Int): Key {
            this.layer = layer
            this.bucket = bucket
            this.height = height
            return this
        }

        fun copy() = Key(layer, bucket, height)
        override fun equals(other: Any?) =
            other is Key && other.layer == layer && other.bucket == bucket && other.height == height

        override fun hashCode(): Int = ((layer xor (layer ushr 32)).toInt() * 31 + bucket) * 31 + height
    }

    // Busca com uma chave reaproveitada: acerto no cache não aloca (a tira repinta a 60 Hz).
    private val probe = Key()
    private val hits = object : LinkedHashMap<Key, Bitmap>(128, 0.75f, true) {
        override fun removeEldestEntry(eldest: MutableMap.MutableEntry<Key, Bitmap>?) = size > MAX_HITS
    }
    /** Balde que o motor ainda não tinha → geração em que foi pedido (não repergunta até ela mudar). */
    private val misses = HashMap<Key, Int>()
    private val aspects = LongSparseArray<Float>()

    /** Largura/altura da miniatura da camada (16:9 até a primeira chegar). */
    fun aspect(layer: Long): Float = aspects.get(layer) ?: DEFAULT_ASPECT

    fun get(cache: ThumbnailCache, layer: Long, bucket: Int, timelineFrame: Int, heightPx: Int, generation: Int): Bitmap? {
        probe.set(layer, bucket, heightPx)
        hits[probe]?.let { return it }
        val missed = misses[probe]
        if (missed != null && missed == generation) return null
        val bmp = cache.get(layer, timelineFrame, heightPx)
        val key = probe.copy()
        if (bmp == null) {
            if (misses.size > MAX_MISSES) misses.clear()
            misses[key] = generation
            return null
        }
        misses.remove(key)
        hits[key] = bmp
        if (aspects.get(layer) == null && bmp.height > 0) {
            aspects.put(layer, (bmp.width.toFloat() / bmp.height).coerceIn(MIN_ASPECT, MAX_ASPECT))
        }
        return bmp
    }

    private companion object {
        const val MAX_HITS = 240
        const val MAX_MISSES = 512
        const val DEFAULT_ASPECT = 16f / 9f
        const val MIN_ASPECT = 0.3f
        const val MAX_ASPECT = 4f
    }
}
