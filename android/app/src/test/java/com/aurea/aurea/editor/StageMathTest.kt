package com.aurea.aurea.editor

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class StageMathTest {
    private val eps = 1e-3f

    @Test
    fun unionBoxHugsEveryCorner() {
        val a = floatArrayOf(10f, 10f, 30f, 10f, 30f, 40f, 10f, 40f)
        val b = floatArrayOf(-5f, 20f, 15f, 20f, 15f, 60f, -5f, 60f)
        assertArrayEquals(floatArrayOf(-5f, 10f, 30f, 60f), StageMath.unionBox(listOf(a, b)), eps)
    }

    @Test
    fun unionBoxIgnoresNonFiniteAndEmpty() {
        assertNull(StageMath.unionBox(emptyList()))
        assertNull(StageMath.unionBox(listOf(floatArrayOf(Float.NaN, 1f))))
        val box = StageMath.unionBox(listOf(floatArrayOf(Float.NaN, 0f, 2f, 3f, 4f, 5f)))
        assertArrayEquals(floatArrayOf(2f, 3f, 4f, 5f), box, eps)
    }

    @Test
    fun moveWithoutParentIsPlainDelta() {
        val out = FloatArray(2)
        StageMath.moveInParent(floatArrayOf(1f, 0f, 0f, 1f, 0f, 0f), 100f, 50f, 12f, -7f, out)
        assertArrayEquals(floatArrayOf(112f, 43f), out, eps)
    }

    @Test
    fun moveThroughScaledRotatedParentLandsSameWorldDelta() {
        // Pai girado 90° e com escala 2, deslocado (tx 300, ty 100).
        val a = floatArrayOf(0f, 2f, -2f, 0f, 300f, 100f)
        val out = FloatArray(2)
        StageMath.moveInParent(a, 10f, 20f, 40f, 6f, out)
        val w0x = a[0] * 10f + a[2] * 20f + a[4]
        val w0y = a[1] * 10f + a[3] * 20f + a[5]
        val w1x = a[0] * out[0] + a[2] * out[1] + a[4]
        val w1y = a[1] * out[0] + a[3] * out[1] + a[5]
        assertEquals(40f, w1x - w0x, eps)
        assertEquals(6f, w1y - w0y, eps)
    }

    @Test
    fun moveWithDegenerateParentFallsBackToWorld() {
        val out = FloatArray(2)
        StageMath.moveInParent(floatArrayOf(0f, 0f, 0f, 0f, 5f, 5f), 1f, 1f, 2f, 3f, out)
        assertArrayEquals(floatArrayOf(7f, 8f), out, eps)
    }

    @Test
    fun moveRootsDropsLayersCarriedByASelectedAncestor() {
        // 3 → 2 → 1 (1 é o avô); 5 sozinho; 4 tem pai 9 (fora da seleção) cujo pai é 1.
        val parent = mapOf(3L to 2L, 2L to 1L, 1L to 0L, 5L to 0L, 4L to 9L, 9L to 1L)
        val roots = StageMath.moveRoots(listOf(3L, 1L, 5L, 4L)) { parent[it] ?: 0L }
        assertEquals(listOf(1L, 5L), roots)
    }

    @Test
    fun moveRootsKeepsChildWhenParentNotMoving() {
        val parent = mapOf(3L to 2L, 2L to 0L)
        assertEquals(listOf(3L), StageMath.moveRoots(listOf(3L)) { parent[it] ?: 0L })
    }

    @Test
    fun moveRootsSurvivesParentCycle() {
        val parent = mapOf(1L to 2L, 2L to 1L)
        assertEquals(listOf(1L), StageMath.moveRoots(listOf(1L)) { parent[it] ?: 0L })
    }

    @Test
    fun rotationSnapsNearMultiplesOf45() {
        val s = { v: Float, cur: Float -> StageMath.snapStep(v, 45f, cur, 4f, 6f) }
        assertEquals(90f, s(87.5f, Float.NaN), eps)
        assertEquals(-45f, s(-48f, Float.NaN), eps)
        assertEquals(0f, s(3.9f, Float.NaN), eps)
        assertTrue(s(20f, Float.NaN).isNaN())
        assertTrue(s(84.5f, Float.NaN).isNaN())
    }

    @Test
    fun rotationSnapHasHysteresis() {
        val s = { v: Float, cur: Float -> StageMath.snapStep(v, 45f, cur, 4f, 6f) }
        // Preso em 90: continua até 6° de distância, solta além.
        assertEquals(90f, s(95.5f, 90f), eps)
        assertTrue(s(96.5f, 90f).isNaN())
        // Solto a 5° do alvo: não entra (precisa chegar a menos de 4°).
        assertTrue(s(95f, Float.NaN).isNaN())
    }

    @Test
    fun scaleSnapsTo100Percent() {
        val s = { v: Float, cur: Float -> StageMath.snapTarget(v, 1f, cur, 0.03f, 0.045f) }
        assertEquals(1f, s(1.02f, Float.NaN), eps)
        assertEquals(1f, s(0.975f, Float.NaN), eps)
        assertTrue(s(1.04f, Float.NaN).isNaN())
        assertEquals(1f, s(1.04f, 1f), eps)
        assertTrue(s(1.05f, 1f).isNaN())
    }

    @Test
    fun snapEnteredOnlyOnEntryOrTargetChange() {
        assertTrue(StageMath.snapEntered(Float.NaN, 45f))
        assertFalse(StageMath.snapEntered(45f, 45f))
        assertFalse(StageMath.snapEntered(45f, Float.NaN))
        assertTrue(StageMath.snapEntered(45f, 90f))
    }
}
