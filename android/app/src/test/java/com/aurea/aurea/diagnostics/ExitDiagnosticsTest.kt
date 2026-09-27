package com.aurea.aurea.diagnostics

import java.io.ByteArrayInputStream
import java.io.InputStream
import org.junit.Assert.*
import org.junit.Test

class ExitDiagnosticsTest {
    @Test fun processMarkersFitAndroidLimitAndOnlyContainDiagnosticFields() {
        for (phase in ExitDiagnostics.Phase.entries) {
            val bytes = ExitDiagnostics.marker(phase, Int.MAX_VALUE, Long.MAX_VALUE)
            assertTrue(bytes.size <= 128)
            assertTrue(bytes.toString(Charsets.UTF_8).contains("phase=${phase.name}"))
        }
    }

    @Test fun onlyCrashesDuringVideoPhasesTurnOnSafeVideoMode() {
        val native = 5   // ApplicationExitInfo.REASON_CRASH_NATIVE
        val java = 4     // ApplicationExitInfo.REASON_CRASH
        val userRequested = 10
        fun summary(phase: ExitDiagnostics.Phase) = ExitDiagnostics.marker(phase, 2124, 1).toString(Charsets.UTF_8)
        assertTrue(ExitDiagnostics.crashedDuringVideo(native, summary(ExitDiagnostics.Phase.VIDEO_NATIVE)))
        assertTrue(ExitDiagnostics.crashedDuringVideo(native, summary(ExitDiagnostics.Phase.VIDEO_READY)))
        assertTrue(ExitDiagnostics.crashedDuringVideo(java, summary(ExitDiagnostics.Phase.VIDEO_READY)))
        assertFalse(ExitDiagnostics.crashedDuringVideo(native, summary(ExitDiagnostics.Phase.ENGINE_READY)))
        assertFalse(ExitDiagnostics.crashedDuringVideo(native, summary(ExitDiagnostics.Phase.VIDEO_FAILED)))
        assertFalse(ExitDiagnostics.crashedDuringVideo(userRequested, summary(ExitDiagnostics.Phase.VIDEO_READY)))
        assertFalse(ExitDiagnostics.crashedDuringVideo(native, null))
    }

    @Test fun traceAtLimitIsPreservedButLargerTraceIsNotExportedTruncated() {
        val binary = byteArrayOf(0, -1, 2, 13, 10)
        assertArrayEquals(binary, ExitDiagnostics.readBounded(ByteArrayInputStream(binary), binary.size))
        assertNull(ExitDiagnostics.readBounded(ByteArrayInputStream(binary), binary.size - 1))
        assertArrayEquals(byteArrayOf(), ExitDiagnostics.readBounded(ByteArrayInputStream(byteArrayOf()), 0))
    }

    @Test fun oversizeTraceStopsReadingAtLimitPlusOneByte() {
        var consumed = 0
        val infinite = object : InputStream() {
            override fun read(): Int { consumed++; return 42 }
        }
        assertNull(ExitDiagnostics.readBounded(infinite, 100))
        assertEquals(101, consumed)
    }

    @Test fun shortReadsDoNotLoseBinaryData() {
        val binary = ByteArray(17000) { it.toByte() }
        val input = object : ByteArrayInputStream(binary) {
            override fun read(b: ByteArray, off: Int, len: Int): Int = super.read(b, off, minOf(7, len))
        }
        assertArrayEquals(binary, ExitDiagnostics.readBounded(input, binary.size))
    }
}
