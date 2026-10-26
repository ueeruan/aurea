package com.aurea.aurea.ui.ds

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Omega fundação (F2): onde o menu ancorado abre. Mesmos casos de
 * `AnchoredMenuPosition` do iOS (AnchoredMenu.swift).
 */
class AnchoredMenuPositionTest {
    private val margin = 8
    private val gap = 4

    @Test fun opensBelowAlignedToTheStartEdge() {
        // Âncora 40 × 40 em (20, 100); janela 400 × 800; menu 250 × 200.
        assertEquals(20, AnchoredMenuPosition.x(20, 60, 400, 250, rtl = false, alignEnd = false, margin = margin))
        assertEquals(144, AnchoredMenuPosition.y(100, 140, 800, 200, margin, gap))
    }

    @Test fun rtlAlignsToTheRightEdgeAndAlignEndFlips() {
        // RTL: o início é a borda direita da âncora.
        assertEquals(360 - 250, AnchoredMenuPosition.x(320, 360, 400, 250, rtl = true, alignEnd = false, margin = margin))
        // LTR com alignEnd: borda direita também.
        assertEquals(360 - 250, AnchoredMenuPosition.x(320, 360, 400, 250, rtl = false, alignEnd = true, margin = margin))
        // RTL com alignEnd: volta para a borda esquerda.
        assertEquals(20, AnchoredMenuPosition.x(20, 60, 400, 250, rtl = true, alignEnd = true, margin = margin))
    }

    @Test fun staysEightFromTheWindowEdges() {
        // Âncora colada à direita: o menu não passa da borda.
        assertEquals(400 - 250 - margin, AnchoredMenuPosition.x(380, 400, 400, 250, rtl = false, alignEnd = false, margin = margin))
        // Âncora colada à esquerda em RTL.
        assertEquals(margin, AnchoredMenuPosition.x(0, 40, 400, 250, rtl = true, alignEnd = false, margin = margin))
    }

    @Test fun anchorNearTheBottomOpensUpwards() {
        // Âncora em 700..740 numa janela de 800: 200 não cabe embaixo.
        assertEquals(700 - gap - 200, AnchoredMenuPosition.y(700, 740, 800, 200, margin, gap))
    }

    @Test fun neitherSideFitsKeepsTheMenuInsideTheWindow() {
        // Menu de 328 numa janela de 400 com a âncora no meio: preso dentro.
        val y = AnchoredMenuPosition.y(180, 220, 400, 328, margin, gap)
        assertEquals(400 - 328 - margin, y)
    }
}
