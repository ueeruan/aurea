package com.aurea.aurea.editor.timeline

import com.aurea.aurea.engine.KeyframeRow
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Seleção de keyframes em VÁRIAS camadas (veio do app antigo): losangos de
 * outra fileira somam à seleção, e mover desloca todas pelo mesmo delta,
 * cada camada presa ao próprio clipe.
 */
class TimelineCrossLayerKeysTest {
    private val a10 = KeyframeRow(0, -1, 10, 1f, 1, 0)
    private val a40 = KeyframeRow(0, -1, 40, 2f, 1, 0)
    private val b5 = KeyframeRow(3, -1, 5, 1f, 1, 0)
    private val b20 = KeyframeRow(1, -1, 20, 1f, 1, 0)

    @Test fun otherLayerKeysJoinWithoutReplacingThePrimary() {
        var sel = KeySelection(1).toggle(a10)
        sel = sel.toggleGroupOn(2, listOf(b5, b20))
        assertEquals(1L, sel.layer)
        assertEquals(3, sel.size)
        assertTrue(sel.crossLayer)
        assertEquals(listOf(1L, 2L), sel.layers())
        assertTrue(sel.containsAnyOn(2, listOf(b20)))
        assertFalse(sel.containsAnyOn(1, listOf(b20)))
        // Tocar de novo no mesmo grupo tira; a camada vazia sai do mapa.
        sel = sel.toggleGroupOn(2, listOf(b5, b20))
        assertFalse(sel.crossLayer)
        assertEquals(1, sel.size)
        assertSame(sel, sel.toggleGroupOn(2, emptyList()))
    }

    @Test fun shiftMovesEveryLayerByTheSameDelta() {
        val sel = KeySelection(1).toggle(a10).toggleGroupOn(2, listOf(b5)).shifted(3)
        assertEquals(setOf(KeyRef(0, -1, 0, 13)), sel.keys)
        assertEquals(setOf(KeyRef(3, -1, 0, 8)), sel.on(2))
        assertArrayEquals(longArrayOf(3, -1, 0, 8), sel.references(2))
    }

    @Test fun limitsAreTheTightestOfAllLayers() {
        // Camada 1: clipe 0..100, sem deslocamento; keyframes em 10 e 40 → pode andar −10..+60.
        // Camada 2: clipe 50..80, deslocamento 0; keyframe local 5 = timeline 55 → −5..+25.
        val sel = KeySelection(1).toggle(a10).toggle(a40).toggleGroupOn(2, listOf(b5))
        val clips = mapOf(1L to intArrayOf(0, 100, 0), 2L to intArrayOf(50, 80, 0))
        assertArrayEquals(intArrayOf(-5, 25), sel.shiftLimits { clips[it] })
        // Só a principal: os limites dela.
        assertArrayEquals(intArrayOf(-10, 60), KeySelection(1).toggle(a10).toggle(a40).shiftLimits { clips[it] })
    }

    @Test fun validationDropsTheWholeSelectionWhenAnyLayerLostAKey() {
        val sel = KeySelection(1).toggle(a10).toggleGroupOn(2, listOf(b5))
        val keys = mapOf(1L to listOf(a10, a40), 2L to listOf(b5))
        assertSame(sel, sel.validatedAll { keys[it] })
        assertNull(sel.validatedAll { if (it == 2L) emptyList() else keys[it] })
        assertNull(sel.validatedAll { if (it == 2L) null else keys[it] })
    }
}
