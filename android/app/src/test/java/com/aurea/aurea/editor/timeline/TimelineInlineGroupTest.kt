package com.aurea.aurea.editor.timeline

import com.aurea.aurea.ui.theme.LayerType
import org.junit.Assert.*
import org.junit.Test

/**
 * 0.5.3 ("Please let us expand group, seeing all contents of it without
 * ungrouping!!"): as camadas de dentro de um grupo aberto entram recuadas logo
 * depois dele (e das trilhas abertas dele), com o grupo na identidade da linha.
 */
class TimelineInlineGroupTest {
    private fun row(id: Long, type: LayerType = LayerType.Video, track: TimelineTrack? = null, group: Long = 0L, editable: Boolean = true) =
        RowModel(id, type, 100, 200, 0, true, false, false, false, "L$id", 0, IntArray(0), emptyArray(), track,
            group = group, inlineEditable = editable)

    @Test fun innerRowsFollowTheGroupAndItsLanes() {
        val group = row(7, LayerType.Group)
        val lane = row(7, LayerType.Group, track = TimelineTrack(0))
        val after = row(8)
        // Os ids de dentro repetem os de fora (cada composição tem a sua tabela).
        val innerA = row(8, group = 7)
        val innerB = row(1, LayerType.Image, group = 7)
        val base = listOf(row(1), group, lane, after)
        val out = withInlineGroups(base, mapOf(7L to listOf(innerA, innerB)))
        assertEquals(listOf(1L, 7L, 7L, 8L, 1L, 8L), out.map { it.id })
        assertSame(lane, out[2])
        assertSame(innerA, out[3])
        assertSame(innerB, out[4])
        assertTrue(out[3].inner && out[3].group == 7L)
        assertFalse(out[5].inner)
        // A linha de dentro tem a altura de camada (não de trilha) e miniatura se for mídia.
        assertEquals(28f, timelineRowHeight(innerB, 28f, 1f), 0.001f)
        assertTrue(innerB.hasThumbs)
    }

    @Test fun closedOrUnknownGroupsKeepTheSameList() {
        val base = listOf(row(1), row(7, LayerType.Group))
        assertSame(base, withInlineGroups(base, emptyMap()))
        // Grupo sem linhas de dentro lidas: nada entra.
        assertEquals(base, withInlineGroups(base, mapOf(99L to listOf(row(3, group = 99)))))
        // Uma camada de outro tipo com o mesmo id de um grupo aberto não recebe filhos.
        val notGroup = listOf(row(7))
        assertEquals(notGroup, withInlineGroups(notGroup, mapOf(7L to listOf(row(3, group = 7)))))
    }

    @Test fun innerRowsAreNeverConfusedWithOuterRowsOfTheSameId() {
        val outer = row(5)
        val inner = row(5, group = 9, editable = false)
        assertFalse(outer.inner)
        assertTrue(inner.inner)
        assertFalse(inner.inlineEditable)
        assertNotEquals(outer.group, inner.group)
    }
}
