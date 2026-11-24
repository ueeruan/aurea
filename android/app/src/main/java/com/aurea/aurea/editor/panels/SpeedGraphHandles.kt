package com.aurea.aurea.editor.panels

import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.state.EditorStore
import kotlin.math.abs
import kotlin.math.sqrt

/**
 * Tangents belong to the outgoing interval, never to the whole track.
 *
 * Com a força ×[power] (a bézier aplicada `power` vezes, Keyframe::easePower)
 * a inclinação nas pontas vira inclinação^power — B(0)=0 e B(1)=1, então a
 * derivada da composição é o produto das derivadas. A alça encosta na curva de
 * velocidade e o arrasto desfaz a potência para achar a alça.
 *
 * [ease] são as alças GRAVADAS (sem prender): arrastar uma alça só escreve o
 * par dela. Antes as duas saíam presas em 1 %..99 % e a alça parada era
 * regravada (o Ease Out (0;0) virava (0,01;0) ao mexer na alça de chegada).
 * [base] é a velocidade média do trecho: Δvalor·qps/Δt, ou |Δposição| na
 * velocidade espacial (X/Y/Z com a mesma curva, como o AE).
 */
internal data class SpeedHandle(val key: KeyframeRow, val end: KeyframeRow, val incoming: Boolean,
    val ease: FloatArray, val base: Float, val power: Int = 1) {
    private val duration = (end.time.toLong() - key.time).toFloat()
    private val position = AureaEngine.nativeSpeedGraphTangent(ease, incoming, power, base, 0f, 0f, false)
    val velocity get() = position?.get(1) ?: 0f
    val frame get() = key.time + duration * (position?.get(0) ?: if (incoming) .99f else .01f)
    fun changed(frame: Float, velocity: Float): FloatArray? =
        AureaEngine.nativeSpeedGraphTangent(ease, incoming, power, base, (frame - key.time) / duration, velocity, true)
}

/**
 * As alças que o gráfico mostra e grava num trecho: a bézier gravada, ou a
 * equivalente das curvas nomeadas (que ignoram a força no motor: `keyframe_ease`
 * só repete a bézier na Bézier/Custom — com força velha > 1 a alça saía fora
 * da curva).
 */
internal fun speedEase(ease: Ease, interp: Int): Pair<FloatArray, Int> {
    val h = if (interp == Interp.LINEAR) floatArrayOf(1f / 3, 1f / 3, 2f / 3, 2f / 3) else ease.handles()
    val power = if (ease.isBezier) ease.power else 1
    return h to power
}

internal fun speedHandles(store: EditorStore, layer: Long, keys: List<KeyframeRow>, fps: Float): List<SpeedHandle> =
    keys.zipWithNext().flatMap { (a, b) ->
        val base = (b.value - a.value) * fps / (b.time.toLong() - a.time).toFloat()
        val ease = easeOf(store, layer, a)
        if (!base.isFinite() || abs(base) < .000001f || !ease.hasHandles) emptyList()
        else {
            val (h, power) = speedEase(ease, a.interpolation)
            listOf(SpeedHandle(a, b, false, h, base, power), SpeedHandle(a, b, true, h, base, power))
        }
    }

/**
 * VELOCIDADE ESPACIAL (como o gráfico de velocidade do AE para a Posição): com
 * as trilhas irmãs ([siblings], X/Y/Z) tendo marca nos MESMOS instantes, a
 * velocidade é o módulo do vetor — |Δp|·E′(t)·qps/Δt quando a curva é a mesma
 * nos eixos (a do painel de curva e do arrasto, que liga os eixos). A alça usa
 * esse |Δp| como base. Vazio = algum trecho sem marca irmã no mesmo instante.
 */
internal fun spatialSpeedHandles(store: EditorStore, layer: Long, keys: List<KeyframeRow>,
                                 siblings: List<List<KeyframeRow>>, fps: Float): List<SpeedHandle> {
    if (siblings.isEmpty()) return emptyList()
    val out = ArrayList<SpeedHandle>()
    for ((a, b) in keys.zipWithNext()) {
        var sq = (b.value - a.value).let { it * it }
        for (s in siblings) {
            val va = s.firstOrNull { it.time == a.time } ?: return emptyList()
            val vb = s.firstOrNull { it.time == b.time } ?: return emptyList()
            sq += (vb.value - va.value).let { it * it }
        }
        val base = sqrt(sq) * fps / (b.time.toLong() - a.time).toFloat()
        val ease = easeOf(store, layer, a)
        if (!base.isFinite() || base < .000001f || !ease.hasHandles) continue
        val (h, power) = speedEase(ease, a.interpolation)
        out += SpeedHandle(a, b, false, h, base, power)
        out += SpeedHandle(a, b, true, h, base, power)
    }
    return out
}
