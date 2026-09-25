package com.aurea.aurea.ai

import org.junit.Assert.*
import org.junit.Test
import java.io.InputStream

class ImageUploadBytesTest {
    @Test fun keepsBytesAtTheLimitAndRejectsOversizeWithoutReadingTheWholeFile() {
        val bytes = ByteArray(65536) { (it % 251).toByte() }
        assertArrayEquals(bytes, readImageUploadBytes(bytes.inputStream(), bytes.size))
        var consumed = 0
        val huge = object : InputStream() {
            override fun read(): Int { consumed++; return 42 }
            override fun read(b: ByteArray, off: Int, len: Int): Int {
                b.fill(42, off, off + len); consumed += len; return len
            }
        }
        assertNull(readImageUploadBytes(huge, bytes.size))
        assertEquals(bytes.size + 1, consumed)
    }
}
