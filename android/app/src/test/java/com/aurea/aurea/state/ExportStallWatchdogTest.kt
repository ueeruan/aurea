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

    // --- 0.5.3 (P0-3): o tempo em que ninguém leu o progresso não é travamento ---

    private fun screenWatchdog() = ExportStallWatchdog(observeGapMs = ExportStallWatchdog.OBSERVE_GAP_MS)

    @Test fun tenMinuteGapBetweenTwoReadsDoesNotCancel() {
        // Serviço recusado + tela apagada: o processo congela e volta 10 min depois
        // com o MESMO quadro. Antes a primeira leitura via 600 s "sem progresso".
        val w = screenWatchdog()
        var t = 1_000L
        repeat(50) { assertEquals(ExportStallWatchdog.Verdict.Running, w.observe(t, 42, "exportando")); t += 100 }
        t += 10 * 60_000L
        assertEquals(ExportStallWatchdog.Verdict.Running, w.observe(t, 42, "exportando"))
        assertFalse(w.cancelled)
        // O prazo recomeça de onde estava: só o que foi OBSERVADO parado conta.
        val observedBefore = 49 * 100L
        var waited = 0L
        while (waited + 100 < stall - observedBefore) {
            t += 100; waited += 100
            assertEquals(ExportStallWatchdog.Verdict.Running, w.observe(t, 42, "exportando"))
        }
        t += 100
        assertEquals(ExportStallWatchdog.Verdict.Cancel, w.observe(t, 42, "exportando"))
    }

    @Test fun realStallObservedEvery100msStillCancelsAndGivesUp() {
        val w = screenWatchdog()
        var t = 0L
        var verdict = w.observe(t, 7, "montando 3/9")
        while (verdict == ExportStallWatchdog.Verdict.Running && t < stall + 1_000) { t += 100; verdict = w.observe(t, 7, "montando 3/9") }
        assertEquals(ExportStallWatchdog.Verdict.Cancel, verdict)
        assertEquals(stall, t)
        val cancelledAt = t
        verdict = ExportStallWatchdog.Verdict.Running
        while (verdict == ExportStallWatchdog.Verdict.Running && t < cancelledAt + giveUp + 1_000) { t += 100; verdict = w.observe(t, 7, "montando 3/9") }
        assertEquals(ExportStallWatchdog.Verdict.GiveUp, verdict)
        assertEquals(cancelledAt + giveUp, t)
    }

    @Test fun gapAfterTheCancelDoesNotGiveUpEarly() {
        val w = ExportStallWatchdog(stallMs = 1_000, giveUpMs = 500, observeGapMs = 200)
        var t = 0L
        while (w.observe(t, 1, "") == ExportStallWatchdog.Verdict.Running) t += 100
        assertTrue(w.cancelled)
        // Congelado logo depois do cancelar: o motor não teve os 500 ms para concluir.
        t += 60_000L
        assertEquals(ExportStallWatchdog.Verdict.Running, w.observe(t, 1, ""))
        t += 100; assertEquals(ExportStallWatchdog.Verdict.Running, w.observe(t, 1, ""))
        t += 100; assertEquals(ExportStallWatchdog.Verdict.Running, w.observe(t, 1, ""))
        t += 100; assertEquals(ExportStallWatchdog.Verdict.Running, w.observe(t, 1, ""))
        t += 100; assertEquals(ExportStallWatchdog.Verdict.Running, w.observe(t, 1, ""))
        t += 100; assertEquals(ExportStallWatchdog.Verdict.GiveUp, w.observe(t, 1, ""))
    }

    @Test fun slowButRegularReadsBelowTheGapStillCount() {
        // Leituras a cada 4 s (main thread engasgada) ainda são "alguém olhando".
        val w = screenWatchdog()
        var t = 0L
        var verdict = w.observe(t, 3, "x")
        while (verdict == ExportStallWatchdog.Verdict.Running && t < stall + 10_000) { t += 4_000; verdict = w.observe(t, 3, "x") }
        assertEquals(ExportStallWatchdog.Verdict.Cancel, verdict)
        assertEquals(stall, t)
        assertEquals(5_000L, ExportStallWatchdog.OBSERVE_GAP_MS)
    }

    // --- 0.5.3 (P0-8): o Cancelar da pessoa sempre termina ---

    @Test fun userCancelEndsAfterTheSameGiveUpAsTheWatchdog() {
        assertFalse(ExportStallWatchdog.userCancelExpired(-1L, 1_000_000L))            // ainda não pediu
        assertFalse(ExportStallWatchdog.userCancelExpired(50_000L, 50_000L))
        assertFalse(ExportStallWatchdog.userCancelExpired(50_000L, 50_000L + giveUp - 1))
        assertTrue(ExportStallWatchdog.userCancelExpired(50_000L, 50_000L + giveUp))   // 15 s, não 180 + 15
        assertTrue(ExportStallWatchdog.userCancelExpired(0L, giveUp))                  // relógio em zero também vale
        assertTrue(giveUp < stall)
    }

    @Test fun uiWaitsLongerThanEveryEngineDeadline() {
        // GPU 120 s, worker do encoder por fase até 45 s (ExportWatchdog.hpp).
        assertTrue(stall > 120_000L + 45_000L)
        assertEquals(180_000L, stall)
        assertEquals(15_000L, giveUp)
    }

    @Test fun safeModeOnlyClimbsToTheEngineSuggestionUpToTwo() {
        assertEquals(1, ExportStallWatchdog.nextSafeMode(0, 1))
        assertEquals(2, ExportStallWatchdog.nextSafeMode(0, 2))
        assertEquals(2, ExportStallWatchdog.nextSafeMode(1, 2))
        assertEquals(0, ExportStallWatchdog.nextSafeMode(0, 0))   // motor não sugeriu
        assertEquals(0, ExportStallWatchdog.nextSafeMode(1, 1))   // nunca repete o mesmo nível
        assertEquals(0, ExportStallWatchdog.nextSafeMode(2, 1))   // nunca volta
        assertEquals(0, ExportStallWatchdog.nextSafeMode(2, 2))   // nunca repete software
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

    /**
     * BETA 2: o conselho do motor (bits 16..18) vira a próxima tentativa — o
     * nível só sobe, e o decoder preso pede também a decodificação compatível.
     */
    @Test fun nextAttemptCarriesLevelAndSoftwareDecode() {
        val shift = com.aurea.aurea.engine.ExportProgress.RETRY_SHIFT
        val sw = com.aurea.aurea.engine.ExportProgress.RETRY_SOFTWARE_DECODE
        assertEquals(2, ExportStallWatchdog.nextAttempt(0, 2 shl shift))
        assertEquals(2 or ExportStallWatchdog.ATTEMPT_SOFTWARE_DECODE, ExportStallWatchdog.nextAttempt(0, (2 shl shift) or sw))
        assertEquals(0, ExportStallWatchdog.nextAttempt(2, (2 shl shift) or sw))   // uma vez só
        assertEquals(0, ExportStallWatchdog.nextAttempt(0, sw))                      // sem nível, sem tentativa
        val p = com.aurea.aurea.engine.ExportProgress()
        p.flags = (2 shl shift) or sw or (com.aurea.aurea.engine.ExportProgress.FAILURE_CODEC_HUNG shl
            com.aurea.aurea.engine.ExportProgress.FAILURE_SHIFT)
        assertEquals(2, p.retrySafeMode)
        assertTrue(p.retrySoftwareDecode)
        assertEquals(9, p.failure)
        assertEquals(1 shl 18, sw)
        assertEquals(1 shl 7, com.aurea.aurea.engine.ExportProgress.FLAG_SOFTWARE_DECODE)
    }
}
