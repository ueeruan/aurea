@file:Suppress("unused", "UNUSED_PARAMETER")

package com.google.mlkit.vision.common

class InputImage private constructor(val width: Int, val height: Int) {
    companion object {
        @JvmStatic fun fromBitmap(bitmap: android.graphics.Bitmap, rotationDegrees: Int) = InputImage(bitmap.width, bitmap.height)
    }
}
