package com.aurea.aurea.editor.timeline

import kotlin.math.abs
import kotlin.math.floor
import kotlin.math.min

/**
 * Contas puras do arrasto de losango (timeline), espelhadas em `KeyDrag` do
 * TimelineModel.swift. Beta "é difícil mover/deslizar o keyframe":
 *
 * - o dedo anda em px fracionários e o keyframe em frames inteiros: o frame só
 *   troca depois de passar meio frame + uma histerese pequena do atual, e volta
 *   pelo mesmo limiar — na fronteira entre dois frames ele não treme;
 * - o cabeçote parado NO instante de onde o losango saiu não é ímã: tocar no
 *   losango leva o cabeçote até ele, e o ímã o segurava no lugar nos primeiros
 *   8 dp do arrasto (parecia preso). Depois de sair, o cabeçote em outro frame
 *   volta a ser alvo como sempre.
 */
internal object KeyDrag {
    /**
     * Frame inteiro para o tempo [desired] (fracionário) partindo de [current].
     * [hysteresisPx] vira frames por [pxPerFrame] e fica limitada a ¼ de frame:
     * de perto (muitos px por frame) o passo continua no meio do frame; de longe
     * (vários frames por px) a histerese some e nada fica preso.
     */
    fun quantize(desired: Double, current: Int, pxPerFrame: Float, hysteresisPx: Float): Int {
        if (!desired.isFinite()) return current
        val extra = if (pxPerFrame > 0f && pxPerFrame.isFinite()) min(0.25, (hysteresisPx / pxPerFrame).toDouble()) else 0.0
        if (abs(desired - current) <= 0.5 + extra) return current
        val clamped = desired.coerceIn(Int.MIN_VALUE.toDouble(), Int.MAX_VALUE.toDouble())
        return floor(clamped + 0.5).toInt()
    }

    /** O cabeçote como ímã do arrasto; NONE quando ele está no instante de origem do losango. */
    fun playheadMagnet(playhead: Int, origin: Int): Int = if (playhead == origin) Snap.NONE else playhead
}
