package com.aurea.aurea.state

import java.io.IOException
import java.io.InputStream
import org.junit.Assert.*
import org.junit.Test

class BoundedInputTest {
    @Test fun acceptsExactLimitAndRejectsOneMoreByte() {
        assertArrayEquals(byteArrayOf(1, 2), byteArrayOf(1, 2).inputStream().readBounded(2))
        assertTrue(runCatching { byteArrayOf(1, 2, 3).inputStream().readBounded(2) }.exceptionOrNull() is IOException)
        assertArrayEquals(byteArrayOf(), byteArrayOf().inputStream().readBounded(0))
    }
    @Test fun providerReturningZeroCannotSpinForever() {
        val input = object : InputStream() {
            var remaining = 3
            override fun read(bytes: ByteArray, off: Int, len: Int) = 0
            override fun read() = if (remaining-- > 0) 7 else -1
        }
        assertArrayEquals(byteArrayOf(7, 7, 7), input.readBounded(3))
    }
}
