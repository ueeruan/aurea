package com.aurea.aurea.state

/** Null means there are no markers; a boundary keeps the current frame. */
internal fun markerNavigationTarget(frames: IntArray, current: Int, direction: Int): Int? {
    if (frames.isEmpty()) return null
    return if (direction > 0) frames.filter { it > current }.minOrNull() ?: current
    else if (direction < 0) frames.filter { it < current }.maxOrNull() ?: current
    else current
}
