package com.aurea.aurea.editor.panels

import com.aurea.aurea.engine.EffectParam
import com.aurea.aurea.engine.ParamType
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Seções, dicas, alça de ponto e deslizar para apagar do cartão de efeito
 * (P1-4, P1-5, P2-9). A ordem da tela é a mesma do iOS (`fxEffectLayout`).
 */
class EffectSectionsTest {

    private fun slot(
        index: Int,
        type: Int = ParamType.FLOAT,
        section: Int = 0,
        toggle: Int = -1,
        open: Boolean = true,
        flags: Int = ParamType.FLAG_ANIMATABLE,
    ) = ParamSlot(
        index, type, flags, 0f, 1f, "p$index", "", emptyList(),
        section = section, sectionId = if (section > 0) "s$section" else "", sectionLabel = if (section > 0) "Seção $section" else "",
        sectionToggle = toggle, sectionOpen = open,
    )

    @Test
    fun withoutSectionsTheCardKeepsPrincipalAndAdvanced() {
        // Efeito sem seção e sem tabela: até 6 linhas tudo à vista (o de sempre).
        val visible = (0 until 4).map { slot(it) }
        val layout = effectLayout(0x1234, visible)
        assertEquals(visible, layout.main)
        assertTrue(layout.advanced.isEmpty())
        assertTrue(layout.sections.isEmpty())
    }

    @Test
    fun sectionsComeAfterLooseRowsInOrdinalOrderWithParamsByIndex() {
        // Índices fora de ordem dentro da seção; seção 2 declarada "antes" na lista.
        val visible = listOf(
            slot(0, ParamType.BOOL, section = 1, toggle = 0),
            slot(5, section = 2, open = false),
            slot(3, section = 1, toggle = 0),
            slot(1, section = 1, toggle = 0),
            slot(2),
            slot(4, section = 2, open = false),
            slot(6),
        )
        val layout = effectLayout(0x1234, visible)
        assertEquals(listOf(2, 6), layout.main.map { it.index })
        assertTrue(layout.advanced.isEmpty())
        assertEquals(listOf(1, 2), layout.sections.map { it.ordinal })
        val first = layout.sections[0]
        assertEquals(0, first.toggle?.index)                     // o interruptor sai das linhas
        assertEquals(listOf(1, 3), first.params.map { it.index })
        assertTrue(first.open)
        assertEquals("Seção 1", first.label)
        val second = layout.sections[1]
        assertNull(second.toggle)
        assertFalse(second.open)
        assertEquals(listOf(4, 5), second.params.map { it.index })
        // Ordem da tela: soltas, interruptor + linhas da 1, linhas da 2.
        assertEquals(listOf(2, 6, 0, 1, 3, 4, 5), layout.all.map { it.index })
        // Entrar por um parâmetro da seção recolhida abre a seção 2.
        assertEquals(2, closedSectionOf(layout, 5))
        assertEquals(0, closedSectionOf(layout, 3))
        assertEquals(0, closedSectionOf(layout, 2))
    }

    @Test
    fun sectionWithOnlyItsToggleOrHiddenMembersIsNotShown() {
        // Membros ocultos já saíram de `visible`: sobrou só o interruptor.
        val layout = effectLayout(0x1234, listOf(slot(0, ParamType.BOOL, section = 1, toggle = 0), slot(1)))
        assertTrue(layout.sections.isEmpty())
        assertEquals(listOf(1), layout.main.map { it.index })
        // Interruptor que não é Bool não vira interruptor (fica como linha).
        val odd = effectLayout(0x1234, listOf(slot(0, ParamType.FLOAT, section = 1, toggle = 0), slot(1, section = 1, toggle = 0)))
        assertNull(odd.sections.single().toggle)
        assertEquals(listOf(1), odd.sections.single().params.map { it.index })
    }

    @Test
    fun onlyRelativePointsGetAStageHandle() {
        val rel = ParamType.FLAG_ANIMATABLE or ParamType.FLAG_RELATIVE
        assertTrue(stagePointEligible(ParamType.POINT2D, rel))
        assertTrue(stagePointEligible(ParamType.POINT3D, rel))
        // Deslocamento em px / escala em %: não é posição.
        assertFalse(stagePointEligible(ParamType.POINT2D, ParamType.FLAG_ANIMATABLE or ParamType.FLAG_PIXELS))
        assertFalse(stagePointEligible(ParamType.POINT2D, ParamType.FLAG_PERCENT))
        assertFalse(stagePointEligible(ParamType.FLOAT, rel))
        assertFalse(stagePointEligible(ParamType.POINT2D, rel or ParamType.FLAG_HIDDEN))
    }

    @Test
    fun swipeDeletesOnlyLeftPastTheThreshold() {
        val width = 400f
        val min = 96f
        assertFalse(fxSwipeCommits(-100f, width, min))       // 25 %: volta
        assertTrue(fxSwipeCommits(-140f, width, min))        // 35 %: apaga
        assertFalse(fxSwipeCommits(200f, width, min))        // para a direita: nada
        // Cartão estreito: nunca menos que o mínimo em px.
        assertFalse(fxSwipeCommits(-60f, 100f, min))
        assertTrue(fxSwipeCommits(-96f, 100f, min))
    }

    @Test
    fun bridgeRowCarriesSectionToggleStateAndTip() {
        val rows = ByteBuffer.allocate(EffectParam.ROW_BYTES).order(ByteOrder.nativeOrder())
        val text = "glowBrilho externoDica".toByteArray(Charsets.UTF_8)
        val blob = ByteBuffer.allocateDirect(text.size).order(ByteOrder.nativeOrder()).put(text)
        blob.flip()
        rows.putInt(0, 2)
        rows.putInt(4, ParamType.FLOAT)
        rows.putFloat(20, 1f)
        rows.putInt(100, 1)                 // seção 1
        rows.putInt(104, 0)                 // interruptor = parâmetro 0
        rows.putInt(108, EffectParam.SECTION_OPEN)
        rows.putInt(112, 0); rows.putInt(116, 4)
        rows.putInt(120, 4); rows.putInt(124, 14)
        rows.putInt(128, 18); rows.putInt(132, 4)
        val p = EffectParam.read(rows, 0, blob)
        assertEquals(1, p.section)
        assertEquals(0, p.sectionToggle)
        assertTrue(p.sectionOpen)
        assertEquals("glow", p.sectionId)
        assertEquals("Brilho externo", p.sectionLabel)
        assertEquals("Dica", p.tip)
        val s = ParamSlot.of(p)
        assertEquals(1, s.section)
        assertEquals("Dica", s.tip)
        // Linha antiga (zerada): sem seção, sem dica.
        val zero = EffectParam.read(ByteBuffer.allocate(EffectParam.ROW_BYTES).order(ByteOrder.nativeOrder()), 0, blob)
        assertEquals(0, zero.section)
        assertEquals("", zero.tip)
    }
}
