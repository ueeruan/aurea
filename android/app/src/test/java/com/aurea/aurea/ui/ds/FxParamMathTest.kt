package com.aurea.aurea.ui.ds

import com.aurea.aurea.engine.ParamControl
import com.aurea.aurea.engine.ParamType
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs

/**
 * Omega efeitos (área 07): a conta dos controles da referência — slider com
 * pista de encaixe, régua (1 risco = 1 passo), alturas, casas pelo passo,
 * voltas de ângulo, regra de abrir o efeito novo, guia e trackball. Mesma conta
 * do `FxParamMath` do iOS (EffectsHuman.swift).
 */
class FxParamMathTest {

    @Test
    fun rowHeightsFollowTheReference() {
        assertEquals(51f, FxParamMath.rowHeight(ParamControl.SLIDER, ParamType.FLOAT))
        assertEquals(57f, FxParamMath.rowHeight(ParamControl.RULER, ParamType.FLOAT))
        assertEquals(57f, FxParamMath.rowHeight(ParamControl.POINT, ParamType.POINT2D))
        assertEquals(57f, FxParamMath.rowHeight(ParamControl.XYZ, ParamType.POINT3D))
        assertEquals(38f, FxParamMath.rowHeight(ParamControl.AUTO, ParamType.BOOL))
        assertEquals(38f, FxParamMath.rowHeight(ParamControl.AUTO, ParamType.ENUM))
        // Linha antiga (sem controle) num número: régua.
        assertEquals(57f, FxParamMath.rowHeight(ParamControl.AUTO, ParamType.FLOAT))
        assertEquals(ParamControl.RULER, FxParamMath.controlFor(ParamControl.AUTO, ParamType.ANGLE))
        assertEquals(ParamControl.POINT, FxParamMath.controlFor(ParamControl.AUTO, ParamType.POINT2D))
        assertEquals(ParamControl.XYZ, FxParamMath.controlFor(ParamControl.AUTO, ParamType.POINT3D))
        assertEquals(ParamControl.SLIDER, FxParamMath.controlFor(ParamControl.SLIDER, ParamType.FLOAT))
        assertEquals(ParamControl.AUTO, FxParamMath.controlFor(ParamControl.AUTO, ParamType.COLOR))
    }

    @Test
    fun sliderIsAbsoluteQuantizedAndHoldsInTheSnapZone() {
        // Trilho de 210 px, faixa 0..100, encaixe no 50 com 10 px de pista: 200 px úteis.
        val w = 210f
        assertEquals(0f, FxParamMath.sliderValueAt(0f, w, 0f, 100f, 1f, listOf(50f), 10f))
        assertEquals(100f, FxParamMath.sliderValueAt(w, w, 0f, 100f, 1f, listOf(50f), 10f))
        assertEquals(25f, FxParamMath.sliderValueAt(50f, w, 0f, 100f, 1f, listOf(50f), 10f))
        // Dentro da pista (100..110 px) o valor fica parado no encaixe.
        assertEquals(50f, FxParamMath.sliderValueAt(100f, w, 0f, 100f, 1f, listOf(50f), 10f))
        assertEquals(50f, FxParamMath.sliderValueAt(109f, w, 0f, 100f, 1f, listOf(50f), 10f))
        // Depois da pista, desconta os 10 px.
        assertEquals(75f, FxParamMath.sliderValueAt(160f, w, 0f, 100f, 1f, listOf(50f), 10f))
        // Quantizado no passo.
        assertEquals(30f, FxParamMath.sliderValueAt(61f, 200f, 0f, 100f, 10f, emptyList(), 10f))
        // O inverso: o polegar do encaixe fica no meio da pista.
        assertEquals(105f, FxParamMath.sliderX(50f, w, 0f, 100f, listOf(50f), 10f))
        assertEquals(160f, FxParamMath.sliderX(75f, w, 0f, 100f, listOf(50f), 10f))
        // Encaixe fora da faixa não ocupa pista.
        assertEquals(50f, FxParamMath.sliderValueAt(100f, 200f, 0f, 100f, 0f, listOf(-5f, 100f), 10f))
    }

    @Test
    fun rulerTickIsOneStepAndSnapsAndLimits() {
        // 11 px por risco, passo 0,5: andar 22 px = +1,0.
        assertEquals(3f, FxParamMath.rulerValueAfter(2f, 22f, 11f, 0.5f), 1e-6f)
        assertEquals(1.5f, FxParamMath.rulerValueAfter(2f, -11f, 11f, 0.5f), 1e-6f)
        assertEquals(2f, FxParamMath.rulerValueAfter(2f, 22f, 0f, 0.5f))
        assertEquals(1.5f, FxParamMath.snapToTick(1.6f, 0.5f), 1e-6f)
        assertEquals(-2f, FxParamMath.snapToTick(-1.8f, 1f), 1e-6f)
        assertEquals(10f, FxParamMath.limitRange(12f, 0f, 10f))
        assertEquals(0f, FxParamMath.limitRange(-3f, 10f, 0f))
        assertTrue(FxParamMath.crossedSnap(-0.5f, 0.5f, listOf(0f)))
        assertTrue(FxParamMath.crossedSnap(0.5f, 0f, listOf(0f)))
        assertFalse(FxParamMath.crossedSnap(0f, 0.5f, listOf(0f)))   // saindo do encaixe não vibra
        assertFalse(FxParamMath.crossedSnap(1f, 2f, listOf(0f)))
    }

    @Test
    fun decimalsFollowTheStepAndAnglesShowTurns() {
        assertEquals(5, FxParamMath.decimalsForStep(0.00001f))
        assertEquals(3, FxParamMath.decimalsForStep(0.005f))
        assertEquals(2, FxParamMath.decimalsForStep(0.01f))
        assertEquals(1, FxParamMath.decimalsForStep(0.5f))
        assertEquals(0, FxParamMath.decimalsForStep(1f))
        assertEquals(0 to 270f, FxParamMath.angleTurns(270f))
        assertEquals(2 to 30f, FxParamMath.angleTurns(750f))
        assertEquals(-2 to -30f, FxParamMath.angleTurns(-750f))
    }

    @Test
    fun onlyTheFirstEffectOfAnEmptyLayerOpensItself() {
        assertTrue(FxParamMath.opensAfterAdd(0, 1))
        assertFalse(FxParamMath.opensAfterAdd(1, 1))
        assertFalse(FxParamMath.opensAfterAdd(0, 0))
    }

    @Test
    fun guideTimingsFadeInHoldAndOut() {
        val s = FxParamMath.Guide.Short
        assertEquals(1500, s.total)
        assertEquals(0.5f, s.alphaAt(50), 1e-6f)
        assertEquals(1f, s.alphaAt(300), 1e-6f)
        assertEquals(0.5f, s.alphaAt(100L + 500 + 450), 1e-6f)
        assertEquals(0f, s.alphaAt(2000), 1e-6f)
        assertEquals(4100, FxParamMath.Guide.Long.total)
        assertEquals(2400, FxParamMath.Guide.Medium.total)
    }

    @Test
    fun trackballRoundTripsAndRotatesAroundTheRightAxis() {
        // Euler ↔ quatérnio ida e volta.
        val q = FxParamMath.eulerToQuat(20.0, -35.0, 50.0)
        val e = FxParamMath.quatToEuler(q)
        assertEquals(20.0, e[0], 1e-6)
        assertEquals(-35.0, e[1], 1e-6)
        assertEquals(50.0, e[2], 1e-6)
        // Sem arrasto: nada muda.
        val same = FxParamMath.trackball(10f, 20f, 30f, 0.2f, 0.1f, 0.2f, 0.1f)
        assertEquals(10f, same[0], 1e-4f); assertEquals(20f, same[1], 1e-4f); assertEquals(30f, same[2], 1e-4f)
        // Arrasto horizontal pelo centro gira em torno de Y (guinada); a inclinação fica.
        val yaw = FxParamMath.trackball(0f, 0f, 0f, 0f, 0f, 0.5f, 0f)
        assertTrue(abs(yaw[1]) > 20f)
        assertEquals(0f, yaw[0], 1e-3f)
        // Arrasto vertical gira em torno de X (inclinação).
        val pitch = FxParamMath.trackball(0f, 0f, 0f, 0f, 0f, 0f, 0.5f)
        assertTrue(abs(pitch[0]) > 20f)
        assertEquals(0f, pitch[1], 1e-3f)
    }
}
