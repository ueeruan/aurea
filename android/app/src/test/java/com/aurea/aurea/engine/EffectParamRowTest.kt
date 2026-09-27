package com.aurea.aurea.engine

import com.aurea.aurea.editor.panels.ParamSlot
import com.aurea.aurea.ui.ds.dragBounds
import org.junit.Assert.assertEquals
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Faixa do SLIDER x faixa DIGITADA (edição extrema): a linha da ponte
 * (`bridge::EffectParamRow`, 104 bytes) traz as duas; a régua anda na primeira,
 * o teclado aceita a segunda, e o arrasto que começa fora da régua não salta.
 */
class EffectParamRowTest {

    private fun row(min: Float, max: Float, hardMin: Float, hardMax: Float, value: Float): ByteBuffer {
        val b = ByteBuffer.allocate(EffectParam.ROW_BYTES).order(ByteOrder.nativeOrder())
        b.putInt(0, 3)                    // index
        b.putInt(4, ParamType.FLOAT)      // type
        b.putFloat(16, min)
        b.putFloat(20, max)
        b.putFloat(24, value)
        b.putFloat(92, hardMin)
        b.putFloat(96, hardMax)
        return b
    }

    private val emptyBlob: ByteBuffer = ByteBuffer.allocate(0)

    @Test
    fun rowIs104BytesWithTheTypedRangeAfterTheId() {
        assertEquals(104, EffectParam.ROW_BYTES)
        val p = EffectParam.read(row(0f, 500f, 0f, 3000f, 1200f), 0, emptyBlob)
        assertEquals(3, p.index)
        assertEquals(0f, p.min)
        assertEquals(500f, p.max)
        assertEquals(0f, p.hardMin)
        assertEquals(3000f, p.hardMax)
        assertEquals(1200f, p.value[0])
    }

    @Test
    fun typedRangeNeverNarrowerThanTheSlider() {
        // Linha antiga/zerada ou NaN: cai na faixa do slider, nunca a estreita.
        val zero = EffectParam.read(row(-10f, 10f, 0f, 0f, 0f), 0, emptyBlob)
        assertEquals(-10f, zero.hardMin)
        assertEquals(10f, zero.hardMax)
        val nan = EffectParam.read(row(1f, 2f, Float.NaN, Float.NaN, 1f), 0, emptyBlob)
        assertEquals(1f, nan.hardMin)
        assertEquals(2f, nan.hardMax)
    }

    @Test
    fun slotCarriesTheTypedRangeForTheKeypad() {
        val p = EffectParam.read(row(0f, 500f, -20f, 3000f, 0f), 0, emptyBlob)
        val s = ParamSlot.of(p)
        assertEquals(0f, s.min)
        assertEquals(500f, s.max)
        assertEquals(-20f, s.typedLo)
        assertEquals(3000f, s.typedHi)
        // Sem faixa digitada declarada: a do slider.
        val plain = ParamSlot(0, ParamType.FLOAT, 0, 1f, 5f, "x", "", emptyList())
        assertEquals(1f, plain.typedLo)
        assertEquals(5f, plain.typedHi)
    }

    @Test
    fun dragStartingBeyondTheSliderContinuesFromTheValue() {
        // Dentro da régua: a própria faixa.
        assertEquals(0f to 500f, dragBounds(0f, 500f, 120f))
        // Digitado além do fim: a faixa do gesto vai até o valor (sem puxar para 500).
        val (lo, hi) = dragBounds(0f, 500f, 1800f)
        assertEquals(0f, lo)
        assertEquals(1800f, hi)
        assertEquals(1800f, (1800f + 0f).coerceIn(lo, hi))       // o primeiro toque não mexe
        assertEquals(1790f, (1800f - 10f).coerceIn(lo, hi))      // arrastar continua do valor
        assertEquals(-40f to 10f, dragBounds(0f, 10f, -40f))
        // Faixa invertida ou partida não finita: nunca lança nem troca os lados.
        assertEquals(0f to 10f, dragBounds(10f, 0f, Float.NaN))
    }
}
