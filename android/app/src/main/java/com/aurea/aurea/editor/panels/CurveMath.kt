package com.aurea.aurea.editor.panels

import kotlin.math.abs

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
