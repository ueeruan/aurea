package com.aurea.aurea.state

import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min

/** Mirrors export_frame_size_v2: picture dimensions are independent of codec storage alignment. */
object VideoExportRules {
    const val LONG_SIDE_ALIGN = 16
    const val SHORT_SIDE_ALIGN = 2

    internal fun alignNearest(v: Double, a: Int): Int {
        if (!(v > 0.0) || v.isInfinite()) return a
        return max(a, floor(v / a + 0.5).toInt() * a)
    }

    /** (largura, altura) do vídeo para o lado menor [shortSide] (0 = o da composição). */
    fun frameSize(compW: Int, compH: Int, shortSide: Int): Pair<Int, Int> {
        if (compW <= 0 || compH <= 0) return 0 to 0
        val compShort = min(compW, compH)
        val side = if (shortSide > 0) shortSide else compShort
        val k = side.toDouble() / compShort
        val w = compW * k
        val h = compH * k
        if ((shortSide <= 0 || shortSide == compShort) && (compW % 2 != 0 || compH % 2 != 0)) return 0 to 0
        if (side % 2 != 0) return 0 to 0
        return alignNearest(w, SHORT_SIDE_ALIGN) to alignNearest(h, SHORT_SIDE_ALIGN)
    }
}
