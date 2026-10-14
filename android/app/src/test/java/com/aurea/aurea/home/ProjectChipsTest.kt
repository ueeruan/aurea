package com.aurea.aurea.home

import com.aurea.aurea.state.ProjectEntry
import org.junit.Assert.assertEquals
import org.junit.Test

/** Chips de metadados da linha do projeto na Home (spec am-ui P2-8). */
class ProjectChipsTest {
    private fun entry(w: Int, h: Int, fps: Float) = ProjectEntry("/p/a.aurea", "A", 1L, w, h, fps, 300, null)

    @Test fun `quatro chips na ordem fps proporcao resolucao tamanho`() {
        val chips = projectChips(entry(1080, 1920, 30f))
        assertEquals(listOf(ProjectChipKind.Fps, ProjectChipKind.Aspect, ProjectChipKind.Resolution, ProjectChipKind.Size), chips.map { it.kind })
        assertEquals(listOf("30 fps", "9:16", "Full HD 1080p", "1080 × 1920"), chips.map { it.text })
    }

    @Test fun `sem medida gravada so o fps aparece e NaN nao derruba`() {
        assertEquals(listOf("30 fps"), projectChips(entry(0, 0, Float.NaN)).map { it.text })
        assertEquals(listOf("29.97 fps"), projectChips(entry(0, 1080, 29.97f)).map { it.text })
    }

    @Test fun `proporcao fora dos presets nao vira 16 por 9`() {
        assertEquals("16:9", aspectLabel(3840, 2160))
        assertEquals("4:5", aspectLabel(1080, 1350))
        assertEquals("64:27", aspectLabel(2560, 1080))
        assertEquals("2.39:1", aspectLabel(1920, 803))
        assertEquals("1:2.39", aspectLabel(803, 1920))
        assertEquals("3:2", aspectLabel(1500, 1000))
    }
}
