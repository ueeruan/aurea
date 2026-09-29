package com.aurea.aurea.editor.panels

import com.aurea.aurea.engine.KeyframeRow
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min

// O gráfico de valor/velocidade desenha a MESMA conta do motor, não uma
// amostra grossa dela. `query_track_curve` devolvia 160 valores em frames
// inteiros: numa trilha longa pulava frames (o "manter" virava rampa, o quique
// perdia os saltos) e a velocidade saía de diferenças entre frames — as alças
// de velocidade nunca encostavam na curva. Aqui a curva é `Track::sample_keys`
// (engine/src/animation/Curve.cpp) com `apply_easing` (core/Math.hpp) — o
// mesmo float nos frames inteiros, que são os únicos que o motor pede — e os
// frames inteiros da janela entram como vértices do traço.

/** Uma marca como o avaliador lê: tempo local, valor e a curva de SAÍDA dela. */
internal data class CurveKey(val time: Int, val value: Float, val ease: Ease)

/** As marcas de UMA trilha (em ordem) com a curva de saída de cada uma. */
internal fun curveKeys(track: List<KeyframeRow>, easeOf: (KeyframeRow) -> Ease): List<CurveKey> =
    track.sortedBy { it.time }.map { CurveKey(it.time, it.value, easeOf(it)) }

/** Índice da marca em ou antes de [frame] (−1 = antes da primeira). */
private fun before(keys: List<CurveKey>, frame: Double): Int {
    var lo = 0
    var hi = keys.lastIndex
    var best = -1
    while (lo <= hi) {
        val mid = (lo + hi) ushr 1
        if (keys[mid].time <= frame) { best = mid; lo = mid + 1 } else hi = mid - 1
    }
    return best
}

/** O trecho [a]→[b] em [frame] (dentro dele), exatamente como `sample_keys`. */
private fun segmentValue(a: CurveKey, b: CurveKey, frame: Double): Float {
    if (a.ease.interp == Interp.HOLD) return a.value
    val span = b.time.toLong() - a.time
    if (span <= 0) return b.value
    val t01 = ((frame - a.time) / span.toDouble()).toFloat()
    val eased = a.ease.transform(t01)
    return a.value + (b.value - a.value) * eased
}

/** `Track::sample_keys` num instante qualquer (nos inteiros, o valor do motor). */
internal fun sampleCurve(keys: List<CurveKey>, frame: Double): Float {
    if (keys.isEmpty()) return 0f
    if (keys.size == 1) return keys[0].value
    val i = before(keys, frame)
    if (i < 0) return keys[0].value
    if (i >= keys.lastIndex) return keys.last().value
    return segmentValue(keys[i], keys[i + 1], frame)
}

/** Velocidade (unidades/s) dentro do trecho [a]→[b]: a derivada da mesma conta. */
private fun segmentVelocity(a: CurveKey, b: CurveKey, frame: Double, fps: Float): Float {
    val span = (b.time.toLong() - a.time).toDouble()
    if (span <= 0.0 || a.ease.interp == Interp.HOLD || a.ease.interp == Interp.STEPS) return 0f
    val t = ((frame - a.time) / span).coerceIn(0.0, 1.0).toFloat()
    val h = 1e-3f
    val lo = max(0f, t - h)
    val hi = min(1f, t + h)
    val slope = (a.ease.transform(hi) - a.ease.transform(lo)) / (hi - lo)
    return ((b.value - a.value) * slope * fps / span).toFloat()
}

/** Velocidade em [frame] (fora da animação, parada). */
internal fun sampleVelocity(keys: List<CurveKey>, frame: Double, fps: Float): Float {
    if (keys.size < 2) return 0f
    val i = before(keys, frame)
    if (i < 0 || i >= keys.lastIndex) return 0f
    return segmentVelocity(keys[i], keys[i + 1], frame, fps)
}

/**
 * O traço do gráfico em [from]..[to] (frames). Cada trecho é subdividido pela
 * tela ([pointsPerFrame]) e SEMPRE passa pelos frames inteiros dentro dele
 * (até um limite), então o vértice desenhado num frame é o valor que o motor
 * renderiza ali. Descontinuidades (manter, degraus e a velocidade entre
 * trechos) viram dois vértices no mesmo x — um degrau reto, não uma rampa.
 */
internal fun graphCurve(keys: List<CurveKey>, from: Double, to: Double, pointsPerFrame: Double, speed: Boolean, fps: Float): List<GraphSample> {
    if (keys.isEmpty() || !from.isFinite() || !to.isFinite() || to <= from) return emptyList()
    val out = ArrayList<GraphSample>()
    fun add(frame: Double, value: Float) {
        if (value.isFinite()) out += GraphSample(frame.toFloat(), value)
    }
    val density = if (pointsPerFrame.isFinite() && pointsPerFrame > 0) pointsPerFrame else 1.0
    fun edge(frame: Double) = if (speed) 0f else sampleCurve(keys, frame)
    add(from, edge(from))
    for (i in 0 until keys.size - 1) {
        val a = keys[i]
        val b = keys[i + 1]
        val t0 = a.time.toDouble()
        val t1 = b.time.toDouble()
        if (t1 <= from || t0 >= to || t1 <= t0) continue
        val lo = max(t0, from)
        val hi = min(t1, to)
        // Parada antes da primeira marca: a velocidade sobe na vertical ali.
        if (speed && i == 0 && t0 > from) add(t0, 0f)
        when (a.ease.interp) {
            Interp.HOLD -> {
                if (speed) { add(lo, 0f); add(hi, 0f) }
                else { add(lo, a.value); add(hi, a.value) }
            }
            Interp.STEPS -> {
                // Quatro degraus: o motor usa floor(t·4)/4.
                for (k in 0 until 4) {
                    val s = t0 + (t1 - t0) * k / 4.0
                    val e = t0 + (t1 - t0) * (k + 1) / 4.0
                    if (e <= lo || s >= hi) continue
                    val v = if (speed) 0f else a.value + (b.value - a.value) * (k / 4f)
                    add(max(s, lo), v); add(min(e, hi), v)
                }
            }
            else -> {
                val frames = ArrayList<Double>()
                val n = ceil((hi - lo) * density).toInt().coerceIn(8, 2048)
                for (j in 0..n) frames += lo + (hi - lo) * j / n
                val first = ceil(lo).toLong()
                val last = floor(hi).toLong()
                if (last - first <= 4096) for (f in first..last) frames += f.toDouble()
                frames.sort()
                var previous = Double.NaN
                for (f in frames) {
                    if (f == previous) continue
                    previous = f
                    add(f, if (speed) segmentVelocity(a, b, f, fps) else segmentValue(a, b, f))
                }
            }
        }
        // Chegada: o valor da marca seguinte (o salto do "manter"); na
        // velocidade, depois da última marca ela cai a zero na vertical.
        if (!speed && t1 <= to) add(t1, b.value)
        if (speed && i + 1 == keys.lastIndex && t1 < to) add(t1, 0f)
    }
    add(to, edge(to))
    return out
}

/** Faixa de valores do traço (para o "Ajustar"). */
internal fun graphRange(samples: List<GraphSample>): Pair<Float, Float>? {
    var low = Float.POSITIVE_INFINITY
    var high = Float.NEGATIVE_INFINITY
    for (s in samples) { if (s.value < low) low = s.value; if (s.value > high) high = s.value }
    return if (low <= high) low to high else null
}

/** Trilhas animadas (2+ marcas) de uma camada, uma lista por trilha, em ordem estável. */
internal fun animatedTracks(keys: List<KeyframeRow>): List<List<KeyframeRow>> =
    keys.groupBy { Triple(it.property, if (it.property < com.aurea.aurea.engine.TrackProperty.EFFECT_PARAM) 0 else it.effectIndex, it.paramIndex) }
        .toSortedMap(compareBy<Triple<Int, Int, Int>> { it.first }.thenBy { it.second }.thenBy { it.third })
        .values.map { track -> track.sortedBy { it.time } }
        .filter { it.size >= 2 }

/**
 * O que o gráfico mostra numa camada: a mesma trilha de [like] se ela anima
 * ali, senão uma do mesmo grupo (X→Y da posição), senão a primeira animada.
 * Vazia = a camada não tem animação para mostrar.
 */
internal fun graphTrackFor(keys: List<KeyframeRow>, like: KeyframeRow?): List<KeyframeRow> {
    val tracks = animatedTracks(keys)
    if (like != null) {
        tracks.firstOrNull { it[0].sameTrack(like) }?.let { return it }
        curveTrack(tracks.filter { it[0].sameGroup(like) }).takeIf { it.size >= 2 }?.let { return it }
    }
    return curveTrack(tracks)
}

/** As trilhas IRMÃS de [track] na camada (o grupo inteiro: X/Y/Z, componentes). */
internal fun graphGroup(keys: List<KeyframeRow>, track: List<KeyframeRow>): List<List<KeyframeRow>> {
    val head = track.firstOrNull() ?: return emptyList()
    val group = animatedTracks(keys).filter { it[0].sameGroup(head) }
    return if (group.any { it[0].sameTrack(head) }) group else listOf(track) + group
}

/** Curva só existe entre 2 marcas: com menos, o editor não abre (avisa). */
internal fun curveEditable(track: List<KeyframeRow>): Boolean = track.size >= 2

/**
 * "Aplicar a todos os keyframes desta propriedade": o início de CADA trecho
 * de cada trilha do grupo (X/Y/Z, componentes do mesmo parâmetro de efeito,
 * largura/altura da forma) — a última marca de cada trilha não abre trecho.
 * Vazio = a trilha escolhida tem menos de 2 marcas.
 */
internal fun propertySegmentStarts(keys: List<KeyframeRow>, track: List<KeyframeRow>): List<KeyframeRow> {
    if (!curveEditable(track)) return emptyList()
    return graphGroup(keys, track).flatMap { t -> t.sortedBy { it.time }.dropLast(1) }
        .distinctBy { listOf(it.property, it.effectIndex, it.paramIndex, it.time) }
}

/**
 * O trecho que o cabeçote pede: o índice da marca que abre o trecho sob
 * [localPlayhead] (a última marca abre o trecho que chega nela). −1 = nenhum.
 */
internal fun segmentIndexAt(track: List<KeyframeRow>, localPlayhead: Int): Int {
    if (track.size < 2) return -1
    return track.indexOfLast { it.time <= localPlayhead }.coerceIn(0, track.size - 2)
}

/** O trecho que o painel mostra para a marca escolhida em [time]. */
internal fun segmentIndexOf(track: List<KeyframeRow>, time: Int): Int {
    if (track.size < 2) return -1
    val i = track.indexOfFirst { it.time == time }.takeIf { it >= 0 } ?: track.indexOfLast { it.time <= time }.coerceAtLeast(0)
    return i.coerceAtMost(track.size - 2)
}

/** Diferença relativa pequena (testes e o traço). */
internal fun sameValue(a: Float, b: Float, tolerance: Float = 1e-4f): Boolean =
    abs(a - b) <= tolerance * max(1f, max(abs(a), abs(b)))
