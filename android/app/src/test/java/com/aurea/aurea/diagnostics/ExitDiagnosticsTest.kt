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

    @Test fun exportFrameDetailFitsAndNeverCarriesPathsOrNames() {
        // O marcador do export leva o quadro ("fecha em 70%": em QUAL quadro).
        for (phase in ExitDiagnostics.Phase.entries) {
            val bytes = ExitDiagnostics.marker(phase, Int.MAX_VALUE, Long.MAX_VALUE, "f=4294967295/4294967295")
            assertTrue(bytes.size <= 128)
        }
        val s = ExitDiagnostics.marker(ExitDiagnostics.Phase.PROJECT_EXPORT_VIDEO, 2126, 5_000, "f=1400/2000").toString(Charsets.UTF_8)
        assertEquals("Aurea build=2126 phase=PROJECT_EXPORT_VIDEO f=1400/2000 uptimeMs=5000", s)
        // Caminho, espaço, URI e nome saem.
        assertEquals("f=1/2", ExitDiagnostics.cleanDetail("f=1/2"))
        assertFalse(ExitDiagnostics.cleanDetail("/storage/emulated/0/Minha Viagem.mp4 content://x").contains(" "))
        assertTrue(ExitDiagnostics.cleanDetail("x".repeat(100)).length <= 24)
        assertEquals("", ExitDiagnostics.cleanDetail("çãé"))
        // O detalhe não quebra a leitura do modo seguro de vídeo (uptimeMs no fim).
        val video = ExitDiagnostics.marker(ExitDiagnostics.Phase.VIDEO_READY, 2126, 1_000, "f=1/2").toString(Charsets.UTF_8)
        assertTrue(ExitDiagnostics.crashedDuringVideo(5, video, 2126, 2_000))
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
