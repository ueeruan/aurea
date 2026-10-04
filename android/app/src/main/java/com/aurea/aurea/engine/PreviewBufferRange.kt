package com.aurea.aurea.engine

/** Actual completed render-cache frames in composition time; end is exclusive. */
data class PreviewBufferRange(val startFrame: Long, val endFrame: Long)
