package com.aurea.aurea.ai

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.IOException
import org.junit.Assert.*
import org.junit.Test

class VideoSecurityTest {
    @Test fun exactLimitCopiesEveryByte() {
        val bytes = ByteArray(65539) { (it % 251).toByte() }
        val output = ByteArrayOutputStream()
        AureaBackendVideoProvider.copyBounded(ByteArrayInputStream(bytes), output, bytes.size.toLong())
        assertArrayEquals(bytes, output.toByteArray())
    }

    @Test fun missingLengthCannotWriteBeyondLimit() {
        val output = ByteArrayOutputStream()
        try {
            AureaBackendVideoProvider.copyBounded(ByteArrayInputStream(ByteArray(200000)), output, 70000)
            fail("an oversized response must be rejected")
        } catch (_: IOException) { assertTrue(output.size() <= 70000) }
    }
}
