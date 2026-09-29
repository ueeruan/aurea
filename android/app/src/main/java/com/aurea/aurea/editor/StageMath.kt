package com.aurea.aurea.editor

import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.round

/**
 * Contas puras dos gestos do palco (sem Compose, sem motor): mover várias
 * camadas juntas e os encaixes da pinça. Espelhadas em Swift no fim de
 * PreviewMetalView.swift (`StageMath`).
 */
internal object StageMath {
    /** Giro: encaixa a cada 45°, entra a menos de 4° e só solta além de 6°. */
    const val ROT_STEP = 45f
    const val ROT_ENTER = 4f
    const val ROT_EXIT = 6f

    /** Escala: encaixa em 100% a menos de 3%, solta além de 4,5%. */
    const val SCALE_ENTER = 0.03f
    const val SCALE_EXIT = 0.045f

    /**
     * Caixa que abraça todos os cantos (x, y, x, y… em px da composição):
     * [minX, minY, maxX, maxY], ou null quando não há ponto válido.
     */
    fun unionBox(cornerSets: List<FloatArray>): FloatArray? {
        var x0 = Float.POSITIVE_INFINITY
        var y0 = Float.POSITIVE_INFINITY
        var x1 = Float.NEGATIVE_INFINITY
        var y1 = Float.NEGATIVE_INFINITY
        for (c in cornerSets) {
            var i = 0
            while (i + 1 < c.size) {
                val x = c[i]
                val y = c[i + 1]
                if (x.isFinite() && y.isFinite()) {
                    x0 = min(x0, x); y0 = min(y0, y)
                    x1 = max(x1, x); y1 = max(y1, y)
                }
                i += 2
            }
        }
        return if (x0 <= x1 && y0 <= y1) floatArrayOf(x0, y0, x1, y1) else null
    }

    /**
     * Anda ([dx], [dy]) px da COMPOSIÇÃO uma camada cuja posição local é
     * ([localX], [localY]) no espaço do pai ([affine] = pai → composição,
     * a b c d tx ty). Devolve em [out] a nova posição local. Pai degenerado
     * (sem inversa): a composição vale como local, como `compToParent`.
     */
    fun moveInParent(affine: FloatArray, localX: Float, localY: Float, dx: Float, dy: Float, out: FloatArray) {
        val a = if (affine.size >= 6) affine else IDENTITY
        val wx = a[0] * localX + a[2] * localY + a[4] + dx
        val wy = a[1] * localX + a[3] * localY + a[5] + dy
        val det = a[0] * a[3] - a[2] * a[1]
        if (abs(det) < 1e-9f) { out[0] = wx; out[1] = wy; return }
        val rx = wx - a[4]
        val ry = wy - a[5]
        out[0] = (a[3] * rx - a[2] * ry) / det
        out[1] = (-a[1] * rx + a[0] * ry) / det
    }

    /**
     * Quem anda de verdade num mover em grupo: tira quem tem um ancestral
     * (em qualquer nível) também no conjunto — o pai já leva o filho, mover
     * os dois dobraria o deslocamento. [parentOf] devolve 0 sem pai. A ordem
     * de [ids] é mantida.
     */
    fun moveRoots(ids: Collection<Long>, parentOf: (Long) -> Long): List<Long> {
        val set = ids.toHashSet()
        val out = ArrayList<Long>(ids.size)
        for (id in ids) {
            var p = parentOf(id)
            var depth = 0
            var covered = false
            while (p != 0L && p != id && depth < 64) {
                if (p in set) { covered = true; break }
                p = parentOf(p)
                depth++
            }
            if (!covered) out.add(id)
        }
        return out
    }

    /**
     * Encaixe em múltiplos de [step] com histerese: preso em [current] (não
     * NaN) enquanto [value] estiver a até [exit] dele; senão prende no
     * múltiplo mais próximo se estiver a menos de [enter]. NaN = solto.
     */
    fun snapStep(value: Float, step: Float, current: Float, enter: Float, exit: Float): Float {
        if (!value.isFinite() || step <= 0f) return Float.NaN
        if (!current.isNaN() && abs(value - current) <= exit) return current
        val k = round(value / step) * step
        return if (abs(value - k) < enter) k else Float.NaN
    }

    /** O mesmo encaixe para um alvo único (ex.: escala 100% = 1). */
    fun snapTarget(value: Float, target: Float, current: Float, enter: Float, exit: Float): Float {
        if (!value.isFinite()) return Float.NaN
        if (!current.isNaN() && abs(value - current) <= exit) return current
        return if (abs(value - target) < enter) target else Float.NaN
    }

    /** Entrou num encaixe (ou trocou de alvo): hora do toque háptico. */
    fun snapEntered(previous: Float, next: Float): Boolean = !next.isNaN() && next != previous

    private val IDENTITY = floatArrayOf(1f, 0f, 0f, 1f, 0f, 0f)
}
