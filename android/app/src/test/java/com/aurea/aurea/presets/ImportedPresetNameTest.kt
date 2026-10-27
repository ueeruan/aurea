package com.aurea.aurea.presets

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ImportedPresetNameTest {
    @Test(timeout = 1000)
    fun longDuplicateNamesRemainUniqueAcrossSuffixWidths() {
        val saved = mutableSetOf<String>()
        repeat(110) {
            val name = importedPresetName("Very long preset title ".repeat(10), exists = saved::contains)
            assertTrue(name.length <= 60)
            assertTrue(saved.add(name))
        }
        assertEquals(110, saved.size)
        assertTrue(saved.any { it.endsWith(" 110") })
    }

    @Test
    fun shortNamesKeepTheirTitle() {
        assertEquals("Importado · Football", importedPresetName("Football") { false })
        assertEquals("Importado · Football 2", importedPresetName("Football") { it == "Importado · Football" })
        assertEquals("Imported · Football", importedPresetName("Football", { "Imported · $it" }) { false })
    }
}
