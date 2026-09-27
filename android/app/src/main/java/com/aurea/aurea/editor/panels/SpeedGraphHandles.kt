package com.aurea.aurea.editor.panels

import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.state.EditorStore
import kotlin.math.abs

/** Tangents belong to the outgoing interval, never to the whole track. */
internal data class SpeedHandle(val key: KeyframeRow, val end: KeyframeRow, val incoming: Boolean,
    val ease: FloatArray, val base: Float) {
    val influence get() = if (incoming) 1f - ease[2] else ease[0]
    val velocity get() = base * if (incoming) (1f - ease[3]) / influence else ease[1] / influence
    val frame get() = key.time + (end.time.toLong() - key.time).toFloat() * if (incoming) ease[2] else ease[0]
    fun changed(frame: Float, velocity: Float): FloatArray {
        val h = ease.copyOf()
        val fraction = ((frame - key.time) / (end.time.toLong() - key.time).toFloat()).coerceIn(.01f, .99f)
        if (incoming) { h[2] = fraction; h[3] = 1f - velocity / base * (1f - fraction) }
        else { h[0] = fraction; h[1] = velocity / base * fraction }
        return h
    }
}

internal fun speedHandles(store: EditorStore, layer: Long, keys: List<KeyframeRow>, fps: Float): List<SpeedHandle> =
    keys.zipWithNext().flatMap { (a, b) ->
        val base = (b.value - a.value) * fps / (b.time.toLong() - a.time).toFloat()
        val ease = easeOf(store, layer, a)
        if (!base.isFinite() || abs(base) < .000001f || !ease.hasHandles) emptyList()
        else {
            val h = if (a.interpolation == Interp.LINEAR) floatArrayOf(1f/3, 1f/3, 2f/3, 2f/3) else ease.handles()
            h[0] = h[0].coerceIn(.01f,.99f); h[2] = h[2].coerceIn(.01f,.99f)
            listOf(SpeedHandle(a,b,false,h,base), SpeedHandle(a,b,true,h,base))
        }
    }
