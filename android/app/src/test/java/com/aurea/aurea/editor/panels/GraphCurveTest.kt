package com.aurea.aurea.editor.panels

import com.aurea.aurea.engine.KeyframeRow
import org.junit.Assert.*
import org.junit.Test
import kotlin.math.abs

/** O traço do gráfico é a conta do motor (`Track::sample_keys` + `apply_easing`). */
class GraphCurveTest {
    private fun ease(interp: Int, x1: Float = .33f, y1: Float = 0f, x2: Float = .67f, y2: Float = 1f) = Ease(interp, x1, y1, x2, y2)
    private val keys = listOf(
        CurveKey(0, 0f, ease(Interp.LINEAR)),
        CurveKey(10, 100f, ease(Interp.EASE_IN)),
        CurveKey(20, 0f, ease(Interp.BEZIER, .42f, 0f, .58f, 1f)),
        CurveKey(30, 50f, ease(Interp.HOLD)),
        CurveKey(40, 10f, ease(Interp.BOUNCE)),
        CurveKey(50, 90f, ease(Interp.ELASTIC)),
        CurveKey(60, 20f, ease(Interp.STEPS)),
        CurveKey(70, 80f, ease(Interp.EASE_IN_OUT)),
        CurveKey(83, -40f, ease(Interp.LINEAR)),
    )

    @Test fun sampleMatchesTheEngineFormulaAtFrames() {
        assertEquals(0f, sampleCurve(keys, -5.0), 0f)          // antes: o valor da primeira
        assertEquals(50f, sampleCurve(keys, 5.0), 1e-5f)       // linear
        assertEquals(75f, sampleCurve(keys, 15.0), 1e-4f)      // t² de 100 → 0
        assertEquals(50f, sampleCurve(keys, 39.0), 0f)         // manter até o fim do trecho
        assertEquals(10f, sampleCurve(keys, 40.0), 0f)         // e salta NA marca seguinte
        assertEquals(50f, sampleCurve(keys, 65.0), 1e-5f)      // degraus: floor(.5·4)/4
        assertEquals(-40f, sampleCurve(keys, 200.0), 0f)       // depois: o valor da última
        // Bézier: mesmo float que cubic_bezier (Math.hpp) no meio do trecho.
        assertEquals(0f + (50f - 0f) * cubicBezier(.42f, 0f, .58f, 1f, .3f), sampleCurve(keys, 23.0), 1e-5f)
    }

    @Test fun drawnCurvePassesThroughEveryRenderedFrame() {
        for (speed in listOf(false, true)) {
            val drawn = graphCurve(keys, -3.0, 90.0, 3.0, speed, 30f)
            for (frame in -3..90) {
                val want = if (speed) sampleVelocity(keys, frame.toDouble(), 30f) else sampleCurve(keys, frame.toDouble())
                val at = drawnAt(drawn, frame.toFloat())
                assertTrue("frame $frame is drawn (speed=$speed)", at.isNotEmpty())
                assertTrue("frame $frame: $at vs $want", at.any { sameValue(it, want) })
            }
            // Em x, o traço nunca volta (degraus são verticais, não laços).
            drawn.zipWithNext().forEach { (a, b) -> assertTrue(b.frame >= a.frame) }
        }
    }

    /** O que o traço desenha no x [frame]: os vértices ali e os segmentos que o cruzam. */
    private fun drawnAt(drawn: List<GraphSample>, frame: Float): List<Float> {
        val out = drawn.filter { it.frame == frame }.map { it.value }.toMutableList()
        drawn.zipWithNext().forEach { (a, b) ->
            if (a.frame < frame && frame < b.frame) out += a.value + (b.value - a.value) * (frame - a.frame) / (b.frame - a.frame)
        }
        return out
    }

    @Test fun easedSegmentsHaveAVertexOnEveryFrame() {
        val drawn = graphCurve(keys, 0.0, 30.0, .5, false, 30f)
        for (frame in 0..30) assertTrue(drawn.any { it.frame == frame.toFloat() && it.value == sampleCurve(keys, frame.toDouble()) })
    }

    @Test fun holdAndStepsAreDrawnAsStepsNotRamps() {
        val drawn = graphCurve(keys, 30.0, 70.0, 4.0, false, 30f)
        // Manter: tudo entre 30 e 40 fica em 50, com o salto vertical em 40.
        assertTrue(drawn.filter { it.frame > 30f && it.frame < 40f }.all { it.value == 50f })
        assertEquals(setOf(50f, 10f), drawn.filter { it.frame == 40f }.map { it.value }.toSet())
        // Degraus: só quatro alturas entre 60 e 70 (fora a chegada em 70).
        val heights = drawn.filter { it.frame >= 60f && it.frame < 70f }.map { it.value }.toSet()
        assertEquals(setOf(20f, 35f, 50f, 65f), heights)
    }

    @Test fun speedCurveTouchesTheSpeedHandles() {
        val a = KeyframeRow(0, -1, 0, 0f, Interp.BEZIER, 0)
        val b = KeyframeRow(0, -1, 24, 120f, Interp.LINEAR, 0)
        val h = floatArrayOf(.25f, .6f, .7f, .9f)
        val curve = listOf(CurveKey(0, 0f, ease(Interp.BEZIER, h[0], h[1], h[2], h[3])), CurveKey(24, 120f, ease(Interp.LINEAR)))
        val base = 120f * 30f / 24f
        val out = SpeedHandle(a, b, false, h, base)
        val inn = SpeedHandle(a, b, true, h, base)
        val start = sampleVelocity(curve, 0.0, 30f)
        val end = sampleVelocity(curve, 23.9999, 30f)
        assertTrue("start $start vs handle ${out.velocity}", abs(start - out.velocity) <= abs(out.velocity) * .01f)
        assertTrue("end $end vs handle ${inn.velocity}", abs(end - inn.velocity) <= abs(inn.velocity) * .01f)
        // Linear: a velocidade é a do trecho inteiro, constante.
        val linear = listOf(CurveKey(0, 0f, ease(Interp.LINEAR)), CurveKey(10, 100f, ease(Interp.LINEAR)))
        for (f in 0 until 10) assertEquals(300f, sampleVelocity(linear, f + .5, 30f), .05f)
    }

    @Test fun speedHandlesFollowTheCurvePower() {
        // Força ×2/×3: a alça encosta na curva de velocidade e arrastá-la até
        // um valor volta com esse valor (a potência é desfeita na alça).
        val a = KeyframeRow(0, -1, 0, 0f, Interp.BEZIER, 0)
        val b = KeyframeRow(0, -1, 24, 120f, Interp.LINEAR, 0)
        val h = floatArrayOf(.25f, .6f, .7f, .9f)
        val base = 120f * 30f / 24f
        for (power in 2..3) {
            val curve = listOf(CurveKey(0, 0f, Ease(Interp.BEZIER, h[0], h[1], h[2], h[3], power)), CurveKey(24, 120f, ease(Interp.LINEAR)))
            val out = SpeedHandle(a, b, false, h, base, power)
            val inn = SpeedHandle(a, b, true, h, base, power)
            val start = sampleVelocity(curve, 0.0, 30f)
            val end = sampleVelocity(curve, 23.9999, 30f)
            assertTrue("×$power start $start vs ${out.velocity}", abs(start - out.velocity) <= abs(out.velocity) * .02f)
            assertTrue("×$power end $end vs ${inn.velocity}", abs(end - inn.velocity) <= abs(inn.velocity) * .02f)
            val moved = SpeedHandle(a, b, false, out.changed(out.frame, base * 1.5f), base, power)
            assertEquals(base * 1.5f, moved.velocity, base * 1e-3f)
        }
    }

    @Test fun graphFollowsTheLayerAndThePlayheadSegment() {
        fun k(p: Int, t: Int, v: Float) = KeyframeRow(p, -1, t, v, Interp.LINEAR, 0)
        val keys = listOf(k(0, 0, 0f), k(0, 20, 10f), k(1, 0, 5f), k(1, 20, 9f), k(1, 40, 1f), k(10 + 3, 0, 1f), k(13, 9, 1f))
        // A mesma trilha, se anima nesta camada; senão uma irmã; senão a primeira animada.
        assertEquals(1, graphTrackFor(keys, k(1, 99, 0f)).first().property)
        assertEquals(0, graphTrackFor(keys, k(2, 0, 0f)).first().property)
        assertEquals(0, graphTrackFor(keys, null).first().property)
        assertTrue(graphTrackFor(listOf(k(0, 0, 1f)), null).isEmpty())
        // O grupo inteiro (X e Y da posição) aparece junto.
        assertEquals(listOf(0, 1), graphGroup(keys, graphTrackFor(keys, k(1, 0, 0f))).map { it[0].property })
        val track = keys.filter { it.property == 1 }
        assertEquals(0, segmentIndexAt(track, -5))
        assertEquals(1, segmentIndexAt(track, 25))
        assertEquals(1, segmentIndexAt(track, 40))   // na última marca: o trecho que chega nela
        assertEquals(1, segmentIndexOf(track, 40))
        assertEquals(0, segmentIndexOf(track, 0))
    }
}
