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

    @Test fun onlyCrashesDuringVideoImportOfThisBuildTurnOnSafeVideoMode() {
        val native = 5   // ApplicationExitInfo.REASON_CRASH_NATIVE
        val java = 4     // ApplicationExitInfo.REASON_CRASH
        val userRequested = 10
        val marked = 1_000_000L
        fun summary(phase: ExitDiagnostics.Phase, build: Int = 2125) =
            ExitDiagnostics.marker(phase, build, marked).toString(Charsets.UTF_8)
        fun video(reason: Int, s: String?, crashAt: Long? = marked + 5_000) =
            ExitDiagnostics.crashedDuringVideo(reason, s, 2125, crashAt)
        assertTrue(video(native, summary(ExitDiagnostics.Phase.VIDEO_NATIVE)))
        assertTrue(video(native, summary(ExitDiagnostics.Phase.VIDEO_NATIVE), crashAt = null))
        // Logo depois de importar ainda conta; muito depois (surface, 3D…) não.
        assertTrue(video(java, summary(ExitDiagnostics.Phase.VIDEO_READY)))
        assertFalse(video(native, summary(ExitDiagnostics.Phase.VIDEO_READY), crashAt = marked + ExitDiagnostics.VIDEO_READY_WINDOW_MS + 1))
        assertFalse(video(native, summary(ExitDiagnostics.Phase.VIDEO_READY), crashAt = null))
        assertFalse(video(native, summary(ExitDiagnostics.Phase.VIDEO_READY), crashAt = marked - 1))
        // Crash de outro build não prende o build novo nos planos da CPU.
        assertFalse(video(native, summary(ExitDiagnostics.Phase.VIDEO_NATIVE, build = 2124)))
        assertFalse(video(native, summary(ExitDiagnostics.Phase.VIDEO_NATIVE, build = 21250)))
        assertFalse(video(native, summary(ExitDiagnostics.Phase.ENGINE_READY)))
        assertFalse(video(native, summary(ExitDiagnostics.Phase.VIDEO_FAILED)))
        assertFalse(video(userRequested, summary(ExitDiagnostics.Phase.VIDEO_NATIVE)))
        assertFalse(video(native, null))
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
