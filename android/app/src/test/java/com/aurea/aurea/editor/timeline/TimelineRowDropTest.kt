package com.aurea.aurea.editor.timeline

import com.aurea.aurea.ui.theme.LayerType
import org.junit.Assert.*
import org.junit.Test

/**
 * Arrasto vertical de UM trecho (beta: "os textos andam todos juntos"): onde ele
 * cai com o dedo em y. O motor faz a pilha; aqui só o pedido (âncora + modo).
 */
class TimelineRowDropTest {
    private fun clip(
        id: Long, line: Int, start: Int, end: Int,
        magnetic: Boolean = false, type: LayerType = LayerType.Text,
    ) = RowModel(id, type, start, end, 0, true, false, magnetic, false, "c$id", 0,
        IntArray(0), emptyArray(), line = line)

    private fun tops(n: Int, h: Float = 40f) = FloatArray(n + 1) { it * h }

    @Test fun threeIndependentTextsDropBetweenRows() {
        val t1 = clip(1, 1, 0, 90)
        val t2 = clip(2, 2, 0, 90)
        val t3 = clip(3, 3, 0, 90)
        val rows = sharedRows(listOf(t1, t2, t3))
        assertEquals(3, rows.size)
        // T1 (fileira 0) para a metade de baixo de T3 (fileira 2): fundo da pilha.
        val bottom = RowDrop.target(rows, tops(3), 110f, t1, 0)
        assertEquals(RowDrop.INSERT, bottom.kind)
        assertEquals(0L, bottom.anchor)
        assertEquals(0, bottom.mode)
        assertEquals(120f, bottom.lineY, 0f)
        // T3 para a metade de cima de T1: acima da fileira de T1.
        val top = RowDrop.target(rows, tops(3), 5f, t3, 2)
        assertEquals(RowDrop.INSERT, top.kind)
        assertEquals(1L, top.anchor)
        assertEquals(0f, top.lineY, 0f)
        // T3 para a metade de baixo de T1: acima de T2 (o próprio vizinho).
        val between = RowDrop.target(rows, tops(3), 30f, t3, 2)
        assertEquals(2L, between.anchor)
        assertEquals(40f, between.lineY, 0f)
    }

    @Test fun droppingNextToItselfChangesNothing() {
        val t1 = clip(1, 1, 0, 90)
        val t2 = clip(2, 2, 0, 90)
        val rows = listOf(t1, t2)
        assertEquals(RowDrop.NONE, RowDrop.target(rows, tops(2), 20f, t1, 0).kind)   // no meio dela
        assertEquals(RowDrop.NONE, RowDrop.target(rows, tops(2), 2f, t1, 0).kind)    // borda de cima
        assertEquals(RowDrop.NONE, RowDrop.target(rows, tops(2), 45f, t1, 0).kind)   // logo abaixo
        assertEquals(RowDrop.INSERT, RowDrop.target(rows, tops(2), 70f, t1, 0).kind) // abaixo do T2
    }

    @Test fun textsOnTheSameOldLineMoveOneAtATime() {
        // Três textos que o duplicar antigo deixou na MESMA linha: sobrepostos,
        // cada um numa fileira, mas a linha era um bloco só no arrasto.
        val t1 = clip(1, 5, 0, 90)
        val t2 = clip(2, 5, 0, 90)
        val t3 = clip(3, 5, 0, 90)
        val rows = sharedRows(listOf(t1, t2, t3))
        assertEquals(3, rows.size)
        // Só a fileira do T3: no meio dela, nada; para cima de tudo, fileira própria.
        val source = rows.indexOfFirst { it.segment(3L) != null }
        assertEquals(RowDrop.NONE, RowDrop.target(rows, tops(3), source * 40f + 20f, t3, source).kind)
        val up = RowDrop.target(rows, tops(3), 1f, t3, source)
        assertEquals(RowDrop.INSERT, up.kind)
        assertNotEquals(3L, up.anchor)          // a âncora nunca é o próprio trecho
        assertEquals(0, up.mode)
        assertEquals(0f, up.lineY, 0f)
    }

    @Test fun joinsAMagneticLineOnlyWhenTheClipFits() {
        val a = clip(1, 4, 0, 30, magnetic = true, type = LayerType.Video)
        val c = clip(3, 4, 60, 90, magnetic = true, type = LayerType.Video)
        val fits = clip(7, 9, 30, 60)
        val wide = clip(8, 10, 20, 70)
        val rows = sharedRows(listOf(fits, wide, c, a))
        assertEquals(3, rows.size)                // fits, wide, linha 4
        val t = tops(3)
        val join = RowDrop.target(rows, t, 100f, fits, 0)   // meio da fileira da linha
        assertEquals(RowDrop.JOIN, join.kind)
        assertEquals(1, join.mode)
        assertEquals(2, join.row)
        assertTrue(join.anchor == 1L || join.anchor == 3L)
        // Não cabe (sobrepõe A e C): vira inserção pela metade da fileira.
        val noFit = RowDrop.target(rows, t, 100f, wide, 1)
        assertEquals(RowDrop.INSERT, noFit.kind)
        assertEquals(0L, noFit.anchor)               // metade de baixo da última = fundo
        // Bordas da fileira são sempre "entre fileiras".
        assertEquals(RowDrop.INSERT, RowDrop.target(rows, t, 82f, fits, 0).kind)
    }

    @Test fun plainSingleLayerRowIsNeverJoined() {
        val t1 = clip(1, 1, 0, 30)
        val t2 = clip(2, 2, 40, 90)
        val rows = listOf(t1, t2)
        val drop = RowDrop.target(rows, tops(2), 60f, t1, 0)
        assertEquals(RowDrop.INSERT, drop.kind)    // fileira de uma camada só: Alight Motion
        assertEquals(0L, drop.anchor)
    }

    @Test fun propertyRowsTravelWithTheirLayerGroup() {
        val t1 = clip(1, 1, 0, 90)
        val lane = RowModel(1, LayerType.Text, 0, 90, 0, true, false, false, false, "x", 0,
            IntArray(0), emptyArray(), track = TimelineTrack(0), line = 1)
        val t2 = clip(2, 2, 0, 90)
        val rows = listOf(t1, lane, t2)
        val t = floatArrayOf(0f, 40f, 56f, 96f)
        // Metade de cima do grupo T1+trilha: acima do T1.
        val drop = RowDrop.target(rows, t, 20f, t2, 2)
        assertEquals(RowDrop.INSERT, drop.kind)
        assertEquals(1L, drop.anchor)
        assertEquals(0f, drop.lineY, 0f)
        // Dedo na trilha de propriedade: é a metade de baixo do grupo, logo
        // abaixo dele — onde o T2 já está. Nunca cai ENTRE a camada e a trilha dela.
        assertEquals(RowDrop.NONE, RowDrop.target(rows, t, 45f, t2, 2).kind)
    }

    @Test fun staleInputIsIgnored() {
        val t1 = clip(1, 1, 0, 90)
        assertEquals(RowDrop.NONE, RowDrop.target(emptyList(), FloatArray(1), 0f, t1, 0).kind)
        assertEquals(RowDrop.NONE, RowDrop.target(listOf(t1), tops(1), Float.NaN, t1, 0).kind)
        assertEquals(RowDrop.NONE, RowDrop.target(listOf(t1), tops(1), 10f, t1, 3).kind)
    }
}
