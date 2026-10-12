package com.aurea.aurea.home

import androidx.compose.animation.core.FastOutSlowInEasing
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R

/**
 * O bonequinho do Aurea em pixel art mandando um coração (tela de privacidade
 * da entrada). Desenho próprio, em grade de caracteres — a MESMA grade e as
 * mesmas cores do iOS (AureaApp.swift, `PrivacyMascot`).
 */
internal object PrivacyMascotPixels {
    val FIGURE = arrayOf(
        "...KKKKKK...",
        ".KKYYYYYYKK.",
        ".KYYYYYYYYK.",
        "KYYYYYYYYYYK",
        "KYYKYYYYKYYK",
        "KYYKYYYYKYYK",
        "KYPYYYYYYPYK",
        "KYYYKYYKYYYK",
        "KYYYYKKYYYYK",
        ".KYYYYYYYYK.",
        ".KKYYYYYYKK.",
        "...KKKKKK...",
    )
    val HEART = arrayOf(
        ".RR.RR.",
        "RLRRRRR",
        "RRRRRRR",
        ".RRRRR.",
        "..RRR..",
        "...R...",
    )

    fun color(c: Char): Color? = when (c) {
        'K' -> Color(0xFF3B2A1A)   // contorno
        'Y' -> Color(0xFFF5C542)   // corpo dourado
        'P' -> Color(0xFFFF8FA3)   // bochecha
        'R' -> Color(0xFFFF3B5C)   // coração
        'L' -> Color(0xFFFFD1DC)   // brilho do coração
        else -> null
    }
}

@Composable
internal fun PrivacyMascot(modifier: Modifier = Modifier) {
    val transition = rememberInfiniteTransition(label = "amor")
    val beat by transition.animateFloat(1f, 1.18f,
        infiniteRepeatable(tween(520, easing = FastOutSlowInEasing), RepeatMode.Reverse), label = "pulso")
    val rise by transition.animateFloat(0f, 1f, infiniteRepeatable(tween(1600, easing = LinearEasing)), label = "sobe")
    val description = stringResource(R.string.privacy_mascot_desc)
    Canvas(modifier.size(132.dp, 96.dp).semantics { contentDescription = description }) {
        val px = size.height / 16f
        val top = size.height - 12f * px
        pixels(PrivacyMascotPixels.FIGURE, 0f, top, px, 1f)
        // O coração sai do bonequinho: sobe, pulsa e some um pouco no alto.
        pixels(PrivacyMascotPixels.HEART, 13f * px, top + px - rise * 3f * px, px * beat, 1f - rise * 0.35f)
    }
}

private fun DrawScope.pixels(rows: Array<String>, x0: Float, y0: Float, cell: Float, alpha: Float) {
    rows.forEachIndexed { r, line ->
        line.forEachIndexed { c, ch ->
            val color = PrivacyMascotPixels.color(ch) ?: return@forEachIndexed
            // +0,5 px: sem frestas entre os quadradinhos em densidade fracionária.
            drawRect(color.copy(alpha = alpha), Offset(x0 + c * cell, y0 + r * cell), Size(cell + 0.5f, cell + 0.5f))
        }
    }
}
