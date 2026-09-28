package com.aurea.aurea.editor.timeline

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Fileira única com a doca aberta "sem ficar impossível de mexer": a doca só
 * compacta a timeline enquanto não há trilhas de propriedade abertas nem
 * escolha de keyframes. Painel aberto continua sempre compacto.
 */
class TimelineCompactTest {
    @Test
    fun `painel aberto e sempre a fileira unica`() {
        for (dock in listOf(false, true)) for (tracks in listOf(false, true)) for (keys in listOf(false, true)) {
            assertTrue(timelineCompact(panel = true, dock = dock, tracksOpen = tracks, selectingKeys = keys))
        }
    }

    @Test
    fun `doca compacta so sem trilhas abertas e fora da escolha de keyframes`() {
        assertTrue(timelineCompact(panel = false, dock = true, tracksOpen = false, selectingKeys = false))
        // O ícone do tipo abriu as trilhas: a timeline volta inteira.
        assertFalse(timelineCompact(panel = false, dock = true, tracksOpen = true, selectingKeys = false))
        // Escolhendo keyframes: inteira.
        assertFalse(timelineCompact(panel = false, dock = true, tracksOpen = false, selectingKeys = true))
    }

    @Test
    fun `sem painel nem doca a timeline e inteira`() {
        assertFalse(timelineCompact(panel = false, dock = false, tracksOpen = false, selectingKeys = false))
        assertFalse(timelineCompact(panel = false, dock = false, tracksOpen = true, selectingKeys = true))
    }
}
