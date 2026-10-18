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

    @Test
    fun pathsNeverLeaveTheDevice() {
        val s = ProblemReport.sanitize("motor=\"nao abriu /storage/emulated/0/Movies/Aurea/Meu video.mp4\" codigo=12")
        assertFalse(s.contains("/storage"))
        assertTrue(s.contains("<arquivo>"))
        assertTrue(s.contains("codigo=12"))
        assertEquals("motivo=5 quadro=3/90", ProblemReport.sanitize("motivo=5 quadro=3/90"))
    }

    @Test fun diagnosticsSurviveLongUserStepsAndRemainWithinTheReportLimit() {
        val evidence = "{\"encoder\":\"c2.vendor.avc.encoder\",\"firstPacketMs\":-1,\"encoderStage\":\"codec_open\"}"
        val combined = ProblemReport.stepsWithExportDiagnostics("x".repeat(4000), "codigo=26", evidence)
        assertTrue(combined.endsWith(evidence))
        assertTrue(combined.contains("[export] codigo=26"))
        assertTrue(combined.length <= ProblemReport.STEPS_MAX)
        assertEquals("texto", ProblemReport.stepsWithExportDiagnostics("texto", null, null))
    }
}
