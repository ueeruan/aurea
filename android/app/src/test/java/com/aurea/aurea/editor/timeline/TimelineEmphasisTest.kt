package com.aurea.aurea.editor.timeline

import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.engine.PodLayout
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.ui.theme.LayerType
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.ByteBuffer

/**
 * Acabamentos da timeline (P1-6): losangos do parâmetro em edição fortes e os
 * outros esmaecidos, o sob o cabeçote em destaque, e o fade da fileira nova.
 */
class TimelineEmphasisTest {
    private fun key(p: Int, t: Int, fx: Int = -1, param: Int = 0) = KeyframeRow(p, fx, t, 1f, 1, param)

    private fun layer() = LayerRow(
        id = 7L, kind = LayerType.Image.kind, zIndex = 0, startFrame = 20, endFrame = 100, opacity = 1f,
        flags = PodLayout.FLAG_VISIBLE, effectCount = 0, maskCount = 0, keyframeCount = 0, blendMode = 0,
        parentIndex = -1, offsetFrames = 5, trackId = 3, nameOffset = 0, nameLength = 0, nameBlob = ByteBuffer.allocate(0),
    )

    @Test fun `os outros parametros ficam esmaecidos e sem repetir o instante forte`() {
        assertArrayEquals(intArrayOf(10, 40), KeyEmphasis.dimmed(intArrayOf(10, 25, 40), intArrayOf(25)))
        assertArrayEquals(intArrayOf(), KeyEmphasis.dimmed(intArrayOf(10, 25), intArrayOf(10, 25)))
        assertArrayEquals(intArrayOf(3, 4), KeyEmphasis.dimmed(intArrayOf(3, 4), intArrayOf()))
        assertArrayEquals(intArrayOf(), KeyEmphasis.dimmed(intArrayOf(), intArrayOf(9)))
        assertEquals(160f / 255f, KeyEmphasis.DIM_ALPHA, 1e-6f)
    }

    @Test fun `escolhido vence selecao que vence o cabecote`() {
        assertEquals(KeyEmphasis.Tone.CHOSEN, KeyEmphasis.tone(chosen = true, picked = true, underPlayhead = true))
        assertEquals(KeyEmphasis.Tone.PICKED, KeyEmphasis.tone(chosen = false, picked = true, underPlayhead = true))
        assertEquals(KeyEmphasis.Tone.PLAYHEAD, KeyEmphasis.tone(chosen = false, picked = false, underPlayhead = true))
        assertEquals(KeyEmphasis.Tone.OFF, KeyEmphasis.tone(chosen = false, picked = false, underPlayhead = false))
    }

    @Test fun `painel focado mantem so o parametro dele tocavel e os outros esmaecidos`() {
        val position = key(0, 10)
        val rotation = key(8, 10)
        val scale = key(3, 30)
        val opacity = key(4, 50)
        val all = listOf(position, rotation, scale, opacity)
        val l = layer()
        val full = buildRow(l, all)
        assertArrayEquals(intArrayOf(25, 45, 65), full.instants)
        assertFalse(full.dimInstants.isNotEmpty())

        val focused = buildFocusedRow(l, all, listOf(TrackKey(3)), full.instants)
        // Forte: só a escala (o que se toca e arrasta, como antes).
        assertArrayEquals(intArrayOf(45), focused.instants)
        assertEquals(listOf(scale), focused.keysAt[0])
        // Esmaecidos: posição/rotação (25) e opacidade (65).
        assertArrayEquals(intArrayOf(25, 65), focused.dimInstants)
        assertTrue(focused.hasKeys)
        assertEquals(l.trackId, focused.line)

        // Parâmetro sem keyframe: nada forte, todos esmaecidos — a barra ainda diz que há animação.
        val none = buildFocusedRow(l, all, listOf(TrackKey(12)), full.instants)
        assertArrayEquals(intArrayOf(), none.instants)
        assertArrayEquals(intArrayOf(25, 45, 65), none.dimInstants)
        assertTrue(none.hasKeys)

        // Foco cobre tudo: nada a esmaecer.
        val every = buildFocusedRow(l, all, listOf(TrackKey(0), TrackKey(8), TrackKey(3), TrackKey(4)), full.instants)
        assertArrayEquals(intArrayOf(), every.dimInstants)
    }

    @Test fun `um keyframe do Mover X+Y e um losango so, parcial empilha`() {
        // 0.3.6 keys itens 1 e 3: ◆ do Mover grava X e Y no mesmo quadro.
        val l = layer()
        val all = listOf(key(0, 10), key(1, 10), key(0, 30))
        val row = buildFocusedRow(l, all, listOf(TrackKey(0), TrackKey(1)), buildRow(l, all).instants)
        assertArrayEquals(intArrayOf(25, 45), row.instants)
        assertEquals(2, row.keysAt[0].size)
        // X e Y no instante: UM losango (antes: dois sobrepostos).
        assertFalse(KeyEmphasis.stacked(focusCount = 2, distinctKeyed = 2))
        // Só X: parcial, empilhado.
        assertEquals(1, row.keysAt[1].size)
        assertTrue(KeyEmphasis.stacked(focusCount = 2, distinctKeyed = 1))
        // Sem painel ou com UMA trilha em foco: nunca empilha.
        assertFalse(KeyEmphasis.stacked(focusCount = 0, distinctKeyed = 2))
        assertFalse(KeyEmphasis.stacked(focusCount = 1, distinctKeyed = 1))
        // 3D (X, Y, Z): os três = um; dois de três = parcial.
        assertFalse(KeyEmphasis.stacked(focusCount = 3, distinctKeyed = 3))
        assertTrue(KeyEmphasis.stacked(focusCount = 3, distinctKeyed = 2))
    }

    @Test fun `fade decelera de 0 a 1 em 600 ms`() {
        assertEquals(0f, RowFade.alpha(0), 0f)
        assertEquals(0.75f, RowFade.alpha(300), 1e-6f)
        assertEquals(1f, RowFade.alpha(600), 0f)
        assertEquals(1f, RowFade.alpha(5_000), 0f)
        var last = -1f
        for (ms in 0L..600L step 50L) {
            val a = RowFade.alpha(ms)
            assertTrue(a >= last)
            last = a
        }
    }

    @Test fun `so nasce camada nova da mesma composicao`() {
        // Primeira leitura (abrir projeto): nada anima.
        assertArrayEquals(longArrayOf(), RowFade.born(null, longArrayOf(1, 2, 3)))
        // Camada adicionada.
        assertArrayEquals(longArrayOf(9), RowFade.born(longArrayOf(1, 2, 3), longArrayOf(1, 2, 3, 9)))
        // Apagar não anima nada.
        assertArrayEquals(longArrayOf(), RowFade.born(longArrayOf(1, 2, 3), longArrayOf(1, 3)))
        // Lista trocada inteira (entrar num grupo): nada anima.
        assertArrayEquals(longArrayOf(), RowFade.born(longArrayOf(1, 2, 3), longArrayOf(7, 8)))
        // Primeira camada num projeto vazio anima; uma leva inteira não.
        assertArrayEquals(longArrayOf(4), RowFade.born(longArrayOf(), longArrayOf(4)))
        assertArrayEquals(longArrayOf(), RowFade.born(longArrayOf(), longArrayOf(4, 5)))
    }
}
