package com.aurea.aurea.editor.timeline

import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

/** O que o dedo pegou. */
internal enum class HitKind { NONE, RULER, HEADER_EYE, HEADER, KEYFRAME, TRIM_START, TRIM_END, ARROW_PREV, ARROW_NEXT, BODY }

/**
 * Hit-test de UMA linha, com a mesma geometria do pintor. Prioridade (spec
 * 03 §5.1): pílula > losango > alça de trim > setas ‹ › > corpo > vazio.
 *
 * Pura: recebe tudo em px (coordenadas da timeline; `y` relativo ao topo da
 * linha) e devolve o tipo; o índice do losango vai em `out[0]` (−1 se nenhum).
 */
internal object RowHit {

    /** Onde o conteúdo da barra começa: gruda depois da coluna das pílulas quando a ponta está escondida. */
    fun contentLeft(m: TimelineMetrics, x0: Float, x1: Float): Float =
        max(x0, m.headerColumn) + if (x1 - x0 < m.narrowBar) m.padLNarrow else m.padL

    /** Onde o conteúdo termina: a seta › fica à vista mesmo com a barra passando da borda (print t2). */
    fun contentRight(m: TimelineMetrics, x0: Float, x1: Float, width: Float): Float =
        min(x1, width) - if (x1 - x0 < m.narrowBar) m.padRNarrow else m.padR

    /** "O que não se vê não se apara": ponta embaixo da coluna das pílulas não tem alça. */
    fun startHandleVisible(m: TimelineMetrics, x0: Float): Boolean = x0 >= m.headerColumn

    fun endHandleVisible(x1: Float, width: Float): Boolean = x1 <= width

    fun hit(
        m: TimelineMetrics,
        x: Float,
        y: Float,
        width: Float,
        x0: Float,
        x1: Float,
        handles: Boolean,
        compact: Boolean,
        keysEnabled: Boolean,
        instants: IntArray,
        view: Double,
        pxPerFrame: Float,
        centerX: Float,
        out: IntArray,
    ): HitKind {
        out[0] = -1
        if (y < 0f || y >= m.row) return HitKind.NONE
        if (x < m.headerColumn) return if (x < m.eyeHitRight) HitKind.HEADER_EYE else HitKind.HEADER

        // Losango mais perto do dedo (faixa de baixo da barra e o respiro abaixo dela).
        var key = -1
        var keyX = 0f
        if (keysEnabled && y >= m.keyTouchTop && instants.isNotEmpty()) {
            val i = Keyframes.nearestIndex(instants, TimeAxis.frameAt(x, view, pxPerFrame, centerX))
            val kx = TimeAxis.xOf(instants[i].toDouble(), view, pxPerFrame, centerX)
            if (abs(kx - x) <= m.keyTouchHalf) {
                key = i
                keyX = kx
            }
        }

        // Zonas das alças: o desenho (dentro das pontas) + folga para fora; num clipe
        // curto as duas param no meio para não se sobreporem.
        val overBar = y < m.bodyHitBottom
        val mid = (x0 + x1) / 2f
        val inStart = handles && overBar && startHandleVisible(m, x0) &&
            x >= x0 - m.trimInsetStart - m.trimTouchOut &&
            x < min(x0 - m.trimInsetStart + m.trimWidth, mid)
        val inEnd = handles && overBar && endHandleVisible(x1, width) &&
            x > max(x1 - m.trimInsetEnd, mid) &&
            x <= x1 - m.trimInsetEnd + m.trimWidth + m.trimTouchOut

        if (key >= 0) {
            // Bug 10.2 da spec: o losango em 0 (o caso mais comum) roubava a alça de
            // início. Dentro da zona da alça, o losango só vence se o dedo está NO
            // desenho dele e dentro da barra; o resto da zona é da alça.
            val onGlyph = abs(keyX - x) <= m.keyGlyphHalf && x >= x0 && x <= x1
            if ((!inStart && !inEnd) || onGlyph) {
                out[0] = key
                return HitKind.KEYFRAME
            }
        }
        if (inStart) return HitKind.TRIM_START
        if (inEnd) return HitKind.TRIM_END
        if (overBar && x >= x0 && x <= x1) {
            if (compact) {
                val cl = contentLeft(m, x0, x1)
                val cr = contentRight(m, x0, x1, width)
                if (x >= cl - m.arrowTouchPad && x <= cl + m.arrowSlot + m.arrowTouchPad) return HitKind.ARROW_PREV
                if (x >= cr - m.arrowSlot - m.arrowTouchPad && x <= cr + m.arrowTouchPad) return HitKind.ARROW_NEXT
            }
            return HitKind.BODY
        }
        return HitKind.NONE
    }
}
