package com.aurea.aurea.editor.timeline

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Arrasto de losango (beta "é difícil mover o keyframe"): alvo de 48 dp, passo
 * por frame com histerese e o cabeçote da origem sem ímã. Espelho Swift:
 * `KeyDrag` / `TimelineHit.test` no TimelineModel.swift/TimelineView.swift.
 */
class KeyDragTest {
    private val m = TimelineMetrics(1f)
    private val out = IntArray(1)

    @Test
    fun `de perto o frame troca no meio do frame mais a histerese e nao treme na fronteira`() {
        val ppf = 20f // 20 px por frame: histerese de 3 px = 0,15 frame
        assertEquals(10, KeyDrag.quantize(10.0, 10, ppf, 3f))
        assertEquals(10, KeyDrag.quantize(10.6, 10, ppf, 3f))
        assertEquals(11, KeyDrag.quantize(10.7, 10, ppf, 3f))
        // Voltando um pouco, fica no 11 até passar o mesmo limiar para o outro lado.
        assertEquals(11, KeyDrag.quantize(10.4, 11, ppf, 3f))
        assertEquals(10, KeyDrag.quantize(10.3, 11, ppf, 3f))
        // Para trás também.
        assertEquals(9, KeyDrag.quantize(9.3, 10, ppf, 3f))
        assertEquals(10, KeyDrag.quantize(9.4, 10, ppf, 3f))
    }

    @Test
    fun `de longe um px anda varios frames e nada fica preso`() {
        val ppf = 0.25f // 4 frames por px: a histerese vira 0,25 no máximo
        assertEquals(10, KeyDrag.quantize(10.7, 10, ppf, 3f))
        assertEquals(14, KeyDrag.quantize(14.0, 10, ppf, 3f))
        assertEquals(6, KeyDrag.quantize(6.0, 10, ppf, 3f))
        // Cada px de dedo (4 frames) sempre move o losango.
        var current = 10
        for (px in 1..20) {
            val next = KeyDrag.quantize(10.0 + px / ppf, current, ppf, 3f)
            assertEquals(10 + px * 4, next)
            current = next
        }
    }

    @Test
    fun `valores fora do contrato nao movem nem estouram`() {
        assertEquals(7, KeyDrag.quantize(Double.NaN, 7, 10f, 3f))
        assertEquals(7, KeyDrag.quantize(Double.POSITIVE_INFINITY, 7, 10f, 3f))
        assertEquals(Int.MAX_VALUE, KeyDrag.quantize(1e12, 0, 10f, 3f))
        assertEquals(Int.MIN_VALUE, KeyDrag.quantize(-1e12, 0, 10f, 3f))
        assertEquals(8, KeyDrag.quantize(8.0, 7, 0f, 3f))
    }

    @Test
    fun `o cabecote no instante de origem nao segura o losango`() {
        assertEquals(Snap.NONE, KeyDrag.playheadMagnet(30, 30))
        assertEquals(45, KeyDrag.playheadMagnet(45, 30))
        // Com o ímã desligado na origem, os primeiros px já andam (antes: preso por 8 dp).
        val ppf = 2f
        val tol = (m.snapKey / ppf).toDouble()
        val desired = 30.0 + 3.0 / ppf // 3 px de dedo
        assertEquals(30, Snap.nearest(IntArray(0), desired, 30, tol))           // o ímã antigo prendia
        assertEquals(Snap.NONE, Snap.nearest(IntArray(0), desired, KeyDrag.playheadMagnet(30, 30), tol))
        assertEquals(32, KeyDrag.quantize(desired, 30, ppf, m.keyDragHysteresis))
    }

    @Test
    fun `alvo do losango tem 48 dp e cede as alcas e a tampa`() {
        assertEquals(24f, m.keyHitHalf, 0f)
        assertEquals(48f, m.keyHitHalf * 2, 0f)
        // Losango no meio do clipe (x = 250): 23 px de cada lado ainda pegam.
        val mid = intArrayOf(125)
        assertEquals(HitKind.KEYFRAME, hit(227f, 13f, mid))
        assertEquals(HitKind.KEYFRAME, hit(273f, 13f, mid))
        assertEquals(HitKind.BODY, hit(276f, 13f, mid))
        // Losango na ponta (x = 200): o núcleo (14) ganha da alça; a folga cede a ela.
        val atStart = intArrayOf(100)
        assertEquals(HitKind.KEYFRAME, hit(212f, 13f, atStart))   // núcleo dentro da alça
        assertEquals(HitKind.TRIM_START, hit(185f, 13f, atStart)) // folga sobre a alça → alça
        assertEquals(HitKind.KEYFRAME, hit(185f, 13f, atStart, handles = false))
        assertEquals(HitKind.KEYFRAME, hit(178f, 13f, atStart))   // fora da alça, ainda na folga
        // Compacto: a tampa ‹ continua alcançável sob a folga.
        assertEquals(HitKind.CAP_BACK, hit(180f, 10f, atStart, handles = false, compact = true))
        assertEquals(HitKind.KEYFRAME, hit(190f, 10f, atStart, handles = false, compact = true))
        // Vizinhos a 20 px: cada toque vai para o mais perto.
        val pair = intArrayOf(120, 130) // x = 240 e 260
        assertEquals(HitKind.KEYFRAME, hit(248f, 13f, pair)); assertEquals(0, out[0])
        assertEquals(HitKind.KEYFRAME, hit(252f, 13f, pair)); assertEquals(1, out[0])
        // Lote de camadas: o losango não pega o dedo (nem pela folga).
        assertEquals(HitKind.BODY, hit(270f, 13f, mid, keys = false))
    }

    private fun hit(
        x: Float, y: Float, instants: IntArray,
        handles: Boolean = true, compact: Boolean = false, keys: Boolean = true,
    ): HitKind {
        val x0 = TimeAxis.xOf(100.0, VIEW, PPF, CX)
        val x1 = maxOf(TimeAxis.xOf(150.0, VIEW, PPF, CX), x0 + m.barMinWidth)
        return RowHit.hit(m, x, y, WIDTH, x0, x1, handles, compact, keys, instants, VIEW, PPF, CX, out)
    }

    private companion object {
        const val VIEW = 100.0
        const val PPF = 2f
        const val CX = 200f
        const val WIDTH = 400f
    }
}
