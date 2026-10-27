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
    private fun layout(mains: List<Boolean>, hasAudio: Boolean, compact: Boolean) =
        EditRowLayout.compute(mains, hasAudio, compact, rulerTop = 34f, main = 56f, row = 34f, gap = 6f, pad = 12f)

    @Test fun editRowsKeepTheDrawingGeometry() {
        // Principal + texto + áudio, sem a faixa "+ áudio" (já há áudio).
        val l = layout(listOf(true, false, false), hasAudio = true, compact = false)
        assertEquals(34f, l.rowTops[0], 0.01f)
        assertEquals(34f + 56f + 6f, l.rowTops[1], 0.01f)
        assertEquals(34f + 56f + 6f + 34f + 6f, l.rowTops[2], 0.01f)
        assertEquals(l.rowTops[2] + 34f + 12f, l.contentBottom, 0.01f)
        // Sem áudio a faixa "+ áudio" ocupa uma linha embaixo da principal (fora do compacto).
        val lane = layout(listOf(true, false), hasAudio = false, compact = false)
        assertEquals(34f + 56f + 6f + 34f + 6f, lane.rowTops[1], 0.01f)
        val compact = layout(listOf(true, false), hasAudio = false, compact = true)
        assertEquals(34f + 56f + 6f, compact.rowTops[1], 0.01f)
        // Sem trilha principal: a dica vazia ocupa o lugar dela.
        val empty = layout(listOf(false), hasAudio = false, compact = false)
        assertEquals(34f + 56f + 6f, empty.rowTops[0], 0.01f)
    }

    @Test fun scrollStopsWhenTheLastRowReachesTheBottom() {
        val l = layout(List(6) { it == 0 }, hasAudio = true, compact = false)
        val viewport = 200f
        val max = EditRowLayout.maxScroll(l.contentBottom, viewport)
        assertEquals(l.contentBottom - viewport, max, 0.01f)
        // Poucas fileiras: não rola. Antes de medir: não rola.
        assertEquals(0f, EditRowLayout.maxScroll(120f, 400f), 0f)
        assertEquals(0f, EditRowLayout.maxScroll(900f, 0f), 0f)
    }

    @Test fun oneAxisPerDrag() {
        assertEquals(EditPanAxis.TIME, EditPanAxis.of(20f, 5f))
        assertEquals(EditPanAxis.TIME, EditPanAxis.of(-20f, 19f))
        assertEquals(EditPanAxis.TIME, EditPanAxis.of(10f, 10f))
        assertEquals(EditPanAxis.ROWS, EditPanAxis.of(3f, -12f))
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
