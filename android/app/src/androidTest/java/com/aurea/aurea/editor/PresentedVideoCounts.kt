package com.aurea.aurea.editor

/** Same color and coverage requirements as the original 41 x 25 readiness grid.
 * Dense sampling avoids aliasing a rotated Motion Tile pattern into its gaps. */
internal class PresentedVideoCounts {
    var samples = 0
        private set
    var lit = 0
        private set
    var colored = 0
        private set
    val cyan = IntArray(3)
    val magenta = IntArray(3)

    fun add(pixel: Int, region: Int) {
        val r = (pixel ushr 16) and 255
        val g = (pixel ushr 8) and 255
        val b = pixel and 255
        if (maxOf(r, g, b) > 70) lit++
        if (maxOf(r, g, b) - minOf(r, g, b) > 25) colored++
        if (g > 160 && b > 160 && r < 110) cyan[region]++
        if (r > 160 && b > 160 && g < 110) magenta[region]++
        samples++
    }

    fun ready(): Boolean = samples > 0 &&
        lit * 1025L > 20L * samples && colored * 1025L > 10L * samples &&
        cyan.sum() * 1025L >= 20L * samples && magenta.sum() * 1025L >= 20L * samples &&
        (0..2).all { cyan[it] * 1025L >= 4L * samples && magenta[it] * 1025L >= 4L * samples }

    fun detail(): String = "colored=$colored lit=$lit samples=$samples videoCyan=${cyan.contentToString()} videoMagenta=${magenta.contentToString()}"
}
