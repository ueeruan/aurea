package com.aurea.aurea.editor

import kotlin.math.abs

/**
 * 0.4.6: o número do contador do texto dinâmico como o motor o mostra
 * (`text::format_counter`, TextAnimator.cpp): casas fixas, ponto de milhar,
 * vírgula decimal e nunca "-0". Usado só no rótulo do painel.
 */
internal fun formatCounter(value: Double, decimals: Int, grouping: Boolean): String {
    val d = decimals.coerceIn(0, 6)
    val v = if (value.isFinite()) value else 0.0
    val fixed = java.lang.String.format(java.util.Locale.ROOT, "%.${d}f", abs(v))
    val dot = fixed.indexOf('.')
    val digits = if (dot >= 0) fixed.substring(0, dot) else fixed
    val frac = if (dot >= 0) fixed.substring(dot + 1) else ""
    val nonzero = (digits + frac).any { it != '0' }
    val out = StringBuilder()
    if (v < 0 && nonzero) out.append('-')
    digits.forEachIndexed { i, c ->
        if (grouping && i > 0 && (digits.length - i) % 3 == 0) out.append('.')
        out.append(c)
    }
    if (frac.isNotEmpty()) out.append(',').append(frac)
    return out.toString()
}
