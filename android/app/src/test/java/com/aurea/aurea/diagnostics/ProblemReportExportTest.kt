package com.aurea.aurea.diagnostics

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** "Não consigo exportar" sem detalhe: a última falha de export vai no relato, sem caminhos. */
class ProblemReportExportTest {
    @Test
    fun lastExportFailureIsAppendedToTheSteps() {
        assertEquals("abri e exportei", ProblemReport.stepsWithExport("abri e exportei", null))
        assertEquals("abri e exportei", ProblemReport.stepsWithExport("abri e exportei", "  "))
        assertEquals("abri\n\n[export] falhou: motivo=5", ProblemReport.stepsWithExport(" abri ", "falhou: motivo=5"))
        assertEquals("[export] falhou: motivo=1", ProblemReport.stepsWithExport("", "falhou: motivo=1"))
        val long = ProblemReport.stepsWithExport("x".repeat(ProblemReport.STEPS_MAX), "falhou")
        assertEquals(ProblemReport.STEPS_MAX, long.length)
    }

    /** BETA 2: falhas no meio (watchdog, verificação, encoder parado) com o que o dono precisa. */
    @Test
    fun exportDiagnosticCarriesDeviceCodecAndRetry() {
        val line = ProblemReport.exportDiagnosticLine(
            platform = "Android 34", device = "Xiaomi 23053RN02A", chip = "Mediatek MT6769Z / mt6768",
            event = "falhou no meio quadro=40/100; refazendo no modo compativel (nivel 2)", percent = 40,
            failure = 2, code = 26, safeMode = 0, softwareDecode = false, retry = "nivel 2",
            hevcNote = "encoder HEVC c2.mtk.hevc.encoder nao aceita 3840x2160@60",
            engine = "codec=H.264 encoder=c2.mtk.avc.encoder(hw) saida=1920x1080@30.000 travou=video",
            timestamp = "2026-10-13 10:00")
        for (part in listOf("Xiaomi 23053RN02A", "chip Mediatek MT6769Z / mt6768", "parou em 40%", "motivo=2", "codigo=26",
                "refez=nivel 2", "hevc->h264:", "encoder=c2.mtk.avc.encoder(hw)", "saida=1920x1080", "travou=video"))
            assertTrue("faltou $part em $line", line.contains(part))
        // Sem nova tentativa nem HEVC trocado: "refez=nao", sem a parte do HEVC.
        val plain = ProblemReport.exportDiagnosticLine("Android 29", "moto g52", "qcom", "nao iniciou codigo=33", 0, 9, 33,
            2, true, "", null, "", "2026-10-13 10:00")
        assertTrue(plain.contains("refez=nao") && plain.contains("decod=software") && !plain.contains("hevc"))
        assertTrue(ProblemReport.sanitize(line).contains("chip Mediatek MT6769Z / mt6768"))
        assertTrue(line.length < ProblemReport.LAST_EXPORT_MAX)
    }

    /** A chave "Anexar diagnóstico do último export" desligada: o relato vai sem ele. */
    @Test
    fun attachSwitchOffSendsOnlyTheUserText() {
        assertEquals("passos", ProblemReport.stepsWithExport("passos", null))
        assertTrue(ProblemReport.stepsWithExport("passos", "diag").endsWith("[export] diag"))
    }

    @Test
    fun pathsNeverLeaveTheDevice() {
        val s = ProblemReport.sanitize("motor=\"nao abriu /storage/emulated/0/Movies/Aurea/Meu video.mp4\" codigo=12")
        assertFalse(s.contains("/storage"))
        assertTrue(s.contains("<arquivo>"))
        assertTrue(s.contains("codigo=12"))
        assertEquals("motivo=5 quadro=3/90", ProblemReport.sanitize("motivo=5 quadro=3/90"))
    }
}
