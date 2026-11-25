package com.aurea.aurea.editor.panels

import com.aurea.aurea.engine.KeyframeRow
import kotlin.math.abs
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** These handles call the shared native renderer through the Android JNI bridge. */
class GraphSpeedHandlesInstrumentedTest {
    private fun ease(interp: Int, x1: Float = .33f, y1: Float = 0f, x2: Float = .67f, y2: Float = 1f) =
        Ease(interp, x1, y1, x2, y2)

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
            val movedEase = requireNotNull(out.changed(out.frame, base * 1.5f))
            val moved = SpeedHandle(a, b, false, movedEase, base, power)
            assertEquals(base * 1.5f, moved.velocity, base * 1e-3f)
        }
    }
}
