package com.aurea.aurea.editor

import org.junit.Assert.assertEquals
import org.junit.Test

/** As contas do "Gravar voz": nome da camada, relógio e medidor (as mesmas do iOS). */
class VoiceTakeTest {
    @Test fun firstNarrationIsNumberOne() {
        assertEquals("Voz 1", VoiceTake.nextName("Voz", emptyList()))
        assertEquals("Voz 1", VoiceTake.nextName("Voz", listOf("Música", "Vozes", "Voz", "Voz abc")))
    }

    @Test fun nextNameFollowsTheHighestNumberStillInUse() {
        // Apagar a "Voz 2" não faz a próxima repetir a "Voz 3" que ainda existe.
        assertEquals("Voz 4", VoiceTake.nextName("Voz", listOf("Voz 1", "Voz 3", "Texto")))
        assertEquals("Voice 11", VoiceTake.nextName("Voice", listOf("Voice 10", "Voice 9")))
        // Outro idioma: só o prefixo do idioma atual conta.
        assertEquals("Voz 1", VoiceTake.nextName("Voz", listOf("Voice 5")))
    }

    @Test fun elapsedIsMinutesAndSeconds() {
        assertEquals("00:00", VoiceTake.elapsedLabel(0L))
        assertEquals("00:00", VoiceTake.elapsedLabel(-40L))
        assertEquals("00:09", VoiceTake.elapsedLabel(9_999L))
        assertEquals("01:05", VoiceTake.elapsedLabel(65_000L))
        assertEquals("61:00", VoiceTake.elapsedLabel(3_660_000L))
    }

    @Test fun meterMapsDecibelsToZeroOne() {
        assertEquals(0f, VoiceTake.levelFromDb(-160f), 0f)
        assertEquals(0f, VoiceTake.levelFromDb(Float.NaN), 0f)
        assertEquals(0.5f, VoiceTake.levelFromDb(-25f), 1e-4f)
        assertEquals(1f, VoiceTake.levelFromDb(0f), 0f)
        assertEquals(1f, VoiceTake.levelFromDb(6f), 0f)
        assertEquals(0f, VoiceTake.levelFromAmplitude(0), 0f)
        assertEquals(1f, VoiceTake.levelFromAmplitude(32767), 1e-4f)
        // −6 dBFS ≈ metade da amplitude.
        assertEquals(0.88f, VoiceTake.levelFromAmplitude(16384), 0.01f)
    }

    @Test fun meterRisesAtOnceAndFallsSlowly() {
        assertEquals(0.9f, VoiceTake.smooth(0.1f, 0.9f), 0f)
        assertEquals(0.82f, VoiceTake.smooth(1f, 0f), 1e-4f)
    }
}
