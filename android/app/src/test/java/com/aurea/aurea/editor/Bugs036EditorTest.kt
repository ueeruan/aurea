package com.aurea.aurea.editor

import com.aurea.aurea.ui.theme.AureaIconStyle
import com.aurea.aurea.ui.theme.CupertinoGlyph
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** 0.3.6 (editor): Edição, transporte, tamanho do texto e o ícone de corte. */
class Bugs036EditorTest {
    // Os mesmos números do desenho da Edição em 1× (régua 26 + 8, principal 56, linha 34, folga 6, base 12).
    @Test fun laneLayoutStopsWhenTheLastTrackReachesTheBottom() {
        val l = EditLaneLayout.compute(6, rulerH = 26f, laneH = 44f, gap = 1f)
        assertEquals(26f, l.tops[0], 0f)
        assertEquals(71f, l.tops[1], 0f)
        assertEquals(296f, l.bottom, 0f)
        assertEquals(2, l.laneAt(120f))
        assertEquals(-1, l.laneAt(10f))
        assertEquals(96f, EditLaneLayout.maxScroll(l.bottom, 200f), 0f)
        assertEquals(0f, EditLaneLayout.maxScroll(120f, 400f), 0f)
        assertEquals(0f, EditLaneLayout.maxScroll(900f, 0f), 0f)
    }

    @Test fun snapGoesToTheNearestTargetInReach() {
        val targets = intArrayOf(100, 130)
        assertEquals(100, snapFrame(103, targets, 5f))
        assertEquals(130, snapFrame(127, targets, 5f))
        assertEquals(115, snapFrame(115, targets, 5f))
    }

    @Test fun timelinePayloadBecomesLanesFromTopToBottom() {
        val raw = longArrayOf(
            4, 1, 12, -1,
            10, 16L, 11, 18L,
            20, 1L or 16L, 21, 1L or 4L,
            77, 20,
        )
        val data = EditTimelineData.parse(raw)!!
        assertEquals(listOf("V2", "V1", "A1", "A2"), data.lanes.map { it.label })
        assertEquals(true, data.tracks[1].locked)
        assertEquals(true, data.tracks[3].muted)
        assertEquals(false, data.tracks[3].target)
        assertEquals(20, data.linkedAudio[77L])
        assertEquals(12L, data.markIn)
        assertEquals(null, EditTimelineData.parse(longArrayOf(9, 9, 0, 0)))
    }

    @Test fun playIsInTheMiddleOfTheTransport() {
        assertEquals(0.5f, TransportLayout.playCenter(), 1e-6f)
        assertEquals(8f, TransportLayout.SIDE_WEIGHT * 2f + TransportLayout.CENTER_WEIGHT, 1e-6f)
    }

    @Test fun textSizeGoesBeyond144AndAcceptsTypedValues() {
        assertTrue(TextSizeSteps.last() >= 1000)
        assertEquals(TEXT_SIZE_MAX.toInt(), TextSizeSteps.last())
        for (i in 1 until TextSizeSteps.size) assertTrue(TextSizeSteps[i] > TextSizeSteps[i - 1])
        assertEquals(TextSizeSteps.indexOf(512), nearestSizeStep(500f))
        assertEquals(350f, parseTextSize("350")!!, 0f)
        assertEquals(72.5f, parseTextSize(" 72,5 ")!!, 0f)
        assertEquals(TEXT_SIZE_MAX, parseTextSize("99999")!!, 0f)
        assertEquals(1f, parseTextSize("0")!!, 0f)
        assertNull(parseTextSize("abc"))
        assertNull(parseTextSize(""))
    }

    @Test fun scissorsUseTheThinOutlineGlyph() {
        assertEquals(CupertinoGlyph.ScissorsAlt, AureaIconStyle.glyph(CupertinoGlyph.Scissors))
    }
}
