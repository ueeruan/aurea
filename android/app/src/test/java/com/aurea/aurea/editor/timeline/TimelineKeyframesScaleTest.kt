package com.aurea.aurea.editor.timeline

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.random.Random

/**
 * 10.000 keyframes numa linha (fase 8D): a lista de desenho dos losangos
 * (culling + agrupamento) e o hit-test, por quadro, em vários zooms.
 */
class TimelineKeyframesScaleTest {
    private val m = TimelineMetrics(3f)
    private val width = 1080f
    private val cx = width / 2f
    private val fps = 30f

    /** O algoritmo de antes (marca a marca), como referência. */
    private fun reference(inst: IntArray, view: Double, ppf: Float, out: IntArray): Int {
        var i = Keyframes.firstAtOrAfter(inst, TimeAxis.frameAt(-m.keyTouchHalf, view, ppf, cx))
        var n = 0
        while (i < inst.size) {
            val kx = TimeAxis.xOf(inst[i].toDouble(), view, ppf, cx)
            if (kx > width + m.keyTouchHalf) break
            var j = i
            var lastX = kx
            while (j + 1 < inst.size) {
                val nx = TimeAxis.xOf(inst[j + 1].toDouble(), view, ppf, cx)
                if (nx - lastX >= m.keyMergeGap) break
                j++
                lastX = nx
            }
            out[2 * n] = i
            out[2 * n + 1] = j
            n++
            i = j + 1
        }
        return n
    }

    private fun keys(count: Int, seed: Int): IntArray {
        val r = Random(seed)
        var t = 0
        return IntArray(count) { t += 1 + r.nextInt(6); t }
    }

    @Test
    fun `mesmos grupos que o algoritmo de antes na parte visivel`() {
        val inst = keys(10_000, 1)
        val a = IntArray(40_000)
        val b = IntArray(40_000)
        for (pps in floatArrayOf(2f, 8f, 40f, 80f, 300f, 800f)) {
            val ppf = TimeAxis.pxPerFrame(pps, m.density, fps)
            for (view in doubleArrayOf(0.0, 500.0, 17_000.0, 34_000.0)) {
                val na = Keyframes.visibleGroups(inst, view, ppf, cx, width, m.keyTouchHalf, m.keyMergeGap, a)
                val nb = reference(inst, view, ppf, b)
                assertEquals("pps $pps vista $view", nb, na)
                for (g in 0 until na) {
                    assertEquals(b[2 * g], a[2 * g])
                    // Só o fim do ÚLTIMO grupo pode parar antes: na borda da tela (o resto está fora).
                    if (b[2 * g + 1] != a[2 * g + 1]) {
                        val lastX = TimeAxis.xOf(inst[a[2 * g + 1]].toDouble(), view, ppf, cx)
                        assertTrue(g == na - 1 && lastX > width + m.keyTouchHalf)
                    }
                }
            }
        }
    }

    @Test
    fun `custo por quadro com 10000 keyframes`() {
        val inst = keys(10_000, 2)
        val out = IntArray(4096)
        val big = IntArray(40_000)
        val hitOut = IntArray(1)
        for (pps in floatArrayOf(2f, 80f, 800f)) {
            val ppf = TimeAxis.pxPerFrame(pps, m.density, fps)
            // Vista andando 1 quadro por desenho (tocando).
            fun run(block: (Double) -> Unit): Double {
                repeat(2000) { block(15_000.0 + it) }
                val t0 = System.nanoTime()
                for (it in 0 until 2000) block(15_000.0 + it)
                return (System.nanoTime() - t0) / 2000 / 1000.0
            }
            val before = run { v -> reference(inst, v, ppf, big) }
            val after = run { v -> Keyframes.visibleGroups(inst, v, ppf, cx, width, m.keyTouchHalf, m.keyMergeGap, out) }
            val hit = run { v ->
                RowHit.hit(m, 500f, m.keyTouchTop + 1f, width, 0f, width, true, false, true, inst, v, ppf, cx, hitOut)
            }
            val groups = Keyframes.visibleGroups(inst, 15_000.0, ppf, cx, width, m.keyTouchHalf, m.keyMergeGap, out)
            println(
                "BENCH losangos 10000 kf, %4.0f dp/s: lista de desenho antes %8.2f us, depois %6.2f us (%d grupos); hit-test %5.2f us"
                    .format(pps, before, after, groups, hit),
            )
        }
    }
}
