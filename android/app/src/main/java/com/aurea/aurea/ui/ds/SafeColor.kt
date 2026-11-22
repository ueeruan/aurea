package com.aurea.aurea.ui.ds

import androidx.compose.ui.graphics.Color

/**
 * 1.0.6: amostra de cor de um valor do motor/projeto. `Color(r, g, b)` do
 * Compose LANÇA com um canal fora de 0..1 ou NaN — e esses valores chegam de
 * vetor/AE importado, preset, emissivo HDR ou curva que passa do ponto. Aqui o
 * canal é preso em 0..1 e NaN vira 0: a amostra mostra o mais perto possível e
 * o painel não fecha o app.
 */
fun swatchColor(r: Float, g: Float, b: Float, a: Float = 1f): Color =
    Color(channel(r), channel(g), channel(b), if (a.isNaN()) 1f else a.coerceIn(0f, 1f))

private fun channel(v: Float): Float = if (v.isNaN()) 0f else v.coerceIn(0f, 1f)
