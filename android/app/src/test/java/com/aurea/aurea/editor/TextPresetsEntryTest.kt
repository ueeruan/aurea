package com.aurea.aurea.editor

import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.ui.theme.LayerType
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Presets de texto de volta (2026-10-03): o "Texto" da barra segue criando o
 * texto direto, os presets de texto ganham uma categoria ao lado dele e a
 * ficha "Presets" volta só no texto 2D/3D — vídeo/imagem continuam sem presets.
 */
class TextPresetsEntryTest {
    private fun layer(type: LayerType, text3D: Boolean = false, hasAudio: Boolean = false) = DockLayer(
        id = 1, kind = type.kind, locked = false, start = 0, end = 30, adjustment = false,
        vector = false, hasAudio = hasAudio, muted = false, text3D = text3D, effectCount = 0,
    )

    @Test fun addBarHasTextPresetsRightAfterText() {
        val tabs = AddTab.entries
        assertEquals(tabs.indexOf(AddTab.Text) + 1, tabs.indexOf(AddTab.TextPresets))
    }

    @Test fun textDocksOpenThePresetsPanel() {
        for (l in listOf(layer(LayerType.Text), layer(LayerType.Model3D, text3D = true))) {
            val sections = sectionsFor(l)
            assertTrue("presets on ${l.kind}", DockSection.Presets in sections)
            assertEquals(EditorPanel.Presets, DockSection.Presets.panel)
            // Frequent actions remain visible; all six tools are reachable in one row.
            assertEquals(listOf(DockSection.EditText, DockSection.Move, DockSection.Effects, DockSection.Blend), sections.take(4))
            assertTrue(DockSection.TextOptions in sections)
            assertEquals(listOf(6), dockRows(sections.size))
        }
    }

    @Test fun mediaAndOtherLayersStayWithoutPresets() {
        val others = listOf(
            layer(LayerType.Video), layer(LayerType.Video, hasAudio = true), layer(LayerType.Image),
            layer(LayerType.Shape), layer(LayerType.Audio), layer(LayerType.Model3D), layer(LayerType.Particles),
            layer(LayerType.Group), layer(LayerType.Camera), layer(LayerType.Light), layer(LayerType.Null),
        )
        for (l in others) assertFalse("no presets on ${l.kind}", DockSection.Presets in sectionsFor(l))
    }
}
