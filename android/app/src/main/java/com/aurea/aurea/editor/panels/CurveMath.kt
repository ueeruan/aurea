package com.aurea.aurea.editor.panels

import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

/** Same safeguarded inverse as the shared core: flat time tangents need
 * parameter convergence, not an absolute x-error early exit. */
internal fun cubicBezier(x1: Float, y1: Float, x2: Float, y2: Float, x: Float): Float {
    if (x <= 0f) return 0f
    if (x >= 1f) return 1f
    fun sample(t: Double, a: Float, b: Float): Double {
        val m = 1.0 - t
        return 3.0 * m * m * t * a + 3.0 * m * t * t * b + t * t * t
    }
    var low = 0.0
    var high = 1.0
    var t = x.toDouble()
    repeat(28) {
        val error = sample(t, x1, x2) - x
        if (error == 0.0) return sample(t, y1, y2).toFloat()
        if (error < 0.0) low = t else high = t
        val m = 1.0 - t
        val slope = 3.0 * m * m * x1 + 6.0 * m * t * (x2.toDouble() - x1) + 3.0 * t * t * (1.0 - x2)
        var next = (low + high) * 0.5
        if (abs(slope) > 1e-12) {
            val candidate = t - error / slope
            if (candidate > low && candidate < high) next = candidate
        }
        if (abs(next - t) < 1e-9) return sample(next, y1, y2).toFloat()
        t = next
    }
    return sample(t, y1, y2).toFloat()
}

/**
 * Onde as duas alças da bézier são DESENHADAS e tocadas (x1, y1, x2, y2 em px):
 * as posições reais, afastadas ao longo da separação quando ficam mais perto
 * que [minSeparation] — a alça de saída (da primeira marca) e a de chegada (da
 * segunda) nunca viram um borrão só. Coincidentes, elas se abrem na direção
 * primeira marca → segunda: cada uma para o lado da própria marca.
 */
internal fun separatedHandles(
    h1x: Float, h1y: Float, h2x: Float, h2y: Float,
    k0x: Float, k0y: Float, k1x: Float, k1y: Float,
    minSeparation: Float,
): FloatArray {
    var dx = h2x - h1x
    var dy = h2y - h1y
    var distance = kotlin.math.hypot(dx, dy)
    if (distance >= minSeparation) return floatArrayOf(h1x, h1y, h2x, h2y)
    var gap = distance
    if (distance < 0.5f) {
        dx = k1x - k0x; dy = k1y - k0y
        distance = kotlin.math.hypot(dx, dy)
        if (distance < 1e-3f) { dx = 1f; dy = 0f; distance = 1f }
        gap = 0f
    }
    val push = (minSeparation - gap) / 2f
    val ux = dx / distance
    val uy = dy / distance
    return floatArrayOf(h1x - ux * push, h1y - uy * push, h2x + ux * push, h2y + uy * push)
}

/** A alça sob o dedo: 0 (saída), 1 (chegada) ou −1; a mais perto vence, dentro de [radius]. */
internal fun nearestHandle(x: Float, y: Float, shown: FloatArray, radius: Float): Int {
    val d1 = (x - shown[0]) * (x - shown[0]) + (y - shown[1]) * (y - shown[1])
    val d2 = (x - shown[2]) * (x - shown[2]) + (y - shown[3]) * (y - shown[3])
    if (kotlin.math.min(d1, d2) > radius * radius) return -1
    return if (d1 <= d2) 0 else 1
}

// --- Editor da curva do trecho (como no app antigo) ---------------------------
// A alça vai para ONDE O DEDO ESTÁ (a mais perto do toque, em qualquer ponto do
// gráfico), encaixa em 0 e 1 perto das bordas, pode passar de 0..1 na vertical
// (−2..3, antecipação e overshoot) e a faixa vertical se ajusta à curva.

/** Faixa vertical das alças: além de 0..1 dá antecipação e overshoot. */
internal const val EASE_Y_MIN = -2f
internal const val EASE_Y_MAX = 3f

/** A alça que o toque pega: a mais perto das duas (0 saída, 1 chegada), sempre uma. */
internal fun grabHandle(x: Float, y: Float, shown: FloatArray): Int {
    val d1 = (x - shown[0]) * (x - shown[0]) + (y - shown[1]) * (y - shown[1])
    val d2 = (x - shown[2]) * (x - shown[2]) + (y - shown[3]) * (y - shown[3])
    return if (d1 <= d2) 0 else 1
}

/**
 * Faixa vertical do gráfico: a curva inteira (com a força) e as alças, sempre
 * contendo 0..1, com 8 % de folga em cima e embaixo. [samples] = a curva
 * amostrada pelo motor (o que se desenha); sem ela, 257 pontos da mesma conta
 * — 41 perdiam os picos do elástico de 8 oscilações e a curva saía do gráfico.
 */
internal fun easeRange(ease: Ease, samples: FloatArray? = null): Pair<Float, Float> {
    var lo = 0f
    var hi = 1f
    val n = samples?.size?.takeIf { it >= 2 } ?: CURVE_SAMPLES
    for (i in 0 until n) {
        val v = samples?.get(i) ?: ease.transform(i / (n - 1f))
        if (v.isFinite()) { lo = min(lo, v); hi = max(hi, v) }
    }
    if (ease.hasHandles) {
        val h = ease.handles()
        lo = min(lo, min(h[1], h[3])); hi = max(hi, max(h[1], h[3]))
    }
    val pad = (hi - lo) * 0.08f
    return (lo - pad) to (hi + pad)
}

/** Encosta em 0 ou 1 quando está a menos de [tolerance] (na mesma unidade). */
internal fun snapUnit(v: Float, tolerance: Float): Float = when {
    abs(v) < tolerance -> 0f
    abs(v - 1f) < tolerance -> 1f
    else -> v
}

/**
 * A posição da alça (x 0..1, y −2..3) para o dedo em ([px], [py]) px do gráfico
 * de [width]×[height] com margem lateral [inset], faixa vertical [lo]..[hi] e
 * encaixe de [snapPx] px nas linhas 0 e 1.
 */
internal fun handleAt(px: Float, py: Float, inset: Float, width: Float, height: Float, lo: Float, hi: Float, snapPx: Float): FloatArray {
    val w = max(1f, width - 2 * inset)
    val h = max(1f, height)
    val span = if (hi - lo > 1e-3f) hi - lo else 1e-3f
    val x = snapUnit(((px - inset) / w).coerceIn(0f, 1f), snapPx / w)
    val y = snapUnit((lo + (h - py) / h * span).coerceIn(EASE_Y_MIN, EASE_Y_MAX), snapPx / h * span)
    return floatArrayOf(x, y)
}

/** O texto do trecho: `cubic-bezier(x1, y1, x2, y2)` e a força (×2, ×3) quando há. */
internal fun cubicBezierLabel(e: Ease): String {
    val h = e.handles()
    fun f(v: Float) = String.format(java.util.Locale.ROOT, "%.2f", v)
    val label = "cubic-bezier(${f(h[0])}, ${f(h[1])}, ${f(h[2])}, ${f(h[3])})"
    return if (e.isBezier && e.power > 1) "$label ×${e.power}" else label
}

/** Próxima força ao tocar no texto (×1 → ×2 → ×3 → ×1); vira bézier com as alças que se vê. */
internal fun nextPower(e: Ease): Ease {
    val h = e.handles()
    val next = if (e.isBezier) e.power % 3 + 1 else 2
    return Ease(Interp.BEZIER, h[0], h[1], h[2], h[3], next)
}

/** Pontos da curva do trecho no gráfico (t = i/256): o motor amostra este tanto. */
internal const val CURVE_SAMPLES = 257

/**
 * O valor da curva amostrada [samples] (t = i/(n−1)) em [t]: a reta entre os
 * dois pontos vizinhos — o ponto do cabeçote fica EM CIMA do traço desenhado.
 */
internal fun easeSampleAt(samples: FloatArray, t: Float): Float {
    if (samples.isEmpty()) return t
    if (samples.size == 1 || t <= 0f) return samples[0]
    if (t >= 1f) return samples.last()
    val x = t * (samples.size - 1)
    val i = x.toInt().coerceIn(0, samples.size - 2)
    val f = x - i
    return samples[i] + (samples[i + 1] - samples[i]) * f
}

// --- Tipos rápidos do editor de curva ----------------------------------------
// Os botões ao lado do gráfico: Linear, Suave, Passar do ponto (Overshoot),
// Elástico e Quique. Os três últimos têm parâmetros (sliders sob o gráfico).

internal enum class CurveQuickType { Linear, Ease, Overshoot, Elastic, Bounce }

/** O tipo rápido em que a curva do trecho se encaixa (nulo = Manter/Degraus). */
internal fun quickTypeOf(e: Ease): CurveQuickType? = when (e.interp) {
    Interp.LINEAR -> CurveQuickType.Linear
    Interp.BEZIER, Interp.CUSTOM, Interp.EASE_IN, Interp.EASE_OUT, Interp.EASE_IN_OUT -> CurveQuickType.Ease
    Interp.OVERSHOOT -> CurveQuickType.Overshoot
    Interp.ELASTIC -> CurveQuickType.Elastic
    Interp.BOUNCE -> CurveQuickType.Bounce
    else -> null
}

/** Quique padrão (3 saltos, força 0,5) com os parâmetros gravados. */
internal val DefaultBounce = Ease(Interp.BOUNCE, .375f, .5f, 1f, EASE_PARAM_MARKER)

/**
 * O easing que o botão [type] aplica sobre a curva [current]: o padrão do tipo;
 * tocar no tipo que já está escolhido mantém os parâmetros (nada muda).
 */
internal fun quickTypeEase(type: CurveQuickType, current: Ease): Ease {
    val same = quickTypeOf(current) == type
    return when (type) {
        CurveQuickType.Linear -> Ease(Interp.LINEAR, 0f, 0f, 1f, 1f)
        CurveQuickType.Ease -> if (same) current else Ease(Interp.BEZIER, .42f, 0f, .58f, 1f)
        CurveQuickType.Overshoot -> if (same) current.overshoot() else Ease(Interp.OVERSHOOT, 0f, 0f, 1f, 1f).overshoot()
        CurveQuickType.Elastic -> if (same) current.elastic() else Ease(Interp.ELASTIC, 0f, 0f, 1f, 1f).elastic()
        CurveQuickType.Bounce -> if (same) current else DefaultBounce
    }
}
