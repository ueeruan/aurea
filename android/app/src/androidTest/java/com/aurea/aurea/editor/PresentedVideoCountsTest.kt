package com.aurea.aurea.editor

import android.graphics.Color
import org.junit.Assert.*
import org.junit.Test

class PresentedVideoCountsTest {
    @Test fun originalCoverageRequirementIsPreservedAtAnyDensity() {
        for (scale in listOf(1, 2, 137)) {
            fun counts(cyanPerRegion: Int, magentaPerRegion: Int): PresentedVideoCounts {
                val result = PresentedVideoCounts()
                repeat(scale) {
                    for (region in 0..2) {
                        repeat(cyanPerRegion) { result.add(Color.CYAN, region) }
                        repeat(magentaPerRegion) { result.add(Color.MAGENTA, region) }
                        repeat((if (region == 2) 325 else 350) - cyanPerRegion - magentaPerRegion) {
                            result.add(Color.GREEN, region)
                        }
                    }
                }
                return result
            }
            assertTrue(counts(7, 7).ready())
            assertFalse("Generic colored shapes do not prove video readiness", counts(0, 0).ready())
            assertFalse("Cyan alone does not prove video readiness", counts(20, 0).ready())
            assertFalse("Magenta alone does not prove video readiness", counts(0, 20).ready())
            assertFalse("Total coverage threshold is unchanged", counts(6, 6).ready())
        }
    }

    @Test fun allThreeRegionsMustContainBothVideoBands() {
        val counts = PresentedVideoCounts()
        for (region in 0..2) {
            repeat(20) { counts.add(Color.CYAN, region) }
            repeat(if (region == 2) 3 else 30) { counts.add(Color.MAGENTA, region) }
            repeat((if (region == 2) 325 else 350) - 20 - (if (region == 2) 3 else 30)) {
                counts.add(Color.GREEN, region)
            }
        }
        assertFalse("A missing band on one side must still fail", counts.ready())
        assertFalse("An empty image must fail", PresentedVideoCounts().ready())
    }

    @Test fun denseSamplingSeesRotatedThinBandsThatTheSparseGridMisses() {
        val sparse = PresentedVideoCounts()
        val dense = PresentedVideoCounts()
        // Horizontal bands alternate with bright green. Every old grid row
        // lands at y=0 mod 40, while both video bands are between those rows.
        fun pixel(y: Int) = when (y % 40) {
            in 10..13 -> Color.CYAN
            in 20..23 -> Color.MAGENTA
            else -> Color.GREEN
        }
        for (y in 0..24) for (x in 0..40) sparse.add(pixel(y * 40), minOf(2, x * 3 / 41))
        for (y in 0..960) for (x in 0..480) dense.add(pixel(y), minOf(2, x * 3 / 481))
        assertFalse(sparse.ready())
        assertTrue(dense.ready())
    }
}
