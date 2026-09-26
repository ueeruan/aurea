package com.aurea.aurea.editor.panels

import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.TrackProperty
import org.junit.Assert.*
import org.junit.Test

class CurveTrackAccessTest {
    private fun key(property: Int, time: Int, value: Float, param: Int = 0) =
        KeyframeRow(property, -1, time, value, 1, param)

    @Test fun nullPositionFindsYOrZWhenXHasNoMotion() {
        for (axis in listOf(TrackProperty.POSITION_Y, TrackProperty.POSITION_Z)) {
            val still = listOf(key(0, 0, 50f), key(0, 30, 50f))
            val moving = listOf(key(axis, 0, 0f), key(axis, 30, 200f))
            assertEquals(moving, curveTrack(listOf(still, moving)))
            assertEquals(moving.first(), curveTrack(listOf(emptyList(), moving)).segmentStart(30))
        }
    }

    @Test fun shapeHeightCurveIsAvailableAfterWidthIsFocused() {
        val height = listOf(key(TrackProperty.SHAPE_PARAM, 0, 100f, 6), key(TrackProperty.SHAPE_PARAM, 30, 400f, 6))
        assertEquals(height, curveTrack(listOf(height.shapeTrack(5), height.shapeTrack(6))))
    }

    @Test fun singleKeyCanOpenExplanationAndUnanimatedTracksStayEmpty() {
        val one = listOf(key(0, 0, 50f))
        assertEquals(one, curveTrack(listOf(one, emptyList())))
        assertTrue(curveTrack(listOf(emptyList())).isEmpty())
    }
}
