package com.aurea.aurea.editor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 0.3.3 barra: a pílula de 5 categorias, as categorias extras do "+" e a vaga que acende para cada destino aberto. O iOS tem a
 * mesma tabela (`AddBarCatalog` em AddPanelView.swift).
 */
class AddBar033Test {
    @Test fun pillHasTheFiveCategoriesInOrder() {
        assertEquals(listOf(AddTab.Shape, AddTab.Media, AddTab.Audio, AddTab.Text, AddTab.Element), ADD_BAR_PILL)
    }

    @Test fun plusOffersOnlyCategoriesAbsentFromThePill() {
        assertEquals(listOf(AddTab.TextPresets, AddTab.Model3D, AddTab.Draw, AddTab.Vector), ADD_BAR_GRID)
        assertTrue(ADD_BAR_PILL.intersect(ADD_BAR_GRID.toSet()).isEmpty())
    }

    @Test fun everyDestinationLightsASlotOfThePill() {
        for (tab in AddTab.entries) assertTrue("$tab lights a pill slot", addBarSlot(tab) in ADD_BAR_PILL)
        for (tab in ADD_BAR_PILL) assertEquals(tab, addBarSlot(tab))
        assertEquals(AddTab.Text, addBarSlot(AddTab.TextPresets))
        assertEquals(AddTab.Element, addBarSlot(AddTab.Model3D))
        assertEquals(AddTab.Element, addBarSlot(AddTab.Vector))
    }

    @Test fun everyDestinationIsReachable() {
        val reachable = (ADD_BAR_PILL + ADD_BAR_GRID).toSet()
        assertEquals(AddTab.entries.toSet(), reachable)
    }

    @Test fun plusGridFitsTheSmallestBottomZone() {
        assertEquals(180f, ADD_BAR_GRID_HEIGHT, 0.01f)
        assertTrue("the + grid fits the smallest zone of the divider", ADD_BAR_GRID_HEIGHT <= EditorLayout.ZONE_MIN)
    }
}
