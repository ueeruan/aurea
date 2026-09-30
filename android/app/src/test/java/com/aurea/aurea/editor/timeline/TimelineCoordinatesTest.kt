package com.aurea.aurea.editor.timeline

import org.junit.Assert.*
import org.junit.Test

class TimelineCoordinatesTest {
    @Test fun absoluteTimeDoesNotDependOnClipBoundsOrPadding() {
        // Trim advances both start and source offset; moving advances start alone.
        for ((start, offset, expected) in listOf(Triple(0, 0, 30), Triple(60, 0, 90), Triple(80, 20, 90))) {
            val timestamp = Keyframes.toTimeline(30, start, offset)
            assertEquals(expected, timestamp)
            for (end in listOf(start + 1, start + 300, start + 30000)) {
                for (density in listOf(1f, 2.625f)) for (zoom in listOf(2f, 80f, 800f)) {
                    val m = TimelineMetrics(density)
                    val ppf = TimeAxis.pxPerFrame(zoom, density, 30f)
                    for (scroll in listOf(-0.49, 0.0, 0.49, 2.25)) {
                        val view = timestamp + scroll
                        val x = TimeAxis.xOf(timestamp.toDouble(), view, ppf, 540f)
                        assertEquals(540.0 - scroll * ppf, x.toDouble(), 0.0001)
                        assertEquals(timestamp, TimeAxis.frameAt(x, view, ppf, 540f).toFrame())
                        assertEquals(30, Keyframes.toLocal(timestamp, start, offset))
                        val x0 = TimeAxis.xOf(start.toDouble(), view, ppf, 540f)
                        val x1 = maxOf(x0 + m.barMinWidth, TimeAxis.xOf(end.toDouble(), view, ppf, 540f))
                        val index = intArrayOf(-1)
                        assertEquals(HitKind.KEYFRAME, RowHit.hit(m, x, m.diamondCyNormal, 1080f,
                            x0, x1, false, false, true, intArrayOf(timestamp), view, ppf, 540f, index))
                        assertEquals(0, index[0])
                    }
                }
            }
        }
    }

    @Test fun grabDistanceIsTemporalAndReleaseRoundsOnceToNearestFrame() {
        for (fps in listOf(23.976f, 30f, 60f)) for (zoom in listOf(2f, 80f, 800f)) {
            val ppf = TimeAxis.pxPerFrame(zoom, 2.625f, fps)
            val view = 100.25
            val key = 100
            val down = TimeAxis.xOf(key.toDouble(), view, ppf, 540f) + 3f
            val grab = key - TimeAxis.frameAt(down, view, ppf, 540f)
            // Includes fractional scroll while dragging, preserving where the diamond was grabbed.
            val newView = 106.75
            for (release in listOf(111.1, 111.49, 111.51, 111.9)) {
                val pointer = TimeAxis.xOf(release - grab, newView, ppf, 540f)
                val committed = (TimeAxis.frameAt(pointer, newView, ppf, 540f) + grab).toFrame()
                assertEquals(release.toFrame(), committed)
                assertEquals(committed, TimeAxis.frameAt(TimeAxis.xOf(committed.toDouble(), newView, ppf, 540f), newView, ppf, 540f).toFrame())
            }
        }
    }
}
