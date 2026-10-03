package com.aurea.aurea.state

import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min

/**
 * O tamanho do vídeo exportado — ESPELHO de `export_frame_size`
 * (engine/include/aurea/export/ExportRules.hpp), a regra que o motor usa no
 * export. A tela mostra exatamente o que sai.
 *
 * Lado maior em múltiplo de 16, menor par (como antes); quadrado fica
 * quadrado. "480p" de 16:9 é 848×480 (não 854×480: o encoder MediaTek do
 * Vivo Y30 recusava a largura sem alinhamento — "buffer do encoder menor que
 * o quadro").
 */
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
        return when {
            compW == compH -> alignNearest(w, SHORT_SIDE_ALIGN).let { it to it }
            compW > compH -> alignNearest(w, LONG_SIDE_ALIGN) to alignNearest(h, SHORT_SIDE_ALIGN)
            else -> alignNearest(w, SHORT_SIDE_ALIGN) to alignNearest(h, LONG_SIDE_ALIGN)
        }
    }
}
