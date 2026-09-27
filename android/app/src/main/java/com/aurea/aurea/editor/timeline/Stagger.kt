package com.aurea.aurea.editor.timeline

import kotlin.math.abs

/**
 * "Escalonar" (cascata de N quadros): a conta da UI antes do motor. O motor
 * (`Engine::stagger_layers`) move na ordem que recebe e deixa a primeira no
 * lugar; aqui a ordem vem da timeline e o sinal do passo escolhe o sentido.
 */
object Stagger {
    const val MIN = -120
    const val MAX = 120
    const val DEFAULT = 3

    /** Próximo valor do contador: anda `delta`, nunca para no 0 (−1 → 1) e fica em [MIN, MAX]. */
    fun step(current: Int, delta: Int): Int {
        var next = (current + delta).coerceIn(MIN, MAX)
        if (next == 0) next = if (delta > 0) 1 else -1
        return next
    }

    /**
     * Ordem e passo para o motor. `topToBottom` = camadas na ordem em que a
     * timeline mostra. Passo positivo: a de cima fica e as de baixo atrasam;
     * negativo: a de baixo fica e as de cima atrasam. Null = nada a fazer.
     */
    fun plan(topToBottom: List<Long>, step: Int): Pair<LongArray, Int>? {
        val ids = topToBottom.distinct()
        if (ids.size < 2 || step == 0) return null
        val ordered = if (step > 0) ids else ids.asReversed()
        return ordered.toLongArray() to abs(step)
    }
}

/** Quem mostra (e deixa tocar) os losangos de keyframe na timeline. */
object KeyframeVisibility {
    /**
     * Trilha aberta (propriedade) sempre mostra; a linha da camada mostra se
     * "todas as camadas" está ligado ou se ela está entre as escolhidas.
     */
    fun visible(showAll: Boolean, isPropertyLane: Boolean, selected: Boolean): Boolean =
        showAll || isPropertyLane || selected
}
