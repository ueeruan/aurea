package com.aurea.aurea.engine

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * 0.5.3 (P1-2): o teto do export vem do maior tamanho que o ENCODER aceita, não
 * dos máximos de largura e altura anunciados separados (DeviceProfile.largestSize).
 */
class DeviceProfileSizeTest {
    @Test fun announcedCombinationThatTheEncoderAcceptsIsKept() {
        assertEquals(3840 to 2160, DeviceProfile.largestSize(3840, 2160) { _, _ -> true })
        assertEquals(4096 to 2304, DeviceProfile.largestSize(4096, 2304) { w, h -> w <= 4096 && h <= 2304 })
    }

    @Test fun separateMaximaFallToTheLargestSizeReallyAccepted() {
        // Anuncia 1920 × 1920, mas só aceita até 1920 × 1088 (macroblocos de 1080p).
        val encoder = { w: Int, h: Int -> w <= 1920 && h <= 1088 }
        assertEquals(1920 to 1080, DeviceProfile.largestSize(1920, 1920, encoder))
        // Anuncia 4096 × 4096 e só faz 1440p em paisagem.
        assertEquals(2560 to 1440, DeviceProfile.largestSize(4096, 4096) { w, h -> w * h <= 2560 * 1440 })
    }

    @Test fun nothingAcceptedKeepsTheAnnouncedValuesAsBefore() {
        assertEquals(1920 to 1920, DeviceProfile.largestSize(1920, 1920) { _, _ -> false })
    }
}
