package com.aurea.aurea.engine

import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class GraphicsStartupDeviceTest {
    @Test fun startupOpensAndRendersAProject() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val scratch = File(context.cacheDir, "graphics-startup-test").apply { mkdirs() }
        val documents = File(scratch, "projects").apply { mkdirs() }
        val engine = AureaEngine.create(context)
        try {
            val started = engine.initialize(60f, scratch.absolutePath, documents.absolutePath, debug = true)
            assertTrue(engine.startupError(), started)
            val statusBytes = ByteBuffer.allocateDirect(PodLayout.STATUS_BYTES).order(ByteOrder.nativeOrder())
            val status = EngineStatus()
            assertTrue(engine.readStatus(statusBytes))
            status.readFrom(statusBytes)
            assertEquals("Recovered startup must clear the previous backend error", 0, status.lastError)
            assertTrue(engine.newProject(64, 64, 30f, "Graphics startup"))
            val pixels = ByteBuffer.allocateDirect(64 * 64 * 4)
            val size = IntArray(2)
            assertEquals("Graphics backend must render a frame", pixels.capacity(), engine.captureFrame(64, pixels, size))
            assertEquals(64, size[0])
            assertEquals(64, size[1])
        } finally {
            engine.destroy()
        }
    }
}
