package com.aurea.aurea.editor.timeline

import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.ui.theme.LayerType
import org.junit.Assert.*
import org.junit.Test

class TimelinePropertyIsolationTest {
    private fun key(p: Int, t: Int, fx: Int = -1, param: Int = 0) = KeyframeRow(p, fx, t, 1f, 1, param)
    private val position = key(0, 10)
    private val rotation = key(8, 10)
    private val scale = key(3, 11)
    private fun row() = RowModel(1, LayerType.Image, 20, 100, 5, true, false, true, "clip", 0,
        intArrayOf(25, 26, 45), arrayOf(listOf(position, rotation), listOf(scale), listOf(key(0, 30))))

    @Test fun overviewDragDoesNotMoveCoincidentRotation() {
        val row = row()
        val moving = row.keysForDrag(0, false)
        assertEquals(listOf(position), moving)
        assertArrayEquals(intArrayOf(25, 45), row.dragInstants(moving))
        val limits = IntArray(2)
        Keyframes.dragLimits(row.dragInstants(moving), 0, row.start, row.end, limits)
        assertEquals(44, limits[1]) // scale at frame 26 must not block position
        assertEquals(10, row.toLocal(25))
    }

    @Test fun focusSeparatesTransformAndEffectParameters() {
        val a = key(31, 10, 7, 4)
        val b = key(31, 10, 7, 5)
        val c = key(31, 10, 8, 4)
        val keys = listOf(position, rotation, scale, a, b, c)
        assertEquals(listOf(rotation), focusedKeys(keys, listOf(TrackKey(8))))
        assertEquals(listOf(a), focusedKeys(keys, listOf(TrackKey(31, 7, 4))))
        assertTrue(focusedKeys(keys, emptyList()).isEmpty())
        assertEquals(keys, listOf(position, rotation, scale, a, b, c))
    }

    @Test fun selectionDoesNotJumpToAnotherPropertyAtTheSameFrame() {
        assertEquals(25, row().selectedFrame(position))
        assertEquals(Snap.NONE, row().selectedFrame(key(12, 10)))
        assertEquals(Snap.NONE, row().selectedFrame(position.copy(time = 11)))
        assertEquals(Snap.NONE, row().selectedFrame(position.copy(effectIndex = 2)))
    }

    @Test fun frameGridRoundTripsAcrossZoomAndFractionalFps() {
        for (fps in listOf(23.976f, 29.97f, 30f, 59.94f, 60f)) {
            for (pps in listOf(2f, 80f, 800f)) {
                val ppf = TimeAxis.pxPerFrame(pps, 1f, fps)
                for (raw in listOf(0.0, 10.49, 10.51, 999.7)) {
                    val frame = TimeAxis.clampView(raw, 1200).toFrame()
                    assertEquals(180f, TimeAxis.xOf(frame.toDouble(), frame.toDouble(), ppf, 180f))
                    assertEquals(frame, TimeAxis.frameAt(180f, frame.toDouble(), ppf, 180f).toFrame())
                }
            }
        }
    }
}
