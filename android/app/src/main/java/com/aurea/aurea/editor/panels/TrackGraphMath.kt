package com.aurea.aurea.editor.panels

import kotlin.math.max

internal data class GraphSample(val frame: Float, val value: Float)
internal data class GraphHitPoint(val index: Int, val x: Float, val y: Float)

/** Visual dots may be tiny; their touch target is independently 48dp wide.
 * Prefer the selected point only for coincident/equidistant targets. */
internal fun graphHitIndex(points: List<GraphHitPoint>, x: Float, y: Float, radius: Float, selected: Int = -1): Int {
    if (!x.isFinite() || !y.isFinite() || !radius.isFinite() || radius <= 0f) return -1
    var best = -1
    var distance = radius * radius
    points.forEach { point ->
        val dx = point.x - x; val dy = point.y - y
        val d = dx * dx + dy * dy
        if (d.isFinite() && (d < distance || (d == distance && (best == -1 || point.index == selected)))) {
            best = point.index; distance = d
        }
    }
    return best
}

/** query_track_curve samples integer frames, including when the requested
 * resolution exceeds the number of frames. Remove repeats before deriving
 * velocity so quantization cannot create alternating zero/spike artifacts. */
internal fun graphSamples(values: FloatArray, from: Int, to: Int, fps: Float, speed: Boolean): List<GraphSample> {
    if (values.isEmpty() || to <= from) return emptyList()
    val samples = ArrayList<GraphSample>(values.size)
    values.forEachIndexed { i, value ->
        val frame = (from.toDouble() + (to.toDouble() - from) * i / max(1, values.size - 1)).toInt().toFloat()
        if (value.isFinite() && (samples.isEmpty() || samples.last().frame != frame)) samples += GraphSample(frame, value)
    }
    if (!speed) return samples
    return samples.zipWithNext().mapNotNull { (a, b) ->
        val velocity = (b.value - a.value) * fps / (b.frame - a.frame)
        if (velocity.isFinite()) GraphSample((a.frame + b.frame) * 0.5f, velocity) else null
    }
}

internal data class GraphViewport(val from: Float, val to: Float, val low: Float, val high: Float) {
    val duration get() = max(1f, to - from)
    val range get() = max(0.0001f, high - low)
    fun transform(zoom: Float, dx: Float, dy: Float, anchorX: Float = 0.5f, anchorY: Float = 0.5f): GraphViewport {
        if (!zoom.isFinite() || !dx.isFinite() || !dy.isFinite() || !anchorX.isFinite() || !anchorY.isFinite()) return this
        val factor = zoom.coerceIn(0.25f, 4f)
        val nextDuration = (duration / factor).coerceIn(1f, 1_000_000_000f)
        val nextRange = (range / factor).coerceIn(0.0001f, 1e12f)
        val nextFrom = (from + duration * anchorX - nextDuration * anchorX - dx * duration).coerceIn(-1e9f, 1e9f - nextDuration)
        val nextLow = low + range * anchorY - nextRange * anchorY + dy * range
        return GraphViewport(nextFrom, nextFrom + nextDuration, nextLow, nextLow + nextRange)
    }
}
