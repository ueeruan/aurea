package com.aurea.aurea.diagnostics

import org.junit.Assert.*
import org.junit.Test

/**
 * O "etapa" do relatório de crash vem de ApplicationExitInfo.getProcessStateSummary(),
 * que é UM por processo: o WebView do SDK de anúncios grava o dele por cima do
 * nosso (relatório real do Galaxy A15: versão do Chromium + byte 0x01 + hash +
 * binário). Só a nossa etapa, com o prefixo mágico, vira texto.
 */
class ExitPhaseParsingTest {
    @Test fun ourMarkerRoundTripsForEveryPhase() {
        for (phase in ExitDiagnostics.Phase.entries) {
            val bytes = ExitDiagnostics.marker(phase, 2143, 123_456_789L, "f=10/20")
            val parsed = ExitDiagnostics.parseSummary(bytes)
            assertNotNull(parsed)
            assertTrue(parsed!!.startsWith(ExitDiagnostics.MARKER_MAGIC))
            assertTrue(parsed.contains("phase=${phase.name} "))
            assertEquals(parsed, ExitDiagnostics.phaseLabel(bytes))
        }
    }

    @Test fun webViewSummaryFromTheGalaxyA15ReportIsUnknown() {
        // Bytes do relatório crash-8208923084894 (SM-A156M, LOW_MEMORY):
        // "153.0.8010.36" 0x01 "7cde7240daeffca7cbc725fe0127898abce43763" 0x01 + binário.
        val webView = "153.0.8010.36".toByteArray(Charsets.US_ASCII) + byteArrayOf(1) +
            "7cde7240daeffca7cbc725fe0127898abce43763".toByteArray(Charsets.US_ASCII) +
            byteArrayOf(1, 0x61, 0x8f.toByte(), 0x6b, 0x5e, 0x13, 0x7a, 0xff.toByte(), 0xfe.toByte())
        assertNull(ExitDiagnostics.parseSummary(webView))
        assertEquals(ExitDiagnostics.UNKNOWN_PHASE, ExitDiagnostics.phaseLabel(webView))
        assertEquals("desconhecida", ExitDiagnostics.UNKNOWN_PHASE)
    }

    @Test fun rejectsMissingPrefixInvalidUtf8ControlCharsAndOversize() {
        assertNull(ExitDiagnostics.parseSummary(null))
        assertNull(ExitDiagnostics.parseSummary(ByteArray(0)))
        // Texto legível, mas de outro componente.
        assertNull(ExitDiagnostics.parseSummary("153.0.8010.36".toByteArray()))
        assertNull(ExitDiagnostics.parseSummary("aurea build=1 phase=X".toByteArray()))
        // Prefixo certo + UTF-8 inválido (sobrescrita parcial).
        val torn = "Aurea build=2143 phase=VIDEO_NATIVE ".toByteArray() + byteArrayOf(0xc3.toByte(), 0x28)
        assertNull(ExitDiagnostics.parseSummary(torn))
        // Prefixo certo + caractere de controle.
        assertNull(ExitDiagnostics.parseSummary("Aurea build=2143\u0001phase=X".toByteArray()))
        assertNull(ExitDiagnostics.parseSummary("Aurea build=2143 phase=X\n".toByteArray()))
        // Prefixo certo + não ASCII (válido em UTF-8, mas nunca escrito por nós).
        assertNull(ExitDiagnostics.parseSummary("Aurea build=2143 phase=ç".toByteArray(Charsets.UTF_8)))
        // Acima do teto do Android (128 bytes).
        val big = ("Aurea build=2143 phase=" + "A".repeat(200)).toByteArray()
        assertNull(ExitDiagnostics.parseSummary(big))
        assertEquals(ExitDiagnostics.UNKNOWN_PHASE, ExitDiagnostics.phaseLabel(big))
    }

    @Test fun markersOfOlderBuildsStillParse() {
        // Builds anteriores gravaram o mesmo formato: o histórico de saídas pode
        // trazer o encerramento de antes da atualização.
        val old = "Aurea build=2127 phase=PROJECT_OPEN uptimeMs=5000".toByteArray(Charsets.UTF_8)
        assertEquals("Aurea build=2127 phase=PROJECT_OPEN uptimeMs=5000", ExitDiagnostics.parseSummary(old))
    }

    @Test fun safeVideoModeIgnoresForeignSummaries() {
        // crashedDuringVideo recebe o resumo já validado; o do WebView vira null
        // e nunca liga o modo seguro de vídeo.
        val webView = "153.0.8010.36".toByteArray() + byteArrayOf(1, 2, 3)
        assertFalse(ExitDiagnostics.crashedDuringVideo(
            5 /* ApplicationExitInfo.REASON_CRASH_NATIVE */, ExitDiagnostics.parseSummary(webView), 2143, null))
        val ours = ExitDiagnostics.marker(ExitDiagnostics.Phase.VIDEO_NATIVE, 2143, 1_000L)
        assertTrue(ExitDiagnostics.crashedDuringVideo(
            5 /* ApplicationExitInfo.REASON_CRASH_NATIVE */, ExitDiagnostics.parseSummary(ours), 2143, null))
    }
}
