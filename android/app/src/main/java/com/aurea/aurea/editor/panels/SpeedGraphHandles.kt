package com.aurea.aurea.editor.panels

import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.state.EditorStore
import kotlin.math.abs

/**
 * Tangents belong to the outgoing interval, never to the whole track.
 *
 * Com a força ×[power] (a bézier aplicada `power` vezes, Keyframe::easePower)
 * a inclinação nas pontas vira inclinação^power — B(0)=0 e B(1)=1, então a
 * derivada da composição é o produto das derivadas. A alça encosta na curva de
 * velocidade e o arrasto desfaz a potência para achar a alça.
 */
internal data class SpeedHandle(val key: KeyframeRow, val end: KeyframeRow, val incoming: Boolean,
    val ease: FloatArray, val base: Float, val power: Int = 1) {
    val influence get() = if (incoming) 1f - ease[2] else ease[0]
    private val slope get() = if (incoming) (1f - ease[3]) / influence else ease[1] / influence
    val velocity get() = base * powered(slope, power)
    val frame get() = key.time + (end.time.toLong() - key.time).toFloat() * if (incoming) ease[2] else ease[0]
    fun changed(frame: Float, velocity: Float): FloatArray {
        val h = ease.copyOf()
        val fraction = ((frame - key.time) / (end.time.toLong() - key.time).toFloat()).coerceIn(.01f, .99f)
        val s = rooted(velocity / base, power)
        if (incoming) { h[2] = fraction; h[3] = 1f - s * (1f - fraction) }
        else { h[0] = fraction; h[1] = s * fraction }
        return h
    }
}

/** s^power com o sinal de s (a força é 1..3). */
internal fun powered(s: Float, power: Int): Float {
    var out = s
    repeat(power.coerceIn(1, 3) - 1) { out *= s }
    return out
}

/** A inversa de [powered]: a raiz `power` com sinal (par e negativo = o espelho positivo). */
internal fun rooted(v: Float, power: Int): Float {
    val p = power.coerceIn(1, 3)
    if (p == 1) return v
    val r = Math.pow(abs(v).toDouble(), 1.0 / p).toFloat()
    return if (v < 0f && p % 2 == 1) -r else r
}

internal fun speedHandles(store: EditorStore, layer: Long, keys: List<KeyframeRow>, fps: Float): List<SpeedHandle> =
    keys.zipWithNext().flatMap { (a, b) ->
        val base = (b.value - a.value) * fps / (b.time.toLong() - a.time).toFloat()
        val ease = easeOf(store, layer, a)
        if (!base.isFinite() || abs(base) < .000001f || !ease.hasHandles) emptyList()
        else {
            val h = if (a.interpolation == Interp.LINEAR) floatArrayOf(1f/3, 1f/3, 2f/3, 2f/3) else ease.handles()
            h[0] = h[0].coerceIn(.01f,.99f); h[2] = h[2].coerceIn(.01f,.99f)
            val power = if (a.interpolation == Interp.LINEAR) 1 else ease.power
            listOf(SpeedHandle(a,b,false,h,base,power), SpeedHandle(a,b,true,h,base,power))
        }
    }
