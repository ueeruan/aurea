package com.aurea.aurea.engine

/** Shared composition settings, including the native preview sample budget. */
data class MotionBlurSettings(
    val enabled: Boolean,
    val angle: Float,
    val phase: Float,
    val samples: Int,
    val adaptiveLimit: Int,
    val previewSamples: Int,
) {
    companion object {
        internal fun fromNative(values: FloatArray): MotionBlurSettings? {
            if (values.size < 6 || (0..5).any { !values[it].isFinite() }) return null
            return MotionBlurSettings(values[0] != 0f, values[1], values[2],
                values[3].toInt(), values[4].toInt(), values[5].toInt())
        }
    }
}
