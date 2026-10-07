package com.aurea.aurea.engine

import org.junit.Assert.*
import org.junit.Test

/**
 * Classe de memória LOW (Galaxy A15/A16 de 4 GB, realme RMX2020, moto g52):
 * o Android mata o app em primeiro plano por LOW_MEMORY. Caches de bitmap
 * menores e pressão detectada mais cedo.
 */
class LowMemoryClassPolicyTest {
    private val mb = 1L shl 20

    @Test fun lowClassMatchesTheEngineCutRamFlagAndSmallHeaps() {
        // Mesmo corte do motor (memory_tier): abaixo de 4608 MiB.
        assertTrue(DeviceMemoryClass.isLow(3_700 * mb, false, 256))   // "4 GB" (A15/A16)
        assertTrue(DeviceMemoryClass.isLow(2_800 * mb, false, 256))   // "3 GB" (RMX2020)
        assertFalse(DeviceMemoryClass.isLow(4_608 * mb, false, 256))
        assertFalse(DeviceMemoryClass.isLow(5_500 * mb, false, 512))  // "6 GB"
        // isLowRamDevice ou heap por app pequeno, mesmo com RAM grande.
        assertTrue(DeviceMemoryClass.isLow(8_000 * mb, true, 512))
        assertTrue(DeviceMemoryClass.isLow(8_000 * mb, false, 192))
        assertFalse(DeviceMemoryClass.isLow(8_000 * mb, false, 256))
        // Não medido não rebaixa ninguém.
        assertFalse(DeviceMemoryClass.isLow(0, false, 0))
    }

    @Test fun uiBitmapCachesShrinkOnLowClassOnly() {
        // Miniaturas: heap/16 entre 8 e 24 MB, como antes; 8 MB na LOW.
        assertEquals((16 * mb).toInt(), DeviceMemoryClass.thumbnailCacheBytes(256 * mb, false))
        assertEquals((24 * mb).toInt(), DeviceMemoryClass.thumbnailCacheBytes(512 * mb, false))
        assertEquals((8 * mb).toInt(), DeviceMemoryClass.thumbnailCacheBytes(64 * mb, false))
        assertEquals((8 * mb).toInt(), DeviceMemoryClass.thumbnailCacheBytes(512 * mb, true))
        // Prévias de efeito: 16 MB; 8 MB na LOW.
        assertEquals((16 * mb).toInt(), DeviceMemoryClass.effectPreviewCacheBytes(false))
        assertEquals((8 * mb).toInt(), DeviceMemoryClass.effectPreviewCacheBytes(true))
    }

    @Test fun lowClassTrimsWithMoreSystemHeadroomLeft() {
        val maximum = 256L shl 20
        val threshold = 256L shl 20
        fun sample(policy: MemoryPressurePolicy, time: Long, reserveMb: Long) =
            policy.nextTrim(time, false, 200L shl 20, maximum, threshold + (reserveMb shl 20), threshold)
        // 160 MB acima do limiar: nada nos outros, RUNNING_LOW na classe LOW.
        assertEquals(0, sample(MemoryPressurePolicy(), 0, 160))
        assertEquals(10, sample(MemoryPressurePolicy(lowMemoryClass = true), 0, 160))
        // 80 MB: RUNNING_LOW nos outros, RUNNING_CRITICAL na classe LOW.
        assertEquals(10, sample(MemoryPressurePolicy(), 0, 80))
        assertEquals(15, sample(MemoryPressurePolicy(lowMemoryClass = true), 0, 80))
        // Folga grande: nenhum dos dois.
        assertEquals(0, sample(MemoryPressurePolicy(lowMemoryClass = true), 0, 400))
    }

    @Test fun lowClassStillRateLimitsRepeatedTrims() {
        val policy = MemoryPressurePolicy(lowMemoryClass = true)
        val maximum = 256L shl 20
        val threshold = 256L shl 20
        fun sample(time: Long, reserveMb: Long) =
            policy.nextTrim(time, false, 200L shl 20, maximum, threshold + (reserveMb shl 20), threshold)
        assertEquals(10, sample(1_000, 150))
        assertEquals(0, sample(2_000, 150))      // mesmo nível dentro de 10 s
        assertEquals(15, sample(2_001, 90))      // escala na hora
        assertEquals(0, sample(3_000, 90))
        assertEquals(15, sample(12_001, 90))     // repete depois de 10 s
        assertEquals(0, sample(13_000, 400))     // aliviou
    }
}
