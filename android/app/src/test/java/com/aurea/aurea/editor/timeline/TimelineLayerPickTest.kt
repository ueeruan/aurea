package com.aurea.aurea.editor.timeline

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

/**
 * Modo "Selecionar várias camadas" (veio do app antigo): o toque no clipe
 * soma/tira da seleção em vez de trocar, e a timeline nunca fica na fileira
 * compacta da doca enquanto ele está ligado.
 */
class TimelineLayerPickTest {
    @Test
    fun `no modo de escolha o toque sempre soma ou tira`() {
        for (compact in listOf(false, true)) for (selected in 0..3) {
            assertEquals(LayerTap.TOGGLE, layerTap(picking = true, compact = compact, selected = selected))
        }
    }

    @Test
    fun `fora do modo vale a regra de sempre`() {
        assertEquals(LayerTap.LEAVE_COMPACT, layerTap(picking = false, compact = true, selected = 1))
        assertEquals(LayerTap.REPLACE, layerTap(picking = false, compact = false, selected = 0))
        assertEquals(LayerTap.REPLACE, layerTap(picking = false, compact = false, selected = 1))
        assertEquals(LayerTap.TOGGLE, layerTap(picking = false, compact = false, selected = 2))
    }

    @Test
    fun `tocar de novo na unica escolhida a solta`() {
        assertEquals(LayerTap.DESELECT, layerTap(picking = false, compact = false, selected = 1, tappedSelected = true))
        // Escolhida só "na mão" da timeline (segurada): o toque abre as opções.
        assertEquals(LayerTap.REPLACE, layerTap(picking = false, compact = false, selected = 1, tappedSelected = true, timelineOnly = true))
        // Outra camada troca direto.
        assertEquals(LayerTap.REPLACE, layerTap(picking = false, compact = false, selected = 1, tappedSelected = false))
        // Compacto continua saindo do painel; lote continua somando/tirando.
        assertEquals(LayerTap.LEAVE_COMPACT, layerTap(picking = false, compact = true, selected = 1, tappedSelected = true))
        assertEquals(LayerTap.TOGGLE, layerTap(picking = false, compact = false, selected = 2, tappedSelected = true))
    }

    @Test
    fun `escolhendo camadas a doca nao compacta a timeline`() {
        // O Timeline.kt passa "escolhendo keyframes OU camadas" no último argumento.
        assertFalse(timelineCompact(panel = false, dock = true, tracksOpen = false, selectingKeys = true))
    }
}
