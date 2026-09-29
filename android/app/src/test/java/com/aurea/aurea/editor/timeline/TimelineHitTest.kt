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
    fun `a geometria do redesenho`() {
        // Mockup 2026-09-29: riscos 30 + relógio 30 + respiro 8, e a barra 2 dp abaixo do topo da pílula.
        assertEquals(70f, m.rowsTop, 0f)
        // Fileira 32 (pílula 28 + vão 4) / barra 24: as camadas eram grandes demais.
        assertEquals(32f, m.row, 0f)
        assertEquals(24f, m.bar, 0f)
        assertEquals(13f, m.trackTop, 0f)
        // Pílula de 78 colada à esquerda: olho em x 16, quadradinho de 22 de x 32 a 54.
        assertEquals(78f, m.headerColumn, 0f)
        assertEquals(28f, m.pillHeight, 0f)
        assertEquals(2f, m.pillInset, 0f)
        assertEquals(17f, m.gutterIconCx, 0f)
        assertEquals(16f, m.gutterEyeCx, 0f)
        assertEquals(32f, m.glyphBoxLeft, 0f)
        assertEquals(22f, m.glyphBox, 0f)
        assertEquals(3f, m.stripe, 0f)
        assertEquals(3f, m.barRadius, 0f)
        assertEquals(64f, m.playheadTop, 0f)
    }

    @Test
    fun `pilula ganha de tudo - olho no comeco, o resto e a miniatura do tipo`() {
        assertEquals(HitKind.HEADER_EYE, hit(10f, 10f))
        assertEquals(HitKind.HEADER_EYE, hit(20f, 20f))
        assertEquals(HitKind.HEADER, hit(40f, 10f))          // a miniatura do tipo abre/fecha as trilhas
        assertEquals(HitKind.HEADER, hit(70f, 20f))          // ponta arredondada: ainda a pílula
        assertEquals(HitKind.HEADER, hit(50f, 28f))
        assertEquals(HitKind.NONE, hit(90f, 10f))            // fora da pílula e fora do clipe
        assertEquals(HitKind.NONE, hit(250f, -1f))
        assertEquals(HitKind.NONE, hit(250f, 32f))
    }

    @Test
    fun `corpo e vazio`() {
        assertEquals(HitKind.BODY, hit(250f, 10f))
        assertEquals(HitKind.NONE, hit(250f, 30f))          // o vão abaixo da barra é do vazio
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
        assertEquals(HitKind.KEYFRAME, hit(203f, 20f, instants = atStart))   // no desenho, dentro da barra
        assertEquals(0, out[0])
        assertEquals(HitKind.TRIM_START, hit(190f, 20f, instants = atStart)) // fora da barra: alça
        assertEquals(HitKind.TRIM_START, hit(210f, 20f, instants = atStart)) // perto, mas fora do desenho
        assertEquals(HitKind.TRIM_START, hit(203f, 8f, instants = atStart))  // metade de cima: alça
        assertEquals(HitKind.KEYFRAME, hit(190f, 20f, instants = atStart, handles = false))
    }

    @Test
    fun `losango na faixa de baixo ganha do corpo`() {
        val mid = intArrayOf(125)                            // x = 250
        assertEquals(HitKind.KEYFRAME, hit(260f, 20f, instants = mid))
        assertEquals(HitKind.KEYFRAME, hit(250f, 30f, instants = mid))       // abaixo da barra, ainda na linha
        assertEquals(HitKind.BODY, hit(250f, 8f, instants = mid))
        assertEquals(HitKind.BODY, hit(270f, 20f, instants = mid))
        assertEquals(HitKind.BODY, hit(250f, 20f, instants = mid, keys = false))  // lote: o toque é da camada
    }

    @Test
    fun `tampa e setas do compacto`() {
        // Tampa "‹" de 34 na ponta esquerda (200..234) = voltar.
        assertEquals(HitKind.CAP_BACK, hit(205f, 10f, handles = false, compact = true))
        assertEquals(HitKind.CAP_BACK, hit(230f, 10f, handles = false, compact = true))
        // contentRight = 300 − 10 → › em 268..290 e ‹ em 246..268 (juntas na direita).
        assertEquals(HitKind.ARROW_NEXT, hit(285f, 10f, handles = false, compact = true))
        assertEquals(HitKind.ARROW_PREV, hit(255f, 10f, handles = false, compact = true))
        assertEquals(HitKind.BODY, hit(238f, 10f, start = 100, end = 200, handles = false, compact = true))
        // Barra passando da borda direita: a seta › fica à vista (print t2).
        assertEquals(HitKind.ARROW_NEXT, hit(385f, 10f, end = 400, handles = false, compact = true))
        // Ponta esquerda embaixo da pílula: a tampa gruda depois dela (78..112).
        assertEquals(HitKind.CAP_BACK, hit(100f, 10f, start = 30, end = 400, handles = false, compact = true))
        // Clipe curto: só a tampa (sem espaço para as setas, o resto é corpo).
        assertEquals(HitKind.CAP_BACK, hit(210f, 10f, end = 130, handles = false, compact = true))
        assertEquals(HitKind.BODY, hit(250f, 10f, end = 130, handles = false, compact = true))
    }

    @Test
    fun `ponta escondida sob a pilula nao tem alca`() {
        // Início no frame 30 → x = 60, embaixo da pílula (78).
        assertEquals(HitKind.HEADER, hit(70f, 10f, start = 30))
        assertEquals(HitKind.BODY, hit(85f, 10f, start = 30))
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
