package com.aurea.aurea.editor.timeline

import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Hit-test com densidade 1 (px = dp). Linha de exemplo: vista no frame 100,
 * 2 px por frame, centro em 200 e largura 400 — um clipe de 100 a 150 ocupa
 * x 200..300.
 */
class TimelineHitTest {
    private val m = TimelineMetrics(1f)
    private val out = IntArray(1)

    private fun hit(
        x: Float,
        y: Float,
        start: Int = 100,
        end: Int = 150,
        instants: IntArray = IntArray(0),
        handles: Boolean = true,
        compact: Boolean = false,
        keys: Boolean = true,
    ): HitKind {
        val x0 = TimeAxis.xOf(start.toDouble(), VIEW, PPF, CX)
        val x1 = maxOf(TimeAxis.xOf(end.toDouble(), VIEW, PPF, CX), x0 + m.barMinWidth)
        return RowHit.hit(m, x, y, WIDTH, x0, x1, handles, compact, keys, instants, VIEW, PPF, CX, out)
    }

    @Test
    fun `a geometria da A01`() {
        assertEquals(38f, m.rowsTop, 0f)
        // Linha 36 / barra 30 (mais baixas que a A.01, 46/36: cabem mais camadas).
        assertEquals(36f, m.row, 0f)
        assertEquals(30f, m.bar, 0f)
        assertEquals(19f, m.trackTop, 0f)
        // Calha de 40 (sai a pílula de 58): glifo do tipo em (17, 13), olho pequeno no canto de baixo (31, 25).
        assertEquals(40f, m.headerColumn, 0f)
        assertEquals(17f, m.gutterIconCx, 0f)
        assertEquals(31f, m.gutterEyeCx, 0f)
        assertEquals(25f, m.gutterEyeCy, 0f)
        assertEquals(3f, m.stripe, 0f)
    }

    @Test
    fun `calha ganha de tudo - olho no canto de baixo, o resto e o glifo do tipo`() {
        assertEquals(HitKind.HEADER_EYE, hit(30f, 20f))
        assertEquals(HitKind.HEADER_EYE, hit(38f, 32f))
        assertEquals(HitKind.HEADER, hit(12f, 20f))          // o glifo do tipo abre/fecha as trilhas
        assertEquals(HitKind.HEADER, hit(30f, 8f))           // alto da calha: ainda o glifo
        assertEquals(HitKind.HEADER, hit(12f, 32f))
        assertEquals(HitKind.NONE, hit(50f, 20f))            // fora da calha e fora do clipe
        assertEquals(HitKind.NONE, hit(250f, -1f))
        assertEquals(HitKind.NONE, hit(250f, 46f))
    }

    @Test
    fun `corpo e vazio`() {
        assertEquals(HitKind.BODY, hit(250f, 10f))
        assertEquals(HitKind.NONE, hit(250f, 42f))          // os 10 dp abaixo da barra são do vazio
        assertEquals(HitKind.NONE, hit(350f, 10f))
    }

    @Test
    fun `alcas dentro das pontas com folga para fora`() {
        assertEquals(HitKind.TRIM_START, hit(190f, 10f))
        assertEquals(HitKind.TRIM_START, hit(205f, 10f))
        assertEquals(HitKind.TRIM_END, hit(295f, 10f))
        assertEquals(HitKind.TRIM_END, hit(310f, 10f))
        assertEquals(HitKind.NONE, hit(320f, 10f))
        assertEquals(HitKind.NONE, hit(190f, 10f, handles = false))
        assertEquals(HitKind.BODY, hit(205f, 10f, handles = false))
    }

    @Test
    fun `keyframe em 0 nao rouba mais a alca de inicio - bug 10_2`() {
        val atStart = intArrayOf(100)                        // losango em x = 200, na ponta
        assertEquals(HitKind.KEYFRAME, hit(203f, 30f, instants = atStart))   // no desenho, dentro da barra
        assertEquals(0, out[0])
        assertEquals(HitKind.TRIM_START, hit(190f, 30f, instants = atStart)) // fora da barra: alça
        assertEquals(HitKind.TRIM_START, hit(210f, 30f, instants = atStart)) // perto, mas fora do desenho
        assertEquals(HitKind.TRIM_START, hit(203f, 10f, instants = atStart)) // metade de cima: alça
        assertEquals(HitKind.KEYFRAME, hit(190f, 30f, instants = atStart, handles = false))
    }

    @Test
    fun `losango na faixa de baixo ganha do corpo`() {
        val mid = intArrayOf(125)                            // x = 250
        assertEquals(HitKind.KEYFRAME, hit(260f, 30f, instants = mid))
        assertEquals(HitKind.KEYFRAME, hit(250f, 34f, instants = mid))       // abaixo da barra, ainda na linha
        assertEquals(HitKind.BODY, hit(250f, 10f, instants = mid))
        assertEquals(HitKind.BODY, hit(270f, 30f, instants = mid))
        assertEquals(HitKind.BODY, hit(250f, 30f, instants = mid, keys = false))  // lote: o toque é da camada
    }

    @Test
    fun `setas do compacto`() {
        // contentLeft = 200 + 10 → ‹ em 210..232; contentRight = 300 − 10 → › em 268..290.
        assertEquals(HitKind.ARROW_PREV, hit(220f, 10f, handles = false, compact = true))
        assertEquals(HitKind.ARROW_NEXT, hit(285f, 10f, handles = false, compact = true))
        assertEquals(HitKind.BODY, hit(250f, 10f, handles = false, compact = true))
        // Barra passando da borda direita: a seta › fica à vista (print t2).
        assertEquals(HitKind.ARROW_NEXT, hit(385f, 10f, end = 400, handles = false, compact = true))
    }

    @Test
    fun `ponta escondida sob a calha nao tem alca`() {
        // Início no frame 15 → x = 30, embaixo da calha (40).
        assertEquals(HitKind.HEADER, hit(36f, 10f, start = 15))
        assertEquals(HitKind.BODY, hit(45f, 10f, start = 15))
    }

    @Test
    fun `clipe curto divide as zonas no meio`() {
        // 100..105 → 200..210, alargado ao mínimo de 40 → 200..240, meio 220.
        assertEquals(HitKind.TRIM_START, hit(205f, 10f, end = 105))
        assertEquals(HitKind.BODY, hit(215f, 10f, end = 105))
        assertEquals(HitKind.TRIM_END, hit(230f, 10f, end = 105))
    }

    private companion object {
        const val VIEW = 100.0
        const val PPF = 2f
        const val CX = 200f
        const val WIDTH = 400f
    }
}
