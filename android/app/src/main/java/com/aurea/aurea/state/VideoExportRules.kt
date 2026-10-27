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

    /**
     * (largura, altura) do vídeo para o lado menor [shortSide] (0 = o da composição).
     * Espelha export_attempt_frame_size do degrau 0 (ExportAttempts.hpp, 0.3.6): o
     * nativo par; composição com lado ímpar sai alinhada ([alignedFrameSize]) em vez
     * de recusar com "erro de codec".
     */
    fun frameSize(compW: Int, compH: Int, shortSide: Int): Pair<Int, Int> {
        if (compW <= 0 || compH <= 0) return 0 to 0
        val compShort = min(compW, compH)
        val side = if (shortSide > 0) shortSide else compShort
        val k = side.toDouble() / compShort
        val w = compW * k
        val h = compH * k
        val odd = ((shortSide <= 0 || shortSide == compShort) && (compW % 2 != 0 || compH % 2 != 0)) || side % 2 != 0
        if (odd) return alignedFrameSize(compW, compH, shortSide)
        return alignNearest(w, SHORT_SIDE_ALIGN) to alignNearest(h, SHORT_SIDE_ALIGN)
    }

    /** export_frame_size: lado maior múltiplo de 16, menor par (degraus de compatibilidade). */
    fun alignedFrameSize(compW: Int, compH: Int, shortSide: Int): Pair<Int, Int> {
        if (compW <= 0 || compH <= 0) return 0 to 0
        val compShort = min(compW, compH)
        val k = (if (shortSide > 0) shortSide else compShort).toDouble() / compShort
        val w = compW * k
        val h = compH * k
        return when {
            compW == compH -> alignNearest(w, SHORT_SIDE_ALIGN).let { it to it }
            compW > compH -> alignNearest(w, LONG_SIDE_ALIGN) to alignNearest(h, SHORT_SIDE_ALIGN)
            else -> alignNearest(w, SHORT_SIDE_ALIGN) to alignNearest(h, LONG_SIDE_ALIGN)
        }
    }
}
