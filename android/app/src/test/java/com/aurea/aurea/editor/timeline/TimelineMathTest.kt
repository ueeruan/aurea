package com.aurea.aurea.editor.timeline

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.roundToLong

class TimelineMathTest {

    // --- Tempo ↔ px ---------------------------------------------------------------
    @Test
    fun `o frame sob o cabecote fica no centro e a conta ida e volta fecha`() {
        assertEquals(200f, TimeAxis.xOf(100.0, 100.0, 2.5f, 200f), 1e-4f)
        val x = TimeAxis.xOf(137.5, 100.0, 2.5f, 200f)
        assertEquals(137.5, TimeAxis.frameAt(x, 100.0, 2.5f, 200f), 1e-4)
        // Arrastar para a direita traz o passado: à esquerda do centro é tempo menor.
        assertTrue(TimeAxis.frameAt(150f, 100.0, 2.5f, 200f) < 100.0)
    }

    @Test
    fun `px por frame e vista limitada a composicao`() {
        assertEquals(7f, TimeAxis.pxPerFrame(80f, 2.625f, 30f), 1e-4f)
        assertEquals(TimeAxis.pxPerFrame(80f, 1f, 30f), TimeAxis.pxPerFrame(80f, 1f, 0f), 0f) // fps inválido = 30
        assertEquals(0.0, TimeAxis.clampView(-5.0, 100), 0.0)
        assertEquals(99.0, TimeAxis.clampView(150.0, 100), 0.0)
        assertEquals(0.0, TimeAxis.clampView(10.0, 0), 0.0)
    }

    @Test
    fun `pinca ancorada mantem o instante sob os dedos`() {
        val cx = 200f
        val focusX = 320f
        val f = TimeAxis.frameAt(focusX, 90.0, 2f, cx)
        val v2 = Zoom.anchoredView(f, focusX, cx, 5f)
        assertEquals(f, TimeAxis.frameAt(focusX, v2, 5f, cx), 1e-6)
    }

    @Test
    fun `auto zoom da A01`() {
        assertEquals(379f / 60f, Zoom.autoFit(379f, 60f), 1e-4f)
        assertEquals(80f, Zoom.autoFit(379f, 1f), 0f)
        assertEquals(4f, Zoom.autoFit(379f, 1000f), 0f)
        assertEquals(800f, Zoom.clamp(5000f), 0f)
        assertEquals(2f, Zoom.clamp(0.1f), 0f)
    }

    // --- Régua ------------------------------------------------------------------------
    @Test
    fun `no zoom da A01 a regua e o print - segundo forte e 10 finos sem rotulo`() {
        val s = RulerSteps.of(80f, 30f)
        assertEquals(1.0, s.majorSeconds, 0.0)
        assertEquals(10, s.subdivisions)
        assertFalse(s.frameMinors)
        assertFalse(s.labels)
    }

    @Test
    fun `a regua se adapta ao zoom`() {
        assertTrue(RulerSteps.of(800f, 30f).frameMinors)                 // quadro a 26,7 dp
        RulerSteps.of(30f, 30f).let {
            assertEquals(2.0, it.majorSeconds, 0.0)
            assertEquals(4, it.subdivisions)
            assertTrue(it.labels)
        }
        RulerSteps.of(2f, 30f).let {
            assertEquals(30.0, it.majorSeconds, 0.0)
            assertEquals(6, it.subdivisions)
        }
        assertEquals(10, RulerSteps.of(40f, 30f).subdivisions)
    }

    // --- Relógio ----------------------------------------------------------------------
    @Test
    fun `relogio MM SS FF da A01 e hora a partir de 1 h`() {
        assertEquals("00:00:00", Timecode.format(0, 30f))
        assertEquals("00:00:09", Timecode.format(9, 30f))
        assertEquals("01:05:05", Timecode.format(30 * 65 + 5, 30f))
        assertEquals("1:01:01:02", Timecode.format(30 * 3661 + 2, 30f))
        assertEquals("00:00:00", Timecode.format(-3, 30f))
    }

    @Test
    fun `relogio com fps fracionario conta o quadro dentro do segundo`() {
        assertEquals("00:00:29", Timecode.format(29, 29.97f))
        assertEquals("00:01:00", Timecode.format(30, 29.97f))
        assertEquals("00:01:29", Timecode.format(59, 29.97f))
        assertEquals("00:02:00", Timecode.format(60, 29.97f))
    }

    @Test
    fun `rotulos da regua`() {
        assertEquals("1:05", Timecode.rulerLabel(65))
        assertEquals("1:02:05", Timecode.rulerLabel(3725))
    }

    // --- Ímã ----------------------------------------------------------------------------
    @Test
    fun `ima gruda no alvo mais perto dentro da tolerancia`() {
        val t = intArrayOf(0, 30, 90)
        assertEquals(30, Snap.nearest(t, 33.0, Snap.NONE, 5.0))
        assertEquals(Snap.NONE, Snap.nearest(t, 50.0, Snap.NONE, 5.0))
        assertEquals(52, Snap.nearest(t, 50.0, 52, 5.0))          // o cabeçote disputa junto
        assertEquals(90, Snap.nearest(t, 88.0, 70, 5.0))
        assertEquals(Snap.NONE, Snap.nearest(IntArray(0), 3.0, Snap.NONE, 5.0))
    }

    @Test
    fun `ima de intervalo gruda pelo inicio ou pelo fim`() {
        val t = intArrayOf(0, 100)
        val out = IntArray(2)
        Snap.span(t, 97, 20, Snap.NONE, 6.0, out)
        assertArrayEquals(intArrayOf(100, 100), out)
        Snap.span(t, 75, 20, Snap.NONE, 6.0, out)                 // o fim (95) gruda em 100
        assertArrayEquals(intArrayOf(80, 100), out)
        Snap.span(t, 40, 20, Snap.NONE, 6.0, out)
        assertArrayEquals(intArrayOf(40, Snap.NONE), out)
    }

    @Test
    fun `alvos ordenados sem repeticao`() {
        assertArrayEquals(intArrayOf(1, 3, 5), Snap.sortedDistinct(intArrayOf(5, 1, 5, 3, 9), 4))
        assertEquals(0, Snap.sortedDistinct(IntArray(4), 0).size)
    }

    // --- Inércia e auto-rolagem ------------------------------------------------------------
    @Test
    fun `inercia do scrub percorre metade da velocidade`() {
        assertEquals(0f, Friction.offset(1000f, 0f), 1e-3f)
        assertEquals(1000f / 2.0025f, Friction.offset(1000f, 10f), 1f)
        assertEquals(135f, Friction.velocity(1000f, 1f), 1e-2f)
        assertEquals(-Friction.offset(800f, 0.3f), Friction.offset(-800f, 0.3f), 1e-4f)
    }

    @Test
    fun `auto rolagem so para o lado a que o dedo foi`() {
        assertEquals(-1, AutoScroll.direction(50f, 200f, 104f, 400f, 4f))
        assertEquals(0, AutoScroll.direction(50f, 52f, 104f, 400f, 4f))   // pegou já na borda
        assertEquals(1, AutoScroll.direction(410f, 300f, 104f, 400f, 4f))
        assertEquals(0, AutoScroll.direction(200f, 100f, 104f, 400f, 4f))
    }

    // --- Keyframes ---------------------------------------------------------------------------
    @Test
    fun `losango nunca encosta no vizinho nem sai da camada`() {
        val inst = intArrayOf(10, 20, 40)
        val out = IntArray(2)
        Keyframes.dragLimits(inst, 1, 0, 100, out)
        assertArrayEquals(intArrayOf(11, 39), out)
        Keyframes.dragLimits(inst, 0, 0, 100, out)
        assertArrayEquals(intArrayOf(0, 19), out)
        Keyframes.dragLimits(inst, 2, 0, 100, out)
        assertArrayEquals(intArrayOf(21, 100), out)
        // Já fora da camada (depois de um trim): não pula para dentro sozinho.
        Keyframes.dragLimits(intArrayOf(-5), 0, 0, 50, out)
        assertArrayEquals(intArrayOf(-5, 50), out)
    }

    @Test
    fun `busca de instantes`() {
        val inst = intArrayOf(10, 20, 40)
        assertEquals(1, Keyframes.nearestIndex(inst, 26.0))
        assertEquals(2, Keyframes.nearestIndex(inst, 31.0))
        assertEquals(-1, Keyframes.nearestIndex(IntArray(0), 3.0))
        assertEquals(1, Keyframes.firstAtOrAfter(inst, 15.5))
        assertEquals(2, Keyframes.firstAtOrAfter(inst, 40.0))
        assertEquals(3, Keyframes.firstAtOrAfter(inst, 41.0))
        assertEquals(25, Keyframes.toTimeline(10, 20, 5))
        assertEquals(10, Keyframes.toLocal(25, 20, 5))
    }

    // --- Miniaturas ---------------------------------------------------------------------------
    @Test
    fun `o frame pedido cai no mesmo balde de 250 ms do motor`() {
        for (fps in floatArrayOf(24f, 25f, 29.97f, 30f, 60f)) {
            for (b in 0..400) {
                val local = Thumbs.requestLocalFrame(b, fps)
                assertEquals("fps $fps balde $b", b, Thumbs.bucketOf(local.toDouble(), fps))
                // A conta do motor (`ThumbnailService`): us = llround(local·1e6/fps); balde = us / 250000.
                val us = (local * 1e6 / fps).roundToLong()
                assertEquals("motor fps $fps balde $b", b.toLong(), us / 250_000)
            }
        }
        assertEquals(0, Thumbs.bucketOf(7.4, 30f))
        assertEquals(1, Thumbs.bucketOf(7.5, 30f))
    }

    // --- Reordenar ------------------------------------------------------------------------------
    @Test
    fun `destino do reordenar e traco`() {
        assertEquals(2, Reorder.targetIndex(38f + 46f * 2 + 5f, 38f, 0f, 46f, 5))
        assertEquals(0, Reorder.targetIndex(0f, 38f, 0f, 46f, 5))
        assertEquals(4, Reorder.targetIndex(2000f, 38f, 0f, 46f, 5))
        assertEquals(3, Reorder.targetIndex(38f + 46f * 2 + 5f, 38f, 46f, 46f, 5))
        assertEquals(38f + 46f, Reorder.dropLineY(3, 1, 38f, 0f, 46f), 0f)       // sobe: acima do destino
        assertEquals(38f + 4 * 46f, Reorder.dropLineY(1, 3, 38f, 0f, 46f), 0f)   // desce: abaixo do destino
        assertTrue(Reorder.dropLineY(2, 2, 38f, 0f, 46f).isNaN())
    }
}
