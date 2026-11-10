package com.aurea.aurea.editor

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

// Ícones das ferramentas da Edição, em traços numa grade 24×24. A mesma tabela está
// em EditModeView.swift: o desenho é igual nas duas plataformas.

internal enum class EditIcon(val filled: Boolean, val strokes: Array<FloatArray>) {
    Select(true, arrayOf(floatArrayOf(6f, 3f, 6f, 19.5f, 10.4f, 15.6f, 13.2f, 21.6f, 15.9f, 20.4f, 13.1f, 14.4f, 19f, 14.4f, 6f, 3f))),
    Ripple(false, arrayOf(floatArrayOf(9.5f, 4f, 5f, 4f, 5f, 20f, 9.5f, 20f), floatArrayOf(9f, 12f, 20f, 12f), floatArrayOf(16f, 8f, 20f, 12f, 16f, 16f))),
    Roll(false, arrayOf(floatArrayOf(3f, 4f, 7.5f, 4f, 7.5f, 20f, 3f, 20f), floatArrayOf(21f, 4f, 16.5f, 4f, 16.5f, 20f, 21f, 20f), floatArrayOf(10f, 12f, 14f, 12f), floatArrayOf(11.5f, 9.5f, 9.5f, 12f, 11.5f, 14.5f), floatArrayOf(12.5f, 9.5f, 14.5f, 12f, 12.5f, 14.5f))),
    Razor(false, arrayOf(floatArrayOf(9.1f, 18f, 8.84f, 19.13f, 8.12f, 20.03f, 7.08f, 20.53f, 5.92f, 20.53f, 4.88f, 20.03f, 4.16f, 19.13f, 3.9f, 18f, 4.16f, 16.87f, 4.88f, 15.97f, 5.92f, 15.47f, 7.08f, 15.47f, 8.12f, 15.97f, 8.84f, 16.87f, 9.1f, 18f), floatArrayOf(9.1f, 6f, 8.84f, 7.13f, 8.12f, 8.03f, 7.08f, 8.53f, 5.92f, 8.53f, 4.88f, 8.03f, 4.16f, 7.13f, 3.9f, 6f, 4.16f, 4.87f, 4.88f, 3.97f, 5.92f, 3.47f, 7.08f, 3.47f, 8.12f, 3.97f, 8.84f, 4.87f, 9.1f, 6f), floatArrayOf(8.7f, 7.5f, 20.5f, 18.5f), floatArrayOf(8.7f, 16.5f, 20.5f, 5.5f))),
    Slip(false, arrayOf(floatArrayOf(7f, 4f, 3f, 4f, 3f, 20f, 7f, 20f), floatArrayOf(17f, 4f, 21f, 4f, 21f, 20f, 17f, 20f), floatArrayOf(7.5f, 12f, 16.5f, 12f), floatArrayOf(10f, 9f, 7f, 12f, 10f, 15f), floatArrayOf(14f, 9f, 17f, 12f, 14f, 15f))),
    Slide(false, arrayOf(floatArrayOf(8f, 4f, 16f, 4f, 16f, 13f, 8f, 13f, 8f, 4f), floatArrayOf(3f, 19f, 21f, 19f), floatArrayOf(6f, 16f, 3f, 19f, 6f, 22f), floatArrayOf(18f, 16f, 21f, 19f, 18f, 22f))),
    Snap(false, arrayOf(floatArrayOf(4f, 4f, 4f, 12f, 4f, 12f, 4.39f, 14.47f, 5.53f, 16.7f, 7.3f, 18.47f, 9.53f, 19.61f, 12f, 20f, 14.47f, 19.61f, 16.7f, 18.47f, 18.47f, 16.7f, 19.61f, 14.47f, 20f, 12f, 20f, 4f), floatArrayOf(9f, 4f, 9f, 12f, 9f, 12f, 9.15f, 12.93f, 9.57f, 13.76f, 10.24f, 14.43f, 11.07f, 14.85f, 12f, 15f, 12.93f, 14.85f, 13.76f, 14.43f, 14.43f, 13.76f, 14.85f, 12.93f, 15f, 12f, 15f, 4f), floatArrayOf(4f, 4f, 9f, 4f), floatArrayOf(15f, 4f, 20f, 4f), floatArrayOf(4f, 8f, 9f, 8f), floatArrayOf(15f, 8f, 20f, 8f))),
    MarkIn(false, arrayOf(floatArrayOf(15f, 3.5f, 12f, 3.5f, 10.5f, 5f, 10.5f, 10f, 8f, 12f, 10.5f, 14f, 10.5f, 19f, 12f, 20.5f, 15f, 20.5f))),
    MarkOut(false, arrayOf(floatArrayOf(9f, 3.5f, 12f, 3.5f, 13.5f, 5f, 13.5f, 10f, 16f, 12f, 13.5f, 14f, 13.5f, 19f, 12f, 20.5f, 9f, 20.5f))),
    Lift(false, arrayOf(floatArrayOf(12f, 15f, 12f, 4f), floatArrayOf(8f, 8f, 12f, 4f, 16f, 8f), floatArrayOf(3f, 20f, 9f, 20f), floatArrayOf(15f, 20f, 21f, 20f))),
    Extract(false, arrayOf(floatArrayOf(12f, 15f, 12f, 4f), floatArrayOf(8f, 8f, 12f, 4f, 16f, 8f), floatArrayOf(3f, 20f, 9f, 20f), floatArrayOf(6.5f, 17.5f, 9f, 20f, 6.5f, 22.5f), floatArrayOf(21f, 20f, 15f, 20f), floatArrayOf(17.5f, 17.5f, 15f, 20f, 17.5f, 22.5f))),
    RazorAll(false, arrayOf(floatArrayOf(3f, 6f, 10f, 6f), floatArrayOf(14f, 6f, 21f, 6f), floatArrayOf(3f, 12f, 10f, 12f), floatArrayOf(14f, 12f, 21f, 12f), floatArrayOf(3f, 18f, 10f, 18f), floatArrayOf(14f, 18f, 21f, 18f), floatArrayOf(12f, 2.5f, 12f, 21.5f))),
    AddVideo(false, arrayOf(floatArrayOf(3f, 7f, 14.5f, 7f, 14.5f, 17f, 3f, 17f, 3f, 7f), floatArrayOf(19f, 9f, 19f, 15f), floatArrayOf(16f, 12f, 22f, 12f))),
    AddAudio(false, arrayOf(floatArrayOf(3.5f, 10f, 3.5f, 14f), floatArrayOf(7f, 7f, 7f, 17f), floatArrayOf(10.5f, 9f, 10.5f, 15f), floatArrayOf(14f, 11f, 14f, 13f), floatArrayOf(19f, 9f, 19f, 15f), floatArrayOf(16f, 12f, 22f, 12f))),
    ClearTracks(false, arrayOf(floatArrayOf(4f, 6f, 20f, 6f), floatArrayOf(9f, 6f, 9f, 3.5f, 15f, 3.5f, 15f, 6f), floatArrayOf(6f, 6f, 7f, 21f, 17f, 21f, 18f, 6f))),
    PlaceEnd(false, arrayOf(floatArrayOf(3f, 12f, 15f, 12f), floatArrayOf(11f, 8f, 15f, 12f, 11f, 16f), floatArrayOf(19f, 5f, 19f, 19f))),
    PlaceInsert(false, arrayOf(floatArrayOf(3f, 20f, 9f, 20f), floatArrayOf(15f, 20f, 21f, 20f), floatArrayOf(12f, 3f, 12f, 15f), floatArrayOf(8f, 11f, 12f, 15f, 16f, 11f))),
    PlaceOverwrite(false, arrayOf(floatArrayOf(12f, 3f, 12f, 13f), floatArrayOf(8f, 9f, 12f, 13f, 16f, 9f), floatArrayOf(3f, 17f, 21f, 17f, 21f, 21f, 3f, 21f, 3f, 17f))),
}

@Composable
internal fun EditToolIcon(icon: EditIcon, color: Color, size: Dp = 22.dp) {
    Canvas(Modifier.size(size)) {
        val k = this.size.width / 24f
        val line = Stroke(width = 1.7f * k, cap = StrokeCap.Round, join = StrokeJoin.Round)
        icon.strokes.forEach { points ->
            val path = Path()
            path.moveTo(points[0] * k, points[1] * k)
            var i = 2
            while (i + 1 < points.size) { path.lineTo(points[i] * k, points[i + 1] * k); i += 2 }
            if (icon.filled) drawPath(path, color) else drawPath(path, color, style = line)
        }
    }
}
