package com.aurea.aurea.editor.panels

import com.aurea.aurea.engine.KeyframeRow
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
    /** Influência mostrada (a mínima de 1 % evita a divisão por zero na ponta). */
    val influence get() = (if (incoming) 1f - ease[2] else ease[0]).coerceIn(.01f, .99f)
    private val rise get() = if (incoming) 1f - ease[3] else ease[1]
    private val slope get() = rise / influence
    val velocity get() = base * powered(slope, power)
    val frame get() = key.time + (end.time.toLong() - key.time).toFloat() * if (incoming) 1f - influence else influence
    fun changed(frame: Float, velocity: Float): FloatArray {
        val h = ease.copyOf()
        val fraction = ((frame - key.time) / (end.time.toLong() - key.time).toFloat()).coerceIn(.01f, .99f)
        val s = rooted(velocity / base, power)
        // y nos mesmos −2..3 da curva, do preset e do motor (CurveMath.kt):
        // fora disso o preset salvo cortava a curva e o painel de curva a pulava.
        if (incoming) { h[2] = fraction; h[3] = (1f - s * (1f - fraction)).coerceIn(EASE_Y_MIN, EASE_Y_MAX) }
        else { h[0] = fraction; h[1] = (s * fraction).coerceIn(EASE_Y_MIN, EASE_Y_MAX) }
        return h
    }
}

/** s^power com o sinal de s (a força é 1..3). */
internal fun powered(s: Float, power: Int): Float {
    var out = s
    repeat(power.coerceIn(1, 3) - 1) { out *= s }
    return out
}

/**
 * A inversa de [powered]: a raiz `power` com sinal. Com força par a velocidade
 * não fica negativa: abaixo de zero a alça PARA em zero (antes espelhava e
 * voltava a subir sob o dedo).
 */
internal fun rooted(v: Float, power: Int): Float {
    val p = power.coerceIn(1, 3)
    if (p == 1) return v
    if (v < 0f && p % 2 == 0) return 0f
    val r = Math.pow(abs(v).toDouble(), 1.0 / p).toFloat()
    return if (v < 0f) -r else r
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
