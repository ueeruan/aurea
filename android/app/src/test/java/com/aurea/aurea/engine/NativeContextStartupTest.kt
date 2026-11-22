package com.aurea.aurea.engine

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeContextStartupTest {
    @Test fun arm64HeapTagDoesNotBlockGraphicsStartup() {
        val taggedContext = 0xB400007F12345678uL.toLong()
        assertTrue(initializeNativeContext(taggedContext) { handle ->
            assertEquals(taggedContext, handle)
            true
        })
    }

    @Test fun nullContextDoesNotEnterJniAndBackendFailurePropagates() {
        assertFalse(initializeNativeContext(0L) { error("entered JNI with a null context") })
        assertFalse(initializeNativeContext(0x1234L) { false })
    }
}
