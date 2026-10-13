package com.aurea.aurea.state

import com.aurea.aurea.engine.ExportProgress
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ExportStartupFailureTest {
    private fun failed() = ExportProgress().apply {
        finished = true; result = 26; flags = 2 shl ExportProgress.RETRY_SHIFT
        message = "encoder timeout"
    }

    @Test fun admissionFailureCannotReuseOldRetryOrCause() {
        val old = failed()
        val before = ExportStartupFailure.snapshot(old)
        assertFalse(ExportStartupFailure.isFresh(before, failed(), 26, "123", "123"))
        old.framesDone = 1 // a late update still belongs to the old session
        assertFalse(ExportStartupFailure.isFresh(before, old, 26, "123", "123"))
    }

    @Test fun repeatedIdenticalFailureInNewSessionCanStillRetry() {
        val before = ExportStartupFailure.snapshot(failed())
        assertTrue(ExportStartupFailure.isFresh(before, failed(), 26, "123", "456"))
    }

    @Test fun missingDiagnosticsFallsBackToImmutableProgressComparison() {
        val candidate = failed()
        val before = ExportStartupFailure.snapshot(candidate)
        assertFalse(ExportStartupFailure.isFresh(before, candidate, 26, "", ""))
        candidate.message = "new refusal"
        assertTrue(ExportStartupFailure.isFresh(before, candidate, 26, "", ""))
        candidate.finished = false
        assertFalse(ExportStartupFailure.isFresh(before, candidate, 26, "123", "456"))
        candidate.finished = true; candidate.result = 9
        assertFalse(ExportStartupFailure.isFresh(before, candidate, 26, "123", "456"))
    }
}
