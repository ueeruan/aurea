package com.aurea.aurea.ui.ds

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ParamRowMathTest {
    @Test
    fun rotuloCurtoFicaEm13EOLongoEncolhe() {
        assertEquals(13f, ParamRowMath.labelFontSp("Difusão"), 0f)
        assertEquals(13f, ParamRowMath.labelFontSp("Limite"), 0f)
        assertEquals(13f, ParamRowMath.labelFontSp("  Cor  "), 0f)
        assertEquals(11f, ParamRowMath.labelFontSp("Intensidade"), 0f)
        assertEquals(1, ParamRowMath.labelMaxLines("Intensidade"))
        assertEquals(2, ParamRowMath.labelMaxLines("Centro do mosaico X"))
    }

    @Test
    fun ganhoFinoComDoisDedosELento() {
        assertEquals(0.1f, ParamRowMath.scrubGain(2, 5f), 0f)
        assertEquals(0.25f, ParamRowMath.scrubGain(1, 0.01f), 0f)
        assertEquals(1f, ParamRowMath.scrubGain(1, 1f), 0f)
        assertEquals(1f, ParamRowMath.scrubGain(1, Float.NaN), 0f)
        val mid = ParamRowMath.scrubGain(1, (ParamRowMath.SLOW_SPEED + ParamRowMath.FAST_SPEED) / 2f)
        assertTrue(mid > 0.25f && mid < 1f)
        // Negativo (para a esquerda) conta a mesma velocidade.
        assertEquals(ParamRowMath.scrubGain(1, 0.2f), ParamRowMath.scrubGain(1, -0.2f), 0f)
    }

    @Test
    fun velocidadeSuavizadaIgnoraTempoInvalido() {
        assertEquals(0.5f, ParamRowMath.smoothSpeed(0.5f, 10f, 0f), 0f)
        val s = ParamRowMath.smoothSpeed(0f, 16f, 16f)
        assertEquals(ParamRowMath.SPEED_SMOOTHING, s, 1e-6f)
    }

    @Test
    fun valorAcumulaEPrendeNaFaixa() {
        assertEquals(15f, ParamRowMath.scrubValue(10f, 10f, 0.5f, 0f, 100f), 1e-6f)
        assertEquals(100f, ParamRowMath.scrubValue(90f, 100f, 1f, 0f, 100f), 0f)
        assertEquals(0f, ParamRowMath.scrubValue(5f, -100f, 1f, 0f, 100f), 0f)
        // Valor digitado além da régua: a faixa estende até ele, sem salto.
        assertEquals(150f, ParamRowMath.scrubValue(150f, 0f, 1f, 0f, 100f), 0f)
        assertEquals(149f, ParamRowMath.scrubValue(150f, -1f, 1f, 0f, 100f), 0f)
        // Sem faixa e início inválido.
        assertEquals(3f, ParamRowMath.scrubValue(Float.NaN, 3f, 1f, Float.NaN, Float.NaN), 0f)
        assertEquals(7f, ParamRowMath.scrubValue(7f, 3f, Float.NaN, 0f, 10f), 0f)
    }

    @Test
    fun riscoMaisClaroNoCentro() {
        assertEquals(1f, ParamRowMath.tickBrightness(50f, 100f), 0f)
        assertEquals(0f, ParamRowMath.tickBrightness(0f, 100f), 0f)
        assertEquals(0.5f, ParamRowMath.tickBrightness(75f, 100f), 1e-6f)
        assertEquals(0f, ParamRowMath.tickBrightness(10f, 0f), 0f)
    }
}
