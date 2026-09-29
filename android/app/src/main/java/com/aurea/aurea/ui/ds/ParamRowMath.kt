package com.aurea.aurea.ui.ds

import kotlin.math.abs

/**
 * A CONTA da linha de parâmetro do redesenho 2026-09-29 (Efeitos.dc.html), sem
 * Compose — testada na JVM e espelhada em Swift (`FxParamRowMath`).
 *
 * - Rótulo: 13 sp até 8 letras; mais longo, 11 sp (numa linha até 12, depois duas).
 * - Régua: o valor ACUMULA desde o início do gesto (`início + andado × porDp`),
 *   preso na faixa estendida até o valor de partida ([dragBounds]).
 * - Controle fino: dois dedos = 1/10; arrasto lento = 1/4, subindo até 1 com a
 *   velocidade (o dedo rápido atravessa a faixa, o lento acerta o número).
 * - Riscos: mais claros no centro, apagando para as bordas.
 */
object ParamRowMath {
    /** Fonte do rótulo cheio / encolhido (sp). */
    const val LABEL_FULL_SP = 13f
    const val LABEL_SMALL_SP = 11f

    /** Até quantas letras o rótulo cabe em 13 sp na caixa de 70 dp. */
    const val LABEL_FULL_MAX_CHARS = 8

    /** Até quantas letras o rótulo encolhido cabe numa linha só. */
    const val LABEL_ONE_LINE_MAX_CHARS = 12

    /** Ganho com dois dedos na régua. */
    const val TWO_FINGER_GAIN = 0.1f

    /** Ganho do arrasto lento (abaixo de [SLOW_SPEED]). */
    const val SLOW_GAIN = 0.25f

    /** Velocidades (dp/ms) em que o ganho começa a subir e chega a 1. */
    const val SLOW_SPEED = 0.08f
    const val FAST_SPEED = 0.35f

    /** Peso da amostra nova na média da velocidade. */
    const val SPEED_SMOOTHING = 0.35f

    fun labelFontSp(label: String): Float =
        if (label.trim().length <= LABEL_FULL_MAX_CHARS) LABEL_FULL_SP else LABEL_SMALL_SP

    fun labelMaxLines(label: String): Int =
        if (label.trim().length <= LABEL_ONE_LINE_MAX_CHARS) 1 else 2

    /** Ganho do arrasto: dois dedos, lento (fino) ou rápido (1:1). */
    fun scrubGain(pointers: Int, speedDpPerMs: Float): Float {
        if (pointers >= 2) return TWO_FINGER_GAIN
        val s = abs(speedDpPerMs)
        if (!s.isFinite() || s >= FAST_SPEED) return 1f
        if (s <= SLOW_SPEED) return SLOW_GAIN
        val t = (s - SLOW_SPEED) / (FAST_SPEED - SLOW_SPEED)
        return SLOW_GAIN + (1f - SLOW_GAIN) * t
    }

    /** Velocidade suavizada (dp/ms) com a amostra `dx` em `dt`; `dt` inválido mantém a anterior. */
    fun smoothSpeed(previous: Float, dxDp: Float, dtMs: Float): Float {
        if (!(dtMs > 0f) || !dxDp.isFinite()) return previous
        val sample = abs(dxDp) / dtMs
        return previous + (sample - previous) * SPEED_SMOOTHING
    }

    /**
     * O valor do arrasto: `início + andado × porDp`, preso em [min]..[max]
     * estendida até o início (valor digitado além da régua não salta de volta).
     * Início não finito parte do zero; resultado nunca é NaN.
     */
    fun scrubValue(from: Float, walkedDp: Float, unitsPerDp: Float, min: Float, max: Float): Float {
        val start = if (from.isFinite()) from else 0f
        val lo = if (min.isNaN()) Float.NEGATIVE_INFINITY else min
        val hi = if (max.isNaN()) Float.POSITIVE_INFINITY else max
        val (l, h) = dragBounds(lo, hi, start)
        val v = start + walkedDp * (if (unitsPerDp.isFinite()) unitsPerDp else 0f)
        return if (v.isNaN()) start else v.coerceIn(l, h)
    }

    /** Brilho do risco em `x` numa régua de largura `width`: 1 no centro, 0 nas bordas. */
    fun tickBrightness(x: Float, width: Float): Float {
        if (!(width > 0f)) return 0f
        val half = width / 2f
        return (1f - abs(x - half) / half).coerceIn(0f, 1f)
    }
}
