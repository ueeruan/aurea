package com.aurea.aurea.editor.timeline

import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.ui.theme.LayerType
import org.junit.Assert.*
import org.junit.Test

/** Fileiras por LINHA: trechos com o mesmo `trackId` dividem a mesma fileira. */
class TimelineSharedRowsTest {
    private fun clip(
        id: Long, line: Int, start: Int, end: Int,
        magnetic: Boolean = false, type: LayerType = LayerType.Video,
        visible: Boolean = true, locked: Boolean = false,
    ) = RowModel(id, type, start, end, 0, visible, locked, magnetic, false, "c$id", 0,
        IntArray(0), emptyArray(), line = line)

    @Test fun layersWithoutAPartnerComeBackUntouched() {
        val rows = listOf(clip(1, 1, 0, 30), clip(2, 2, 0, 30), clip(3, 0, 0, 30), clip(4, 0, 0, 30))
        // Ninguém divide linha (e a linha 0 de projeto antigo nunca junta): a MESMA lista.
        assertSame(rows, sharedRows(rows))
    }

    @Test fun splitPiecesShareOneRowAtTheTopmostPiece() {
        val text = clip(9, 7, 0, 90, type = LayerType.Text)
        val a = clip(1, 3, 0, 30)
        val b = clip(2, 3, 30, 60)
        val overlay = clip(5, 8, 10, 20, type = LayerType.Image)
        val c = clip(3, 3, 60, 90)
        // Ordem de desenho (0 = frente): o C é o trecho MAIS ALTO da linha 3.
        val out = sharedRows(listOf(text, c, overlay, b, a))
        assertEquals(3, out.size)
        assertSame(text, out[0])
        val row = out[1]
        assertSame(overlay, out[2])
        assertNotNull(row.shared)
        assertEquals(listOf(1L, 2L, 3L), row.segments.map { it.id })   // em ordem de tempo
        assertEquals(0, row.start)
        assertEquals(90, row.end)
        assertEquals(3, row.line)
        assertSame(b, row.segment(2))
        assertNull(row.segment(9))
    }

    @Test fun overlappingPiecesOfALineDropToARowJustBelow() {
        // Duplicar no cabeçote: a cópia cai na MESMA linha, por cima do original.
        val original = clip(1, 4, 0, 60)
        val copy = clip(2, 4, 30, 90)
        val next = clip(3, 4, 60, 120)
        val other = clip(4, 5, 0, 10)
        val out = sharedRows(listOf(original, copy, next, other))
        assertEquals(3, out.size)
        assertEquals(listOf(1L, 3L), out[0].segments.map { it.id })   // encostam: dividem
        assertSame(copy, out[1])                                        // sobreposto: logo abaixo
        assertSame(other, out[2])
        // As fileiras da linha andam juntas ao reordenar; a outra camada não.
        val keys = timelineGroupKeys(out)
        assertEquals(keys[0], keys[1])
        assertNotEquals(keys[1], keys[2])
    }

    @Test fun sharedRowSummarisesThePill() {
        val out = sharedRows(listOf(
            clip(1, 2, 0, 30, visible = false, locked = true),
            clip(2, 2, 30, 60, visible = true, locked = false),
        ))
        val row = out.single()
        assertTrue(row.visible)     // algum trecho aparece: o olho está aberto
        assertFalse(row.locked)     // só trava quando todos travam
    }

    @Test fun magneticVideoKeepsTheTallRowWhenShared() {
        val layer = 56f
        val tall = sharedRows(listOf(clip(1, 2, 0, 30, magnetic = true), clip(2, 2, 30, 60, magnetic = true))).single()
        assertEquals(layer * 1.9f, timelineRowHeight(tall, layer, 1f), 0.001f)
        val plain = sharedRows(listOf(clip(1, 2, 0, 30, type = LayerType.Text), clip(2, 2, 30, 60, type = LayerType.Text))).single()
        assertEquals(layer, timelineRowHeight(plain, layer, 1f), 0.001f)
    }

    @Test fun expandedTracksBelongToTheOpenedPiece() {
        val a = clip(1, 2, 0, 30)
        val b = RowModel(2, LayerType.Video, 30, 60, 10, true, false, false, true, "b", 0,
            intArrayOf(35), arrayOf(listOf(KeyframeRow(0, -1, 15, 1f, 1, 0))), line = 2)
        val base = sharedRows(listOf(a, b, clip(3, 5, 0, 10)))
        val key = KeyframeRow(0, -1, 15, 1f, 1, 0)
        val out = expandedRows(base, 2, mapOf(2L to listOf(key)), emptyList())
        assertSame(base[0], out[0])
        val position = out.single { it.track == TimelineTrack(0, -1, 0) }
        assertEquals(2L, position.id)
        assertArrayEquals(intArrayOf(35), position.instants)   // no tempo do trecho B, não da fileira
        assertSame(base.last(), out.last())
        // As trilhas abertas andam com a linha ao reordenar.
        val keys = timelineGroupKeys(out)
        for (i in 0 until out.size - 1) assertEquals(keys[0], keys[i])
        assertNotEquals(keys[0], keys.last())
    }

    @Test fun reorderingOneLayerSendsTheSameSingleCommandAsBefore() {
        val order = longArrayOf(10, 11, 12, 13, 14)
        // Descendo sobre a 13: vai para a posição dela (3).
        assertEquals(listOf(11L to 3), RowOrder.moves(order, setOf(11L), 13L, up = false))
        // Subindo sobre a 11: vai para a posição dela (1).
        assertEquals(listOf(14L to 1), RowOrder.moves(order, setOf(14L), 11L, up = true))
    }

    @Test fun reorderingALineMovesAllItsPiecesTogether() {
        // Linha A = {1, 4}; destino = a camada 3.
        val order = longArrayOf(1, 2, 3, 4, 5)
        fun apply(moves: List<Pair<Long, Int>>): List<Long> {
            val cur = order.toMutableList()
            for ((id, to) in moves) { cur.remove(id); cur.add(to, id) }
            return cur
        }
        assertEquals(listOf(2L, 3L, 1L, 4L, 5L), apply(RowOrder.moves(order, setOf(1L, 4L), 3L, up = false)))
        // Linha B = {3, 5} subindo sobre a camada 2.
        assertEquals(listOf(1L, 3L, 5L, 2L, 4L), apply(RowOrder.moves(order, setOf(3L, 5L), 2L, up = true)))
        // Destino dentro do próprio grupo: nada a fazer.
        assertTrue(RowOrder.moves(order, setOf(1L, 4L), 4L, up = false).isEmpty())
    }
}
