package com.aurea.aurea.state

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 0.5.4 Cartoon do texto 3D: o contrato do FloatArray com a ponte (aurea_jni.cpp
 * kText3DFields = 45, campos 35..44) e a leitura de um array antigo (35 campos).
 */
class Text3DCartoonInfoTest {
    private fun base() = Text3DInfo(content = "Pop", depth = 0.32f, alignment = 1, color = floatArrayOf(0.96f, 0.45f, 0.55f, 1f))

    @Test fun fieldsMatchTheBridgeContract() {
        assertEquals(45, Text3DInfo.FIELDS)
        assertEquals(Text3DInfo.FIELDS, base().toFields().size)
    }

    @Test fun cartoonFieldsRoundTripThroughTheBridgeArray() {
        val info = base().copy(
            style = Text3DInfo.STYLE_CARTOON, toonSideAuto = false, toonSideColor = floatArrayOf(0.2f, 0.4f, 0.6f, 1f),
            toonHighlight = 0.7f, outlineColor = floatArrayOf(0f, 0.5f, 1f, 1f), outlineWidth = 12.5f,
        )
        val f = info.toFields()
        assertEquals(1f, f[35])
        assertEquals(0f, f[36])
        assertEquals(12.5f, f[44])
        val back = Text3DInfo.of("Pop", f)
        assertTrue(back.cartoon)
        assertFalse(back.toonSideAuto)
        assertEquals(0.7f, back.toonHighlight)
        assertEquals(12.5f, back.outlineWidth)
        assertArrayEquals(floatArrayOf(0.2f, 0.4f, 0.6f, 1f), back.toonSideColor, 0f)
        assertArrayEquals(floatArrayOf(0f, 0.5f, 1f, 1f), back.outlineColor, 0f)
        assertTrue(back.sameAs(info))
        assertFalse(back.sameAs(info.copy(outlineWidth = 4f)))
    }

    @Test fun anOldThirtyFiveFieldArrayReadsAsStandard() {
        val old = base().toFields().copyOf(35)
        val info = Text3DInfo.of("Pop", old)
        assertFalse(info.cartoon)
        assertTrue(info.toonSideAuto)
        assertEquals(8f, info.outlineWidth)
    }

    @Test fun theCartoonPresetHasTheEngineIndex() {
        assertEquals(Text3DInfo.PRESET_CARTOON, Text3DPreset.Cartoon.ordinal)
    }
}
