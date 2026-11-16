package com.aurea.aurea.ui.platform

import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.nativeCanvas

/** No PC o Canvas do Compose é o do Skia; o `android.graphics.Canvas` do Aurea PC o embrulha. */
val DrawScope.platformCanvas: android.graphics.Canvas
    get() = android.graphics.Canvas(drawContext.canvas.nativeCanvas)
