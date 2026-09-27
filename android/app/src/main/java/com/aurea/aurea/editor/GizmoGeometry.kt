package com.aurea.aurea.editor

import kotlin.math.hypot

/** Screen-space handles only. Movement still uses the original projected axes. */
internal object GizmoGeometry {
    fun tips(raw: FloatArray, density: Float): FloatArray {
        if (raw.size != 8 || raw.any { !it.isFinite() } || density <= 0f) return FloatArray(0)
        val result = raw.copyOf()
        val extent = (1..3).maxOf { hypot(raw[it * 2] - raw[0], raw[it * 2 + 1] - raw[1]) }
        if (extent > 0.0001f) {
            val scale = 80f * density / extent
            for (i in 1..3) {
                result[i * 2] = raw[0] + (raw[i * 2] - raw[0]) * scale
                result[i * 2 + 1] = raw[1] + (raw[i * 2 + 1] - raw[1]) * scale
            }
        }
        if (hypot(result[6] - result[0], result[7] - result[1]) < 44f * density * 0.6f) {
            result[6] = result[0] + 44f * density * 0.7f
            result[7] = result[1] - 44f * density * 0.7f
        }
        return result
    }
}
