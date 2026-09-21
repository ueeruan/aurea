package com.aurea.aurea.home

import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.scale
import androidx.compose.ui.graphics.drawscope.translate
import androidx.compose.ui.unit.Dp
import com.aurea.aurea.ui.theme.AureaColors

/**
 * A logo do Aurea em vetor (spec §2.22, `aurea_logo.dart` da A.01): o tubo
 * azul em quatro passadas (borda, degradê, faixa clara, brilho) e a esfera.
 * Sem saveLayer nem bitmap — a logo aparece dentro de listas e barras com
 * blur, e cada camada offscreen custaria um passe inteiro de GPU.
 *
 * Tudo é proporcional à base 108: a marca de 22 dp tem o mesmo desenho da de 96.
 */
@Composable
fun AureaLogo(size: Dp, modifier: Modifier = Modifier, withBackground: Boolean = true) {
    val clipped = if (withBackground) modifier.clip(RoundedCornerShape(size * 0.22f)) else modifier
    Spacer(
        clipped
            .size(size)
            .drawWithCache {
                val s = this.size.width / BASE
                onDrawBehind {
                    scale(s, s, pivot = Offset.Zero) {
                        if (withBackground) drawRect(AureaColors.Background, size = Size(BASE, BASE))
                        drawPath(TUBE, BORDER_COLOR, style = BORDER_STROKE)
                        drawPath(TUBE, TUBE_BRUSH, style = TUBE_STROKE)
                        translate(-0.75f, -0.75f) {
                            drawPath(TUBE, LIGHT_COLOR, style = LIGHT_STROKE)
                            translate(-0.85f, -0.85f) {
                                drawPath(TUBE, SHINE_COLOR, style = SHINE_STROKE)
                            }
                        }
                        drawCircle(SPHERE_BRUSH, radius = 8f, center = SPHERE_CENTER)
                        drawCircle(SPARK_COLOR, radius = 1.6f, center = SPARK_CENTER)
                    }
                }
            },
    )
}

// Geometria e cores da marca (arte, não tokens de UI): as MESMAS do SVG
// original e do ícone do app. Se a marca mudar, muda aqui e no ícone juntos.
private const val BASE = 108f
private const val STROKE = 9f

private val TUBE = Path().apply {
    moveTo(26f, 77f)
    cubicTo(26f, 45f, 39f, 29f, 62f, 29f)
    cubicTo(77f, 29f, 86f, 37f, 86f, 49f)
    cubicTo(86f, 61f, 76f, 68f, 59f, 68f)
    lineTo(44f, 68f)
}

private fun roundStroke(width: Float) = Stroke(width = width, cap = StrokeCap.Round, join = StrokeJoin.Round)

private val BORDER_STROKE = roundStroke(STROKE + 0.9f)
private val TUBE_STROKE = roundStroke(STROKE)
private val LIGHT_STROKE = roundStroke(STROKE * 0.46f)
private val SHINE_STROKE = roundStroke(STROKE * 0.17f)

private val BORDER_COLOR = Color(0xFF00134F)
private val LIGHT_COLOR = Color(0x8C4E9BF5)
private val SHINE_COLOR = Color(0x99DFF1FF)
private val SPARK_COLOR = Color(0xB3EAF6FF)

// Degradê ao longo do tubo: topRight → bottomLeft do retângulo (21, 24, 74, 60).
private val TUBE_BRUSH = Brush.linearGradient(
    0f to Color(0xFF3E8BF0),
    0.45f to Color(0xFF245D8C),
    1f to Color(0xFF001A63),
    start = Offset(95f, 24f),
    end = Offset(21f, 84f),
)

// Esfera: radial fora do centro (Alignment(-0,45; -0,5), raio 1,05 × 16).
private val SPHERE_CENTER = Offset(87f, 76f)
private val SPHERE_BRUSH = Brush.radialGradient(
    0f to Color(0xFF7FC0FF),
    0.45f to Color(0xFF1F63C8),
    1f to Color(0xFF001460),
    center = Offset(83.4f, 72f),
    radius = 16.8f,
)
private val SPARK_CENTER = Offset(84.6f, 73.2f)
