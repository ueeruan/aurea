package com.aurea.aurea.editor.timeline

import org.junit.Assert.*
import org.junit.Test

/** Regression for the first diamond appearing on the white grip instead of the clip body. */
class TimelineTemporalBoundsTest {
    @Test fun shortClipEndStaysOnBeatThroughZoomOutAndBackIn() {
        val beat = 120
        for (duration in listOf(1, 2, 5, 30)) for (density in listOf(1f, 2.625f))
            for (fps in listOf(23.976f, 30f, 60f)) for (view in listOf(119.625, 120.0, 120.375)) {
                // A zoom cycle exposes a minimum-width rectangle: its end
                // would detach from this marker only at the smaller scales.
                for (zoom in listOf(800f, 100f, 20f, 2f, 20f, 100f, 800f)) {
                    val ppf = TimeAxis.pxPerFrame(zoom, density, fps)
                    val start = TimeAxis.xOf((beat - duration).toDouble(), view, ppf, 216f * density)
                    val end = TimeAxis.xOf(beat.toDouble(), view, ppf, 216f * density)
                    val marker = TimeAxis.xOf(beat.toDouble(), view, ppf, 216f * density)
                    assertEquals(marker, end, 0f)
                    assertEquals(duration * ppf, end - start, .0002f * density)
                    val stroke = ClipGeometry.strokeWidth(end - start, 23f * density, 2f * density)
                    val radius = ClipGeometry.cornerRadius(end - start, 23f * density, 8f * density)
                    assertTrue(stroke <= end - start)
                    assertTrue(radius * 2 <= end - start)
                    val handleWidth = ClipGeometry.trimPaintWidth(end - start, 16f * density)
                    val handleLeft = end - ClipGeometry.trimPaintEndInset(end - start, 16f * density)
                    if (end - start < 32f * density) {
                        assertEquals("Narrow end grip must not extend duration beyond beat", marker, handleLeft + handleWidth, .0001f * density)
                    }
                    assertEquals(beat, TimeAxis.frameAt(end, view, ppf, 216f * density).toFrame())
                }
            }
    }

    @Test fun decorationFitsVisibleFragmentWithoutInflatingTemporalDuration() {
        for (width in listOf(.01f, .1f, 1f, 2f, 5f, 40f)) {
            val stroke = ClipGeometry.strokeWidth(width, 23f, 2f)
            val radius = ClipGeometry.cornerRadius(width, 23f, 14f)
            assertTrue(stroke in 0f..width)
            assertTrue(radius in 0f..(width / 2f))
            // The stroke's outer right edge equals the real rectangle edge.
            val right = 200f + width
            val inset = stroke / 2f
            assertEquals(right, right - inset + stroke / 2f, .0001f)
        }
    }

    @Test fun narrowPaintedGripsKeepBroadExternalTrimTargets() {
        for (refreshed in listOf(false, true)) for (density in listOf(1f, 2.625f)) {
            val metrics = TimelineMetrics(density, referenceTrial = refreshed)
            val x0 = 216f * density
            val x1 = x0 + .1f * density
            val out = IntArray(1)
            assertTrue(ClipGeometry.trimPaintWidth(x1 - x0, metrics.trimWidth) < .1f * density)
            assertEquals(HitKind.TRIM_END, RowHit.hit(metrics, x1 + 12f * density, metrics.bar / 2,
                600f * density, x0, x1, true, false, false, intArrayOf(), 100.0, .1f * density, x0, out))
            assertEquals(HitKind.TRIM_START, RowHit.hit(metrics, x0 - 12f * density, metrics.bar / 2,
                600f * density, x0, x1, true, false, false, intArrayOf(), 100.0, .1f * density, x0, out))
            val paintedWidth = ClipGeometry.trimPaintWidth(x1 - x0, metrics.trimWidth)
            val paintedCenter = x1 - ClipGeometry.trimPaintEndInset(x1 - x0, metrics.trimWidth) + paintedWidth / 2
            assertEquals("The visible short end grip must trim, not move the body", HitKind.TRIM_END,
                RowHit.hit(metrics, paintedCenter, metrics.bar / 2,
                    600f * density, x0, x1, true, false, false, intArrayOf(), 100.0, .1f * density, x0, out))
            assertEquals("Left half remains usable to move the clip", HitKind.BODY,
                RowHit.hit(metrics, x0 + (x1 - x0) / 4, metrics.bar / 2,
                    600f * density, x0, x1, true, false, false, intArrayOf(), 100.0, .1f * density, x0, out))
            assertEquals("A boundary key keeps priority over the painted short grip", HitKind.KEYFRAME,
                RowHit.hit(metrics, paintedCenter, metrics.bar / 2,
                    600f * density, x0, x1, true, false, true, intArrayOf(101), 100.0, .1f * density, x0, out))
        }
    }

    @Test fun firstAndLastKeysStayOnTemporalBodyEdgesInEveryPresentation() {
        for (refreshed in listOf(false, true)) for (compact in listOf(false, true)) {
            for (density in listOf(1f, 2.625f)) for ((start, offset) in listOf(0 to 0, 60 to 20, 80 to 40)) {
                val metrics = TimelineMetrics(density, referenceTrial = refreshed)
                val end = start + 60
                val view = start.toDouble()
                val center = 216f * density
                val ppf = 2f * density
                val x0 = TimeAxis.xOf(start.toDouble(), view, ppf, center)
                val x1 = TimeAxis.xOf(end.toDouble(), view, ppf, center)
                val first = Keyframes.toTimeline(offset, start, offset)
                val last = Keyframes.toTimeline(offset + end - start, start, offset)
                assertEquals(x0, TimeAxis.xOf(first.toDouble(), view, ppf, center), 0f)
                assertEquals(x1, TimeAxis.xOf(last.toDouble(), view, ppf, center), 0f)
                assertTrue(RowHit.capLeft(metrics, x0) < x0)
                for ((x, index) in listOf(x0 to 0, x1 to 1)) {
                    val out = IntArray(1)
                    assertEquals(HitKind.KEYFRAME, RowHit.hit(metrics, x, metrics.diamondCyNormal,
                        600f * density, x0, x1, true, compact, true, intArrayOf(first, last), view, ppf, center, out))
                    assertEquals(index, out[0])
                }
                if (refreshed) {
                    assertEquals(x0, x0 - metrics.trimInsetStart + metrics.trimWidth, 0f)
                    assertEquals(x1, x1 - metrics.trimInsetEnd, 0f)
                    val out = IntArray(1)
                    assertEquals(HitKind.TRIM_START, RowHit.hit(metrics, x0 - 25f * density, metrics.bar / 2f,
                        600f * density, x0, x1, true, compact, true, intArrayOf(first, last), view, ppf, center, out))
                    assertEquals(HitKind.TRIM_END, RowHit.hit(metrics, x1 + 25f * density, metrics.bar / 2f,
                        600f * density, x0, x1, true, compact, true, intArrayOf(first, last), view, ppf, center, out))
                }
            }
        }
    }

    @Test fun oneFrameClipKeepsItsActualEndAtLowZoom() {
        val metrics = TimelineMetrics(1f, referenceTrial = true)
        val x0 = TimeAxis.xOf(100.0, 100.0, .1f, 216f)
        val x1 = TimeAxis.xOf(101.0, 100.0, .1f, 216f)
        assertEquals(.1f, x1 - x0, .0001f)
        val out = IntArray(1)
        assertEquals(HitKind.BODY, RowHit.hit(metrics, (x0 + x1) / 2, metrics.bar / 2,
            600f, x0, x1, false, false, false, intArrayOf(), 100.0, .1f, 216f, out))
        assertEquals(HitKind.NONE, RowHit.hit(metrics, x1 + 1f, metrics.bar / 2,
            600f, x0, x1, false, false, false, intArrayOf(), 100.0, .1f, 216f, out))
    }

    @Test fun referenceGeometryHasSpaceForClockAndThumbnail() {
        val metrics = TimelineMetrics(1f, referenceTrial = true)
        assertEquals(42f, metrics.rowsTop, 0f)
        assertEquals(28f, metrics.row, 0f)
        assertEquals(2.5f, metrics.barTop, 0f)
        assertEquals(23f, metrics.bar, 0f)
        assertEquals(70f, metrics.headerColumn, 0f)
        assertEquals(1.5f, metrics.barRadius, 0f)
        assertEquals(100f, Zoom.DEFAULT_PPS, 0f)
    }
}
