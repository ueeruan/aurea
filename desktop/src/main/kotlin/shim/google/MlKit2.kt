@file:Suppress("unused", "UNUSED_PARAMETER")

package com.google.mlkit.vision.pose.accurate

import com.google.mlkit.vision.pose.PoseDetectorOptionsBase

class AccuratePoseDetectorOptions private constructor() : PoseDetectorOptionsBase() {
    class Builder {
        fun setDetectorMode(mode: Int) = this
        fun build() = AccuratePoseDetectorOptions()
    }
    companion object {
        const val STREAM_MODE = 1
        const val SINGLE_IMAGE_MODE = 2
    }
}
