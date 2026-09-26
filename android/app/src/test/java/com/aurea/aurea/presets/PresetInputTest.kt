package com.aurea.aurea.presets

import org.junit.Assert.assertArrayEquals
import org.junit.Test

class PresetInputTest {
    @Test fun exactLimitPreservesBytes() {
        val data = ByteArray(8193) { it.toByte() }
        assertArrayEquals(data, data.inputStream().use { it.readPreset(data.size) })
    }
    @Test(expected = IllegalArgumentException::class)
    fun oversizedInputIsRejected() {
        ByteArray(8193).inputStream().use { it.readPreset(8192) }
    }
}
