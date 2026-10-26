package com.aurea.aurea.editor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 0.3.3 barra: a pílula de 5 categorias, a grade 4 × 2 do "+" (na ordem do
 * desenho do dono) e a vaga que acende para cada destino aberto. O iOS tem a
 * mesma tabela (`AddBarCatalog` em AddPanelView.swift).
 */
class AddBar033Test {
    @Test fun pillHasTheFiveCategoriesInOrder() {
        assertEquals(listOf(AddTab.Shape, AddTab.Media, AddTab.Audio, AddTab.Text, AddTab.Element), ADD_BAR_PILL)
    }

    @Test fun gridIsFourByTwoInReadingOrder() {
        assertEquals(8, ADD_BAR_GRID.size)
        assertEquals(listOf(AddTab.Shape, AddTab.Media, AddTab.Audio, AddTab.Text), ADD_BAR_GRID.take(4))
        assertEquals(listOf(AddTab.TextPresets, AddTab.Element, AddTab.Model3D, AddTab.Draw), ADD_BAR_GRID.drop(4))
    }

    @Test fun everyDestinationLightsASlotOfThePill() {
        for (tab in AddTab.entries) assertTrue("$tab lights a pill slot", addBarSlot(tab) in ADD_BAR_PILL)
        for (tab in ADD_BAR_PILL) assertEquals(tab, addBarSlot(tab))
        assertEquals(AddTab.Text, addBarSlot(AddTab.TextPresets))
        assertEquals(AddTab.Element, addBarSlot(AddTab.Model3D))
        assertEquals(AddTab.Element, addBarSlot(AddTab.Vector))
    }

    @Test fun everyDestinationIsReachable() {
        // Vetor abre pelo Elemento (add.element.vector); o resto pela pílula ou pela grade.
        val reachable = (ADD_BAR_PILL + ADD_BAR_GRID + AddTab.Vector).toSet()
        assertEquals(AddTab.entries.toSet(), reachable)
    }

    @Test fun plusGridFitsTheSmallestBottomZone() {
        assertEquals(174f, ADD_BAR_GRID_HEIGHT, 0.01f)
        assertTrue("the + grid fits the smallest zone of the divider", ADD_BAR_GRID_HEIGHT <= EditorLayout.ZONE_MIN)
    }
}
