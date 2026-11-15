package com.aurea.aurea.editor

import com.aurea.aurea.captions.CaptionBlock
import com.aurea.aurea.captions.CaptionTrack
import com.aurea.aurea.captions.CaptionWorkspace
import com.aurea.aurea.captions.Word
import org.junit.Assert.assertEquals
import org.junit.Test

/** 0.5.3: as contas puras das abas Transcrição e Legendas (gêmeas em Swift). */
class CaptionWorkspaceTest {
    private fun w(t: String, a: Double, b: Double) = Word(t, a, b)

    @Test fun paragraphsBreakOnLongPausesSentencesAndSize() {
        val words = listOf(w("Oi", 0.0, 0.2), w("gente.", 0.3, 0.5), w("Hoje", 2.0, 2.2), w("vamos", 2.3, 2.5))
        assertEquals(listOf(0, 2), CaptionWorkspace.paragraphStarts(words))
        // Fim de frase só corta depois de [softWords] palavras.
        assertEquals(listOf(0, 2), CaptionWorkspace.paragraphStarts(words, pauseSec = 99.0, softWords = 2))
        assertEquals(listOf(0), CaptionWorkspace.paragraphStarts(words, pauseSec = 99.0))
        assertEquals(listOf(0, 3), CaptionWorkspace.paragraphStarts(words, pauseSec = 99.0, maxWords = 3))
        assertEquals(emptyList<Int>(), CaptionWorkspace.paragraphStarts(emptyList()))
    }

    @Test fun timecodesShowFramesAndHours() {
        assertEquals("00:01:15", CaptionWorkspace.timecode(45, 30.0))
        assertEquals("01:00:00", CaptionWorkspace.timecode(1800, 30.0))
        assertEquals("1:00:00:01", CaptionWorkspace.timecode(108001, 30.0))
        assertEquals("1:05", CaptionWorkspace.clock(65.4))
    }

    @Test fun cursorSplitsBeforeTheWordItIsIn() {
        assertEquals(2, CaptionWorkspace.wordsBefore("um dois tres", 7))
        assertEquals(1, CaptionWorkspace.wordsBefore("um dois tres", 5))
        assertEquals(2, CaptionWorkspace.wordsBefore("um\ndois tres", 8))
        assertEquals(0, CaptionWorkspace.wordsBefore("um dois", 0))
    }

    @Test fun replaceKeepsTheTimeOfEachWord() {
        val words = listOf(w("Ola", 1.0, 1.2), w("OLA,", 1.3, 1.5), w("hum", 2.0, 2.1))
        val (out, n) = CaptionWorkspace.replaceWords(words, "ola", "Olá", matchCase = false)
        assertEquals(2, n)
        assertEquals(listOf("Olá", "Olá,", "hum"), out.map { it.text })
        assertEquals(1.3, out[1].start, 0.0)
        val (gone, m) = CaptionWorkspace.replaceWords(words, "hum", "", matchCase = true)
        assertEquals(1, m)
        assertEquals(2, gone.size)
        assertEquals(0, CaptionWorkspace.replaceWords(words, "OLA", "x", matchCase = true).second - 1)
    }

    @Test fun activeBlockFollowsTheTimelineShift() {
        val track = CaptionTrack(1, 2, listOf(CaptionBlock(1, 0, 10, "a"), CaptionBlock(2, 12, 20, "b")), shift = 100)
        assertEquals(0, CaptionWorkspace.activeIndex(track, 105))
        assertEquals(-1, CaptionWorkspace.activeIndex(track, 111))
        assertEquals(1, CaptionWorkspace.activeIndex(track, 112))
    }
}
