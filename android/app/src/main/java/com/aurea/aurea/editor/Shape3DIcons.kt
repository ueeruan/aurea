package com.aurea.aurea.editor

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import com.aurea.aurea.ui.theme.AureaColors
import kotlin.math.cos
import kotlin.math.sin

/**
 * Ícone de cada forma 3D (a ordem do `Shape3DKind` do motor): traço fino com
 * uma face em destaque — diz "isto tem partes" sem precisar de miniatura 3D.
 */
internal fun DrawScope.drawShape3DIcon(kind: Int) {
    val s = size.minDimension
    val o = Offset((size.width - s) / 2, (size.height - s) / 2)
    fun p(x: Float, y: Float) = Offset(o.x + x * s, o.y + y * s)
    val line = ShellColors.DockTileContent
    val accent = AureaColors.Accent
    val stroke = Stroke(s * 0.055f)
    fun poly(pts: List<Offset>, fill: Color? = null) {
        val path = Path().apply {
            moveTo(pts[0].x, pts[0].y)
            for (i in 1 until pts.size) lineTo(pts[i].x, pts[i].y)
            close()
        }
        if (fill != null) drawPath(path, fill)
        drawPath(path, line, style = stroke)
    }
    val face = accent.copy(alpha = 0.55f)
    when (kind) {
        0 -> { // cubo
            poly(listOf(p(.18f, .34f), p(.62f, .34f), p(.62f, .84f), p(.18f, .84f)), fill = face)
            poly(listOf(p(.18f, .34f), p(.38f, .16f), p(.82f, .16f), p(.62f, .34f)))
            poly(listOf(p(.62f, .34f), p(.82f, .16f), p(.82f, .66f), p(.62f, .84f)))
        }
        1 -> { // esfera: metade de cima em destaque
            drawArc(face, 180f, 180f, true, p(.14f, .14f), Size(s * .72f, s * .72f))
            drawCircle(line, s * .36f, p(.5f, .5f), style = stroke)
            drawOval(line, p(.14f, .42f), Size(s * .72f, s * .16f), style = stroke)
        }
        2 -> { // cilindro
            drawOval(face, p(.22f, .12f), Size(s * .56f, s * .2f))
            drawOval(line, p(.22f, .12f), Size(s * .56f, s * .2f), style = stroke)
            drawLine(line, p(.22f, .22f), p(.22f, .78f), s * .055f)
            drawLine(line, p(.78f, .22f), p(.78f, .78f), s * .055f)
            drawArc(line, 0f, 180f, false, p(.22f, .68f), Size(s * .56f, s * .2f), style = stroke)
        }
        3 -> { // cone
            poly(listOf(p(.5f, .12f), p(.2f, .76f), p(.8f, .76f)), fill = face)
            drawOval(line, p(.2f, .68f), Size(s * .6f, s * .16f), style = stroke)
        }
        4 -> { // pirâmide
            poly(listOf(p(.5f, .12f), p(.16f, .72f), p(.56f, .86f)), fill = face)
            poly(listOf(p(.5f, .12f), p(.56f, .86f), p(.86f, .66f)))
        }
        5 -> { // toro: um quarto em destaque
            drawArc(accent.copy(alpha = .75f), 180f, 90f, false, p(.2f, .2f), Size(s * .6f, s * .6f), style = Stroke(s * .2f))
            drawCircle(line, s * .4f, p(.5f, .5f), style = stroke)
            drawCircle(line, s * .2f, p(.5f, .5f), style = stroke)
        }
        6 -> { // estrela: uma ponta em destaque
            val pts = Array(10) { k ->
                val r = if (k % 2 == 0) .42f else .18f
                val a = -Math.PI / 2 + k * Math.PI / 5
                p(.5f + r * cos(a).toFloat(), .54f + r * sin(a).toFloat())
            }
            poly(listOf(pts[0], pts[1], pts[9]), fill = face)
            poly(pts.toList())
        }
        7 -> { // coração: metade esquerda em destaque
            val path = Path().apply {
                moveTo(p(.5f, .3f).x, p(.5f, .3f).y)
                cubicTo(p(.5f, .1f).x, p(.5f, .1f).y, p(.1f, .12f).x, p(.1f, .12f).y, p(.12f, .4f).x, p(.12f, .4f).y)
                cubicTo(p(.14f, .6f).x, p(.14f, .6f).y, p(.4f, .72f).x, p(.4f, .72f).y, p(.5f, .86f).x, p(.5f, .86f).y)
                close()
            }
            drawPath(path, face)
            val full = Path().apply {
                moveTo(p(.5f, .3f).x, p(.5f, .3f).y)
                cubicTo(p(.5f, .1f).x, p(.5f, .1f).y, p(.1f, .12f).x, p(.1f, .12f).y, p(.12f, .4f).x, p(.12f, .4f).y)
                cubicTo(p(.14f, .6f).x, p(.14f, .6f).y, p(.4f, .72f).x, p(.4f, .72f).y, p(.5f, .86f).x, p(.5f, .86f).y)
                cubicTo(p(.6f, .72f).x, p(.6f, .72f).y, p(.86f, .6f).x, p(.86f, .6f).y, p(.88f, .4f).x, p(.88f, .4f).y)
                cubicTo(p(.9f, .12f).x, p(.9f, .12f).y, p(.5f, .1f).x, p(.5f, .1f).y, p(.5f, .3f).x, p(.5f, .3f).y)
                close()
            }
            drawPath(full, line, style = stroke)
        }
        8 -> { // cápsula
            drawArc(face, 180f, 180f, true, p(.3f, .1f), Size(s * .4f, s * .4f))
            drawRoundRect(line, p(.3f, .1f), Size(s * .4f, s * .8f), androidx.compose.ui.geometry.CornerRadius(s * .2f), style = stroke)
            drawLine(line, p(.3f, .3f), p(.7f, .3f), s * .04f)
            drawLine(line, p(.3f, .7f), p(.7f, .7f), s * .04f)
        }
        else -> { // diamante
            poly(listOf(p(.5f, .1f), p(.22f, .46f), p(.5f, .56f)), fill = face)
            poly(listOf(p(.5f, .1f), p(.78f, .46f), p(.5f, .9f), p(.22f, .46f)))
            drawLine(line, p(.22f, .46f), p(.5f, .56f), s * .04f)
            drawLine(line, p(.5f, .56f), p(.78f, .46f), s * .04f)
            drawLine(line, p(.5f, .1f), p(.5f, .9f), s * .04f)
        }
    }
}
