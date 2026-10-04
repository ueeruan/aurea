package com.aurea.aurea.engine

import org.junit.Assert.*
import org.junit.Test

class ImageMemoryPolicyTest {
    @Test fun memoryPressureCannotChangePersistedImageGeometry() {
        assertEquals(1, ImageMemoryPolicy.sourceSampleSize(4096, 4096, 4096, 4096L * 4096))
        assertNull(ImageMemoryPolicy.sourceSampleSize(4096, 4096, 4096, 2048L * 2048))
        assertEquals(4, ImageMemoryPolicy.sourceSampleSize(12_000, 9000, 4096, 7_000_000))
        assertNull(ImageMemoryPolicy.sourceSampleSize(12_000, 9000, 4096, 5_000_000))
    }
    @Test fun pathologicalPortraitAndHugeDimensionsStayWithinBothBudgets() {
        for ((w, h) in listOf(512 to 100_000, 12_000 to 12_000, Int.MAX_VALUE to Int.MAX_VALUE)) {
            val sample = ImageMemoryPolicy.sampleSize(w, h, 1080, 1_000_000)!!
            val outW = (w.toLong() + sample - 1) / sample
            val outH = (h.toLong() + sample - 1) / sample
            assertTrue(outW <= 1080 && outH <= 1080)
            assertTrue(outW * outH <= 1_000_000)
        }
    }
    @Test fun decodingReservesHeapForRgbaCopiesAndRefusesExhaustedBudget() {
        assertEquals(1_398_101L, ImageMemoryPolicy.pixelBudget(32L shl 20))
        assertEquals(0L, ImageMemoryPolicy.pixelBudget(8L shl 20))
        assertNull(ImageMemoryPolicy.sampleSize(100, 100, 4096, 0))
        assertNull(ImageMemoryPolicy.sampleSize(-1, 100, 4096, 100))
        assertEquals(1, ImageMemoryPolicy.sampleSize(1920, 1080, 4096, 4096L * 4096))
    }
    @Test fun pressureEscalatesImmediatelyButDoesNotRepeatedlyPurgeEveryFrame() {
        val policy = MemoryPressurePolicy()
        val maximum = 256L shl 20
        assertEquals(0, policy.nextTrim(0, false, 200L shl 20, maximum))
        assertEquals(10, policy.nextTrim(1000, false, 30L shl 20, maximum))
        assertEquals(0, policy.nextTrim(2000, false, 30L shl 20, maximum))
        assertEquals(15, policy.nextTrim(2001, true, 200L shl 20, maximum))
        assertEquals(0, policy.nextTrim(3000, true, 200L shl 20, maximum))
        assertEquals(15, policy.nextTrim(12_001, true, 200L shl 20, maximum))
        assertEquals(0, policy.nextTrim(13_000, false, 200L shl 20, maximum))
        assertEquals(15, policy.nextTrim(13_001, false, 8L shl 20, maximum))
    }
}
