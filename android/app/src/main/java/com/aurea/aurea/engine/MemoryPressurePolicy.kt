package com.aurea.aurea.engine

/** One-second sampling is independent of deprecated Android running-memory callbacks. */
internal class MemoryPressurePolicy {
    private var lastLevel = 0
    private var lastTrimMs = Long.MIN_VALUE

    fun nextTrim(nowMs: Long, systemLow: Boolean, heapHeadroom: Long, heapMaximum: Long): Int {
        val critical = maxOf(16L shl 20, heapMaximum / 20)
        val low = maxOf(32L shl 20, heapMaximum / 10)
        val level = when {
            systemLow || heapHeadroom <= critical -> 15
            heapHeadroom <= low -> 10
            else -> 0
        }
        if (level == 0) { lastLevel = 0; return 0 }
        if (level <= lastLevel && lastTrimMs != Long.MIN_VALUE && nowMs - lastTrimMs < 10_000) return 0
        lastLevel = level
        lastTrimMs = nowMs
        return level
    }
}

internal interface TrimmableImageCache { fun releaseImages() }

/** Weak registrations never extend a screen/ViewModel's lifetime. */
internal object UiImageCaches {
    private val caches = java.util.WeakHashMap<TrimmableImageCache, Unit>()
    fun register(cache: TrimmableImageCache) { synchronized(caches) { caches[cache] = Unit } }
    fun trim() {
        val current = synchronized(caches) { caches.keys.toList() }
        current.forEach { it.releaseImages() }
    }
}
