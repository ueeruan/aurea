package com.aurea.aurea.engine

/** Editable anchors and outgoing segment resolved by the shared engine. */
data class KeyframeNavigation(
    val visibleCount: Int,
    val flags: Int,
    val localFrame: Int,
    val previousFrame: Int,
    val nextFrame: Int,
    val segmentStart: Int,
    val segmentEnd: Int,
) {
    val keyHere get() = flags and 1 != 0
    val hasPrevious get() = flags and 2 != 0
    val hasNext get() = flags and 4 != 0
    val hasSegment get() = flags and 8 != 0
}
