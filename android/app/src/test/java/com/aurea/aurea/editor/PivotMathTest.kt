package com.aurea.aurea.editor

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

/**
 * Arrastar o pivô não pode mexer na imagem: todo ponto da camada cai no MESMO
 * lugar antes e depois (posição += R·S·Δâncora), no 2D e no 3D.
 */
class PivotMathTest {
    private val corners = listOf(
        floatArrayOf(0f, 0f, 0f), floatArrayOf(1920f, 0f, 0f), floatArrayOf(1920f, 1080f, 0f),
        floatArrayOf(0f, 1080f, 0f), floatArrayOf(640f, 210f, 0f), floatArrayOf(10f, 900f, 35f),
    )

    private fun assertStill(
        pos: FloatArray, rot: FloatArray, scale: FloatArray, anchor: FloatArray,
        threeD: Boolean, dx: Float, dy: Float, depthFollowsWidth: Boolean = true,
    ) {
        val da = PivotMath.anchorDelta(dx, dy, rot, scale, threeD, depthFollowsWidth)
        assertNotNull(da)
        val anchor2 = FloatArray(3) { anchor[it] + da!![it] }
        val pos2 = floatArrayOf(pos[0] + dx, pos[1] + dy, pos[2])
        for (p in corners) {
            val before = PivotMath.place(pos, rot, scale, anchor, threeD, depthFollowsWidth, p)
            val after = PivotMath.place(pos2, rot, scale, anchor2, threeD, depthFollowsWidth, p)
            for (i in 0..2) assertEquals("ponto ${p.toList()} eixo $i", before[i], after[i], 0.02f)
        }
        // O pivô (a nova âncora) cai exatamente onde o dedo levou a posição.
        val pivot = PivotMath.place(pos2, rot, scale, anchor2, threeD, depthFollowsWidth, anchor2)
        assertEquals(pos2[0], pivot[0], 0.01f)
        assertEquals(pos2[1], pivot[1], 0.01f)
    }

    @Test fun plainLayerKeepsItsPixelsWhenThePivotMoves() {
        assertStill(floatArrayOf(960f, 540f, 0f), floatArrayOf(0f, 0f, 0f), floatArrayOf(1f, 1f, 1f),
            floatArrayOf(960f, 540f, 0f), threeD = false, dx = -310f, dy = -420f)
    }

    @Test fun rotatedScaledMirroredLayerKeepsItsPixels() {
        assertStill(floatArrayOf(700f, 300f, 0f), floatArrayOf(0f, 0f, 37f), floatArrayOf(0.35f, -1.8f, 1f),
            floatArrayOf(9f, 9f, 0f), threeD = false, dx = 123f, dy = -48f)
        // 2D ignora giro X/Y guardado (o motor usa só Rz no 2D).
        assertStill(floatArrayOf(700f, 300f, 0f), floatArrayOf(25f, -60f, 200f), floatArrayOf(2f, 0.5f, 1f),
            floatArrayOf(100f, 40f, 0f), threeD = false, dx = -12f, dy = 77f)
    }

    @Test fun threeDLayerKeepsItsPixelsWithFullEulerRotation() {
        assertStill(floatArrayOf(400f, 250f, -120f), floatArrayOf(30f, -45f, 70f), floatArrayOf(1.5f, 0.75f, 2f),
            floatArrayOf(12f, -8f, 5f), threeD = true, dx = 90f, dy = -35f)
        // Câmera/luz: Z não acompanha a largura.
        assertStill(floatArrayOf(0f, 0f, 500f), floatArrayOf(-80f, 15f, 5f), floatArrayOf(3f, 1f, 0.5f),
            floatArrayOf(0f, 0f, 0f), threeD = true, dx = -200f, dy = 60f, depthFollowsWidth = false)
    }

    @Test fun rotationMatchesTheEngineZOrderIn2D() {
        // Ponto 100 px à direita da âncora, giro de 90° (y para baixo): vai para baixo.
        val p = PivotMath.place(floatArrayOf(0f, 0f, 0f), floatArrayOf(0f, 0f, 90f), floatArrayOf(1f, 1f, 1f),
            floatArrayOf(0f, 0f, 0f), false, true, floatArrayOf(100f, 0f, 0f))
        assertEquals(0f, p[0], 0.001f)
        assertEquals(100f, p[1], 0.001f)
    }

    @Test fun flattenedLayerRefusesInsteadOfExploding() {
        assertNull(PivotMath.anchorDelta(10f, 0f, floatArrayOf(0f, 0f, 0f), floatArrayOf(0f, 1f, 1f), false, true))
    }
}
