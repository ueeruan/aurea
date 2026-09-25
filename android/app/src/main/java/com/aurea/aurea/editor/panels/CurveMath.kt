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
