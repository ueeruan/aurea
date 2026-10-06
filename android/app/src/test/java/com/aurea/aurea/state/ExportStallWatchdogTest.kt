package com.aurea.aurea.state

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * A última rede da tela contra "a exportação parou num percentual e nunca
 * termina" (ExportStallWatchdog) e a escada do modo de segurança que o motor
 * sugere (export/ExportWatchdog.hpp). O AureaModel.swift repete a mesma regra.
 */
class ExportStallWatchdogTest {
    private val stall = ExportStallWatchdog.STALL_MS
    private val giveUp = ExportStallWatchdog.GIVE_UP_MS

    @Test fun progressKeepsItRunningForever() {
        val w = ExportStallWatchdog()
        var t = 0L
        for (frame in 0 until 1_000) {
            // Um quadro a cada 100 s (export lentíssimo, mas andando).
            assertEquals(ExportStallWatchdog.Verdict.Running, w.observe(t, frame, "exportando"))
            t += 100_000L
        }
        assertFalse(w.cancelled)
    }

    @Test fun noProgressCancelsOnceThenGivesUp() {
        val w = ExportStallWatchdog()
        assertEquals(ExportStallWatchdog.Verdict.Running, w.observe(0, 42, "exportando"))
        assertEquals(ExportStallWatchdog.Verdict.Running, w.observe(stall - 1, 42, "exportando"))
        assertEquals(ExportStallWatchdog.Verdict.Cancel, w.observe(stall, 42, "exportando"))
        assertTrue(w.cancelled)
        // O cancelamento sai uma vez só; espera o motor concluir.
        assertEquals(ExportStallWatchdog.Verdict.Running, w.observe(stall + 1_000, 42, "exportando"))
        assertEquals(ExportStallWatchdog.Verdict.Running, w.observe(stall + giveUp - 1, 42, "exportando"))
        assertEquals(ExportStallWatchdog.Verdict.GiveUp, w.observe(stall + giveUp, 42, "exportando"))
    }

    @Test fun engineMessageCountsAsProgress() {
        // Upscale por IA: o quadro não muda por minutos, a mensagem "IA: x/y · %" sim.
        val w = ExportStallWatchdog()
        var t = 0L
        for (pct in 0..100) {
            assertEquals(ExportStallWatchdog.Verdict.Running, w.observe(t, 0, "IA: 1/300 · $pct%"))
            t += 60_000L
        }
        assertFalse(w.cancelled)
    }

    @Test fun progressAfterTheCancelStillEndsInGiveUpIfNeverConcluded() {
        val w = ExportStallWatchdog(stallMs = 1_000, giveUpMs = 500)
        w.observe(0, 1, "")
        assertEquals(ExportStallWatchdog.Verdict.Cancel, w.observe(1_000, 1, ""))
        assertEquals(ExportStallWatchdog.Verdict.Running, w.observe(1_200, 2, "cancelado"))
        assertEquals(ExportStallWatchdog.Verdict.GiveUp, w.observe(1_500, 2, "cancelado"))
    }

    @Test fun uiWaitsLongerThanEveryEngineDeadline() {
        // GPU 120 s, quadro 60 s, worker do encoder 45 s (ExportWatchdog.hpp).
        assertTrue(stall > 120_000L + 45_000L)
        assertEquals(180_000L, stall)
        assertEquals(15_000L, giveUp)
    }

    @Test fun safeModeOnlyClimbsToTheEngineSuggestionUpToTwo() {
        assertEquals(1, ExportStallWatchdog.nextSafeMode(0, 1))
        assertEquals(2, ExportStallWatchdog.nextSafeMode(1, 2))
        assertEquals(0, ExportStallWatchdog.nextSafeMode(0, 0))   // motor não sugeriu
        assertEquals(0, ExportStallWatchdog.nextSafeMode(1, 1))   // nunca repete o mesmo nível
        assertEquals(0, ExportStallWatchdog.nextSafeMode(2, 1))   // nunca volta
        assertEquals(0, ExportStallWatchdog.nextSafeMode(2, 3))   // acima do máximo
    }

    @Test fun retryLevelIsReadFromTheEngineFlags() {
        // Bits 16..17 (kExportRetryShift); o motivo nos 24..31 não vaza para ele.
        val p = com.aurea.aurea.engine.ExportProgress()
        p.flags = (2 shl com.aurea.aurea.engine.ExportProgress.RETRY_SHIFT) or
            (com.aurea.aurea.engine.ExportProgress.FAILURE_ENCODER_STALLED shl com.aurea.aurea.engine.ExportProgress.FAILURE_SHIFT) or
            com.aurea.aurea.engine.ExportProgress.FLAG_SAFE_MODE
        assertEquals(2, p.retrySafeMode)
        assertEquals(com.aurea.aurea.engine.ExportProgress.FAILURE_ENCODER_STALLED, p.failure)
        assertTrue(p.safeMode)
        assertFalse(p.softwareEncoder)
    }
}
