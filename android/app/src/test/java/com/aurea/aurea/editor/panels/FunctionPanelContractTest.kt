package com.aurea.aurea.editor.panels

import com.aurea.aurea.editor.ProjectSettingsTab
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * F11 (13/11/2026): o contrato das telas de função (FUNCTIONS_IA.md, regras R2 e R7) em
 * conta pura — no máximo 4 abas por tela, e dividir um painel não pode perder aba.
 */
class FunctionPanelContractTest {
    @Test fun frameShowsAtMostFourTabs() {
        assertEquals(4, PanelFrameDefaults.MAX_TABS)
        assertTrue(ProjectSettingsTab.entries.size <= PanelFrameDefaults.MAX_TABS)
    }

    @Test fun every3DSubjectFitsTheFrameAndNoTabIsLost() {
        val subjects = Face3D.entries.filter { it != Face3D.All }
        for (face in subjects) {
            val tabs = checkNotNull(face.tabs) { "$face sem abas" }
            assertTrue("$face tem ${tabs.size} abas", tabs.size in 1..PanelFrameDefaults.MAX_TABS)
        }
        // Material · Objeto 3D · Rig · Cena 3D juntos cobrem as 7 abas antigas.
        assertEquals(Tab3D.entries.toSet(), subjects.flatMap { it.tabs.orEmpty() }.toSet())
        // E nenhuma aba está em dois assuntos (um endereço por função, regra R1).
        assertEquals(Tab3D.entries.size, subjects.sumOf { it.tabs.orEmpty().size })
    }

    /** 13/11/2026: o modelo importado tem UM painel simples — Material · Textura · Animação · Sombra. */
    @Test fun importedModelPanelExposesPartsAndAnimation() {
        assertEquals(setOf(Tab3D.Material, Tab3D.Parts, Tab3D.Texture, Tab3D.Anim, Tab3D.Shadow), Face3D.ModelTabs)
        assertTrue(Face3D.Material.tabsFor(true)!!.size <= PanelFrameDefaults.MAX_TABS)
        assertTrue(Face3D.Object.tabsFor(true)!!.size <= PanelFrameDefaults.MAX_TABS)
        // Cor e preenchimento e Editar abrem o mesmo painel do modelo; Rig e Cena 3D não mudam.
        assertEquals(setOf(Tab3D.Material, Tab3D.Texture, Tab3D.Shadow), Face3D.Material.tabsFor(true))
        assertEquals(setOf(Tab3D.Parts, Tab3D.Anim), Face3D.Object.tabsFor(true))
        assertEquals(Face3D.Rig.tabs, Face3D.Rig.tabsFor(true))
        assertEquals(Face3D.Scene.tabs, Face3D.Scene.tabsFor(true))
        // As partes (a aba Mover saiu) moram na Textura do modelo.
        assertTrue(Tab3D.Parts in Face3D.Object.tabsFor(true)!!)
        // Outros objetos 3D: as abas da F6 de sempre.
        assertEquals(Face3D.Material.tabs, Face3D.Material.tabsFor(false))
    }

    @Test fun vectorPanelIsSplitInThreeSubjects() {
        assertEquals(listOf(VectorFace.Fill, VectorFace.Stroke, VectorFace.Paths), VectorFace.entries.toList())
    }
}
