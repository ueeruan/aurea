package com.aurea.aurea.editor

import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.PodLayout
import org.junit.Assert.*
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Run the two methods in separate instrumentation sessions with am force-stop between them. */
class PrecompRecoveryTest {
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private val project get() = File(context.filesDir, "precomp-killed-process.aurea")

    private fun engine(): AureaEngine {
        check(context.packageName.endsWith(".uitest"))
        return AureaEngine.create(context).also {
            assertTrue(it.initialize(60f, context.cacheDir.absolutePath, context.filesDir.absolutePath, true))
        }
    }

    private fun layers(engine: AureaEngine): LongArray {
        val rows = ByteBuffer.allocateDirect(PodLayout.LAYER_ROW_BYTES * 16).order(ByteOrder.LITTLE_ENDIAN)
        val names = ByteBuffer.allocateDirect(4096)
        val count = engine.queryLayers(rows, 16, names)
        assertTrue(count in 0..16)
        return LongArray(count) { rows.getLong(it * PodLayout.LAYER_ROW_BYTES + PodLayout.LAYER_OFF_ID) }
    }

    @Test fun saveWhileNestedBeforeProcessKill() {
        val engine = engine()
        try {
            assertTrue(engine.newProject(640, 360, 30f, "Interrupted precomp"))
            assertNotEquals(0L, engine.addText("Outside A"))
            assertNotEquals(0L, engine.addText("Outside B"))
            val inside = engine.addText("Inside")
            val group = engine.precompose(longArrayOf(inside))
            assertNotEquals(0L, group)
            assertEquals(0, engine.saveProject(project.absolutePath))
            assertTrue(engine.openPrecomp(group))
            val nested = engine.precompose(longArrayOf(engine.addText("Deep text")))
            assertTrue(engine.openPrecomp(nested))
            assertNotEquals(0L, engine.addText("Last saved edit"))
            assertEquals(0, engine.autosaveProject())
            assertEquals(2, engine.precompDepth())
            assertTrue(project.length() > 0)
        } finally {
            // Destruction does not navigate back or save the project.
            engine.destroy()
        }
    }

    @Test fun reopenAfterProcessKillPreservesMainAndNestedLayers() {
        assertTrue("Run saveWhileNestedBeforeProcessKill first", project.isFile)
        val engine = engine()
        try {
            assertEquals(0, engine.loadProject(project.absolutePath))
            assertEquals(0, engine.precompDepth())
            val root = layers(engine)
            assertEquals(3, root.size)
            assertFalse(engine.closePrecomp())
            assertTrue(root.any { engine.openPrecomp(it) })
            assertEquals(1, engine.precompDepth())
            val child = layers(engine)
            assertEquals(2, child.size)
            assertTrue(child.any { engine.openPrecomp(it) })
            assertEquals(2, layers(engine).size)
            assertTrue(engine.closePrecomp())
            assertTrue(engine.closePrecomp())
            assertEquals(3, layers(engine).size)
        } finally { engine.destroy() }
    }
}
