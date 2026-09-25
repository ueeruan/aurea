package com.aurea.aurea.editor.timeline

import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.ui.theme.LayerType
import org.junit.Assert.*
import org.junit.Test

class TimelineExpansionTest {
    private fun row(id: Long) = RowModel(id, LayerType.Video, 100, 200, 20, true, false, true, "Clip", 0, intArrayOf(110), emptyArray())
    @Test fun expandedKeysPreserveTrackIdentityAndLocalTime() {
        val position = KeyframeRow(0, -1, 30, 10f, 1, 0)
        val effect = KeyframeRow(31, 7, 30, 20f, 1, 2)
        val text = KeyframeRow(33, 1, 30, 30f, 1, 8)
        val base = listOf(row(5), row(6))
        val expanded = expandedRows(base, 5, mapOf(5L to listOf(position, effect, text)), listOf(7 to "Blur"))
        assertSame(base[0], expanded.first())
        assertSame(base[1], expanded.last())
        assertEquals(7, expanded.size)
        for (key in listOf(position, effect, text)) {
            val lane = expanded.single { it.track == TimelineTrack(key.property, key.effectIndex, key.paramIndex) }
            assertArrayEquals(intArrayOf(110), lane.instants)
            assertEquals(listOf(key), lane.keysAt.single())
            assertEquals(30, lane.toLocal(110))
            assertFalse(lane.hasThumbs)
        }
        assertTrue(expanded.single { it.track == TimelineTrack(31, 7, -1) }.keysAt.isEmpty())
        assertSame(base, expandedRows(base, null, emptyMap(), emptyList()))
    }
}
