package com.aurea.aurea.editor

import com.aurea.aurea.ui.theme.LayerType
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Presets de texto (2026-10-03): o "Texto" da barra segue criando o texto
 * direto e os presets de texto ganham uma categoria ao lado dele. Omega painel
 * (área 04): o botão Presets mora no painel da camada (fileira 3) e o texto
 * edita o conteúdo pela vaga "Editar texto".
 */
class TextPresetsEntryTest {
    private fun layer(type: LayerType, text3D: Boolean = false) =
        PanelLayer(id = 1, kind = type.kind, locked = false, start = 0, end = 30, text3D = text3D)

    @Test fun addBarHasTextPresetsRightAfterText() {
        val tabs = AddTab.entries
        assertEquals(tabs.indexOf(AddTab.Text) + 1, tabs.indexOf(AddTab.TextPresets))
    }

    @Test fun textPanelsShowPresetsAndEditTheText() {
        for (l in listOf(layer(LayerType.Text), layer(LayerType.Model3D, text3D = true))) {
            val spec = layerPanelSpec(l)
            assertTrue("presets on ${l.kind}", LayerSlot.Presets in spec.row3)
            assertEquals(LayerEdit.Text, spec.edit)
            assertEquals(listOf(LayerSlot.MoveTransform, LayerSlot.Edit, LayerSlot.Presets, LayerSlot.Effects), spec.row3)
        }
    }
}
