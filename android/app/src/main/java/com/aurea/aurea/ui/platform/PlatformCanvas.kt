package com.aurea.aurea.ui.platform

import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.nativeCanvas

/**
 * O `android.graphics.Canvas` por trás de um DrawScope (a timeline desenha
 * miniaturas, ondas e rótulos direto nele).
 *
 * Ponto de troca do Aurea PC: lá o Canvas do Compose é o do Skia, e
 * desktop/src/main/kotlin substitui este arquivo pelo equivalente sobre ele.
 * No Android é exatamente `drawContext.canvas.nativeCanvas`.
 */
val DrawScope.platformCanvas: android.graphics.Canvas
    get() = drawContext.canvas.nativeCanvas
