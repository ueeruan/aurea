package com.aurea.aurea.community

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** As denúncias saem do app no formato que o servidor aceita (discovery/community.js). */
class ModerationRulesTest {
    private val uuid = "30000000-0000-0000-0000-000000000001"

    @Test
    fun targetsFollowTheServerFilter() {
        for (kind in listOf("post", "comment", "user")) {
            assertTrue(isReportTarget(kind, uuid))
            assertFalse(isReportTarget(kind, "job_123"))
        }
        assertTrue(isReportTarget("ai_video", "job_123.a:b-C"))
        assertFalse(isReportTarget("ai_video", "job/123"))
        assertFalse(isReportTarget("ai_video", "x".repeat(81)))
        assertFalse(isReportTarget("ai_video", ""))
        assertFalse(isReportTarget("preset", uuid)) // preset vai pela rota das legendas
    }

    @Test
    fun bodyCarriesReasonAndOnlyUsefulDetails() {
        assertEquals(8, reportReasons.size)
        val bare = reportBody("spam", "   ")
        assertEquals("spam", bare.getString("reason"))
        assertFalse(bare.has("details"))
        val full = reportBody("other", "  ${"a".repeat(600)}  ")
        assertEquals(REPORT_DETAILS_MAX, full.getString("details").length)
        assertTrue(runCatching { reportBody("boring", "") }.isFailure)
    }

    @Test
    fun clippingNeverSplitsAnEmoji() {
        val text = "a".repeat(REPORT_DETAILS_MAX - 1) + "😀"
        val clipped = text.clipReportDetails()
        assertEquals(REPORT_DETAILS_MAX - 1, clipped.length)
        assertFalse(clipped.last().isHighSurrogate())
        assertEquals("curto", "curto".clipReportDetails())
    }
}
