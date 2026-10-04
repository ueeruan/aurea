package com.aurea.aurea.engine

/** Bound transient decoding as well as the retained bitmap; arithmetic uses Long. */
internal object ImageMemoryPolicy {
    private const val RESERVE = 16L * 1024 * 1024

    fun pixelBudget(heapHeadroom: Long): Long = ((heapHeadroom - RESERVE).coerceAtLeast(0) / 12)
        .coerceAtMost(4096L * 4096)

    fun sampleSize(width: Int, height: Int, maxDimension: Int, maxPixels: Long): Int? {
        if (width <= 0 || height <= 0 || maxDimension <= 0 || maxPixels <= 0) return null
        var sample = 1L
        while (sample <= (1L shl 30)) {
            val w = (width.toLong() + sample - 1) / sample
            val h = (height.toLong() + sample - 1) / sample
            if (w <= maxDimension && h <= maxDimension && w * h <= maxPixels) return sample.toInt()
            sample *= 2
        }
        return null
    }

    /** Asset pixel dimensions affect layer geometry: RAM may refuse, never resize them. */
    fun sourceSampleSize(width: Int, height: Int, maxDimension: Int, availablePixels: Long): Int? {
        val sample = sampleSize(width, height, maxDimension, Long.MAX_VALUE) ?: return null
        val w = (width.toLong() + sample - 1) / sample
        val h = (height.toLong() + sample - 1) / sample
        return sample.takeIf { w * h <= availablePixels }
    }

    fun availablePixels(): Long {
        val runtime = Runtime.getRuntime()
        return pixelBudget(runtime.maxMemory() - (runtime.totalMemory() - runtime.freeMemory()))
    }
}
