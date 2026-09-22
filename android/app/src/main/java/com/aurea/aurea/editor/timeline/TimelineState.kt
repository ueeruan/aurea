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
    /** Lido no desenho: mudar pede mais um quadro (miniaturas que ficaram para depois). */
    var redrawTick by mutableIntStateOf(0)
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

    /**
     * Perguntas ao motor que ainda cabem neste quadro (fase 8D). Cada uma trava
     * o modelo do motor (que o quadro do preview segura enquanto prepara) e a
     * que acerta cria um Bitmap na thread da UI: rolando rápido, dezenas de
     * baldes novos por quadro viravam engasgo. O resto espera o próximo quadro
     * ([starved] pede o redesenho) — a miniatura perfeita não passa na frente
     * da rolagem.
     */
    var budget = Int.MAX_VALUE
    var starved = false
        private set

    fun beginFrame(queries: Int) {
        budget = queries
        starved = false
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
        if (budget <= 0) {
            starved = true
            return null
        }
        budget--
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

/** Quem responde a waveform (o store; nos testes, um falso). */
internal fun interface WaveSource {
    fun query(layer: Long, startFrame: Double, framesPerBucket: Double, count: Int, out: java.nio.ByteBuffer): Int
}

/**
 * Waveform da timeline guardada por linha em grade fixa do tempo
 * ([WaveGrid]), numa janela maior que a tela (fase 8D). Antes cada linha de
 * áudio/vídeo visível perguntava ao motor A CADA QUADRO (tocando, rolando) —
 * travando o modelo do motor, que o quadro do preview segura enquanto
 * prepara. Agora pergunta quando a vista sai da janela, o degrau de zoom
 * muda, o modelo muda ou chega pedaço novo da waveform.
 */
internal class WaveStrip(private val capacity: Int) {
    class Entry(capacity: Int) {
        var model: Any? = null
        var generation = -1
        var fpb = 0.0
        /** Janela guardada: baldes `[w0, w1)`. */
        var w0 = 0L
        var w1 = 0L
        var count = 0
        val data = ByteArray(capacity)

        fun at(k: Long): Int = if (k < w0 || k >= w0 + count) 0 else data[(k - w0).toInt()].toInt() and 0xFF
    }

    // Poucas linhas de som na tela: busca linear num array (sem encaixotar o id a cada quadro).
    private val ids = LongArray(MAX_ROWS)
    private val entries = arrayOfNulls<Entry>(MAX_ROWS)
    private var used = 0
    private var nextSlot = 0
    private val buf: java.nio.ByteBuffer = java.nio.ByteBuffer.allocateDirect(capacity)
    private val win = LongArray(2)
    /** Perguntas feitas ao motor (telemetria dos testes). */
    var queries = 0
        private set

    /** A entrada da camada com `[first, last]` coberto, pedindo ao motor só se preciso; null = sem som. */
    fun get(
        source: WaveSource, layer: Long, model: Any?, generation: Int,
        fpb: Double, first: Long, last: Long,
    ): Entry? {
        if (last < first || last - first + 1 > capacity) return null
        var e: Entry? = null
        for (i in 0 until used) if (ids[i] == layer) { e = entries[i]; break }
        if (e != null && e.model === model && e.generation == generation && e.fpb == fpb &&
            first >= e.w0 && last < e.w1
        ) {
            return if (e.count > 0) e else null
        }
        if (e == null) {
            val slot = if (used < MAX_ROWS) used++ else nextSlot.also { nextSlot = (nextSlot + 1) % MAX_ROWS }
            e = entries[slot] ?: Entry(capacity).also { entries[slot] = it }
            ids[slot] = layer
        }
        WaveGrid.window(first, last, capacity, win)
        val n = (win[1] - win[0]).toInt()
        queries++
        buf.clear()
        val got = source.query(layer, win[0] * fpb, fpb, n, buf)
        e.model = model
        e.generation = generation
        e.fpb = fpb
        e.w0 = win[0]
        e.w1 = win[1]
        e.count = if (got > 0) minOf(got, n) else 0
        if (e.count > 0) {
            buf.position(0)
            buf.get(e.data, 0, e.count)
        }
        return if (e.count > 0) e else null
    }

    private companion object {
        const val MAX_ROWS = 32
    }
}
