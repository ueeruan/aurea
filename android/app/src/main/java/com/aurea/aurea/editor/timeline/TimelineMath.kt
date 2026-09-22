package com.aurea.aurea.editor.timeline

import java.util.Locale
import kotlin.math.abs
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.ln
import kotlin.math.max
import kotlin.math.pow
import kotlin.math.roundToInt

// =============================================================================
//  Lógica pura da timeline (sem Android, sem Compose): testada na JVM.
//  Tempo em FRAMES da composição (a unidade do motor); vista em Double para o
//  arrasto e a pinça não andarem de quadro em quadro.
// =============================================================================

/**
 * Tempo ↔ px com o cabeçote FIXO no centro da timeline inteira (A e B): quem
 * anda é o conteúdo. `view` é o frame sob o cabeçote.
 */
internal object TimeAxis {
    fun safeFps(fps: Float): Float = if (fps > 0f) fps else 30f

    /** px por frame para um zoom em dp/s. */
    fun pxPerFrame(pps: Float, density: Float, fps: Float): Float = pps * density / safeFps(fps)

    fun xOf(frame: Double, view: Double, pxPerFrame: Float, centerX: Float): Float =
        (centerX + (frame - view) * pxPerFrame).toFloat()

    fun frameAt(x: Float, view: Double, pxPerFrame: Float, centerX: Float): Double =
        view + (x - centerX) / pxPerFrame

    /** A vista não sai da composição: o motor não tem playhead antes de 0 nem depois do fim. */
    fun clampView(view: Double, durationFrames: Int): Double =
        view.coerceIn(0.0, max(0, durationFrames - 1).toDouble())
}

/** Zoom em dp por segundo. */
internal object Zoom {
    const val MIN_PPS = 2f
    const val MAX_PPS = 800f
    /** A.01: 80 dp/s (10 riscos por segundo a 8 dp). */
    const val DEFAULT_PPS = 80f
    /** A.01 enquadra projetos longos (≥ 20 s) na largura ao abrir. */
    const val AUTO_FIT_MIN_SECONDS = 20f

    fun clamp(pps: Float): Float = pps.coerceIn(MIN_PPS, MAX_PPS)

    /** `clamp((largura − 32) / segundos, 4, 80)` da A.01. */
    fun autoFit(availableDp: Float, seconds: Float): Float =
        if (seconds <= 0f) DEFAULT_PPS else (availableDp / seconds).coerceIn(4f, DEFAULT_PPS)

    /** Pinça: a vista que mantém `focusFrame` sob o ponto focal `focusX`. */
    fun anchoredView(focusFrame: Double, focusX: Float, centerX: Float, pxPerFrame: Float): Double =
        focusFrame - (focusX - centerX) / pxPerFrame
}

/**
 * Passos da régua. No zoom da A.01 (80 dp/s) dá exatamente o print: risco
 * forte por segundo e 10 finos. Fora dele os passos se adaptam para os riscos
 * nunca virarem borrão; só quando o forte vale mais de 1 s aparece rótulo
 * (sem ele não dá para contar tempo na régua).
 */
internal class RulerSteps(
    val majorSeconds: Double,
    /** Subdivisões entre dois fortes (0 = sem riscos finos). Ignorado se [frameMinors]. */
    val subdivisions: Int,
    /** Riscos finos em cada quadro (zoom muito aberto). */
    val frameMinors: Boolean,
    val labels: Boolean,
) {
    companion object {
        private val MAJORS = doubleArrayOf(1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 1800.0, 3600.0)
        private val SUBS = intArrayOf(10, 4, 5, 10, 3, 6, 6, 4, 5, 10, 6, 6)
        private val ONE_SECOND_SUBS = intArrayOf(10, 5, 2)
        const val MIN_MAJOR_DP = 40f
        const val MIN_MINOR_DP = 4f
        const val MIN_FRAME_DP = 12f

        fun of(pps: Float, fps: Float): RulerSteps {
            var i = MAJORS.indexOfFirst { it * pps >= MIN_MAJOR_DP }
            if (i < 0) i = MAJORS.lastIndex
            val major = MAJORS[i]
            if (i == 0) {
                if (pps / TimeAxis.safeFps(fps) >= MIN_FRAME_DP) return RulerSteps(major, 0, true, false)
                for (n in ONE_SECOND_SUBS) if (pps / n >= MIN_MINOR_DP) return RulerSteps(major, n, false, false)
                return RulerSteps(major, 0, false, false)
            }
            val n = SUBS[i]
            return RulerSteps(major, if (major * pps / n >= MIN_MINOR_DP) n else 0, false, true)
        }
    }
}

/** Relógio `MM:SS:FF` da A.01; a partir de 1 h ganha a hora (`H:MM:SS:FF`, a A.01 virava "75:12:03"). */
internal object Timecode {
    /** `out` = [horas, minutos, segundos, quadros]. Sem alocação (o relógio repinta a 60 Hz). */
    fun split(frame: Int, fps: Float, out: IntArray) {
        val f = TimeAxis.safeFps(fps).toDouble()
        val fr = max(0, frame)
        val totalSeconds = floor(fr / f + 1e-9).toLong()
        // Quadro dentro do segundo: conta a partir do 1º quadro que cai NAQUELE segundo
        // (com 29,97 fps o segundo 1 começa no quadro 30, não em 29,97).
        val firstOfSecond = ceil(totalSeconds * f - 1e-6).toLong()
        out[0] = (totalSeconds / 3600).toInt()
        out[1] = ((totalSeconds / 60) % 60).toInt()
        out[2] = (totalSeconds % 60).toInt()
        out[3] = max(0L, fr - firstOfSecond).toInt()
    }

    fun format(frame: Int, fps: Float): String {
        val p = IntArray(4)
        split(frame, fps, p)
        val base = String.format(Locale.ROOT, "%02d:%02d:%02d", p[1], p[2], p[3])
        return if (p[0] > 0) "${p[0]}:$base" else base
    }

    /** Rótulo da régua: `m:ss` (ou `h:mm:ss`). */
    fun rulerLabel(seconds: Int): String {
        val h = seconds / 3600
        val m = (seconds / 60) % 60
        val s = seconds % 60
        return if (h > 0) String.format(Locale.ROOT, "%d:%02d:%02d", h, m, s)
        else String.format(Locale.ROOT, "%d:%02d", m, s)
    }
}

/**
 * Ímã. Os alvos são reunidos UMA vez no começo do gesto (ordenados, busca
 * binária); o cabeçote disputa junto a cada passo porque a vista pode andar
 * (auto-rolagem).
 */
internal object Snap {
    const val NONE = Int.MIN_VALUE

    /** Alvo mais perto de `value` a no máximo `tol` frames (`extra` = cabeçote; NONE = sem). */
    fun nearest(targets: IntArray, value: Double, extra: Int, tol: Double): Int {
        var best = NONE
        var bestD = Double.MAX_VALUE
        if (extra != NONE) {
            val d = abs(extra - value)
            if (d <= tol) {
                bestD = d
                best = extra
            }
        }
        if (targets.isNotEmpty()) {
            var i = java.util.Arrays.binarySearch(targets, floor(value).toInt())
            if (i < 0) i = -i - 1
            for (k in i - 1..i + 1) {
                if (k !in targets.indices) continue
                val d = abs(targets[k] - value)
                if (d <= tol && d < bestD) {
                    bestD = d
                    best = targets[k]
                }
            }
        }
        return best
    }

    /**
     * Encaixa um intervalo pelo INÍCIO ou pelo FIM, o que estiver mais perto.
     * Devolve o novo início em `out[0]` e a guia (frame encaixado ou NONE) em `out[1]`.
     */
    fun span(targets: IntArray, start: Int, length: Int, extra: Int, tol: Double, out: IntArray) {
        val a = nearest(targets, start.toDouble(), extra, tol)
        val b = nearest(targets, (start + length).toDouble(), extra, tol)
        val da = if (a == NONE) Double.MAX_VALUE else abs(a - start).toDouble()
        val db = if (b == NONE) Double.MAX_VALUE else abs(b - (start + length)).toDouble()
        when {
            a != NONE && da <= db -> { out[0] = a; out[1] = a }
            b != NONE -> { out[0] = b - length; out[1] = b }
            else -> { out[0] = start; out[1] = NONE }
        }
    }

    /** Ordena e tira repetidos. */
    fun sortedDistinct(values: IntArray, count: Int): IntArray {
        if (count == 0) return IntArray(0)
        val a = values.copyOf(count)
        a.sort()
        var n = 1
        for (i in 1 until a.size) if (a[i] != a[n - 1]) a[n++] = a[i]
        return a.copyOf(n)
    }
}

/**
 * Auto-rolagem na borda durante arrastos: só rola para o lado a que o dedo
 * FOI desde o começo do gesto (pegar um clipe já perto da borda não sai rolando).
 */
internal object AutoScroll {
    fun direction(pos: Float, from: Float, low: Float, high: Float, intent: Float): Int = when {
        pos < low && pos < from - intent -> -1
        pos > high && pos > from + intent -> 1
        else -> 0
    }
}

/** Instantes de keyframe de uma camada, em frames da timeline. */
internal object Keyframes {
    /** Tempo local → frame da timeline (`t + start − offset`). */
    fun toTimeline(local: Int, start: Int, offset: Int) = local + start - offset
    fun toLocal(timeline: Int, start: Int, offset: Int) = timeline - start + offset

    /**
     * Limites de arrasto do instante `index` (inclusive): nunca encosta no
     * vizinho (senão duas marcas da mesma trilha se fundem) e não sai da camada
     * — a não ser que já estivesse fora (não pula para dentro sozinho).
     */
    fun dragLimits(instants: IntArray, index: Int, start: Int, end: Int, out: IntArray) {
        val t = instants[index]
        var lo = minOf(start, t)
        var hi = maxOf(end, t)
        if (index > 0) lo = max(lo, instants[index - 1] + 1)
        if (index < instants.lastIndex) hi = minOf(hi, instants[index + 1] - 1)
        out[0] = lo
        out[1] = max(lo, hi)
    }

    /** Índice do instante mais perto de `frame` (a lista está ordenada); −1 se vazia. */
    fun nearestIndex(instants: IntArray, frame: Double): Int {
        if (instants.isEmpty()) return -1
        var i = java.util.Arrays.binarySearch(instants, floor(frame).toInt())
        if (i < 0) i = -i - 1
        var best = -1
        var bestD = Double.MAX_VALUE
        for (k in i - 1..i + 1) {
            if (k !in instants.indices) continue
            val d = abs(instants[k] - frame)
            if (d < bestD) {
                bestD = d
                best = k
            }
        }
        return best
    }

    /** Primeiro índice com instante ≥ `frame`. */
    fun firstAtOrAfter(instants: IntArray, frame: Double): Int {
        val key = ceil(frame).toInt()
        var i = java.util.Arrays.binarySearch(instants, key)
        if (i < 0) i = -i - 1
        return i
    }
}

/**
 * Miniaturas: o motor as guarda em baldes de 250 ms do tempo da MÍDIA. A
 * timeline pede um frame por balde (o mesmo sempre), então mexer o clipe não
 * troca a chave e a tira não pisca.
 */
internal object Thumbs {
    const val BUCKET_SECONDS = 0.25

    fun bucketOf(localFrame: Double, fps: Float): Int =
        floor(max(0.0, localFrame) / TimeAxis.safeFps(fps) / BUCKET_SECONDS + 1e-9).toInt()

    /** Frame local que o motor põe no balde `bucket` (o 1º quadro dele). */
    fun requestLocalFrame(bucket: Int, fps: Float): Int =
        ceil(bucket * BUCKET_SECONDS * TimeAxis.safeFps(fps) - 1e-6).toInt()
}

/**
 * Waveform em grade FIXA do tempo (fase 8D). O balde `k` cobre os frames
 * `[k·fpb, (k+1)·fpb)` — não depende de onde está a vista, então rolar ou
 * tocar não faz a forma "tremer" (os baldes andavam com a vista) e a janela
 * pedida ao motor serve por várias telas. O tamanho do balde anda em degraus
 * de √2: a pinça só pede de novo quando passa de um degrau.
 */
internal object WaveGrid {
    fun framesPerBucket(targetPx: Float, pxPerFrame: Float): Double {
        if (!(pxPerFrame > 0f) || !(targetPx > 0f)) return 1.0
        val exact = targetPx.toDouble() / pxPerFrame
        val step = kotlin.math.round(kotlin.math.log2(exact) * 2.0) / 2.0
        return 2.0.pow(step)
    }

    fun bucketAt(frame: Double, fpb: Double): Long = floor(frame / fpb).toLong()

    /**
     * Janela a pedir para ver `[first, last]`: a vista com uma tela de folga
     * de cada lado, no máximo `max` baldes. `out` = [início, fim exclusivo].
     */
    fun window(first: Long, last: Long, max: Int, out: LongArray) {
        val visible = last - first + 1
        val pad = ((max - visible) / 2).coerceIn(0L, visible)
        out[0] = first - pad
        out[1] = minOf(last + 1 + pad, out[0] + max)
    }
}

/** Reordenar: linha de destino sob o dedo (0 = topo = camada da frente). */
internal object Reorder {
    fun targetIndex(y: Float, rowsTop: Float, scroll: Float, rowHeight: Float, count: Int): Int {
        if (count <= 0) return -1
        return floor((y - rowsTop + scroll) / rowHeight).toInt().coerceIn(0, count - 1)
    }

    /** y do traço de destino: acima do destino quando sobe, abaixo quando desce; NaN se não muda. */
    fun dropLineY(source: Int, target: Int, rowsTop: Float, scroll: Float, rowHeight: Float): Float = when {
        target < 0 || target == source -> Float.NaN
        target < source -> rowsTop + target * rowHeight - scroll
        else -> rowsTop + (target + 1) * rowHeight - scroll
    }
}

/** Arredonda ao quadro mais perto (a grade do motor). */
internal fun Double.toFrame(): Int = roundToInt()
