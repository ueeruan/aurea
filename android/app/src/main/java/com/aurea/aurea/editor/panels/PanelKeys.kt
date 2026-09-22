package com.aurea.aurea.editor.panels

import com.aurea.aurea.engine.EffectParam
import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.LayerDetail
import com.aurea.aurea.engine.ParamType
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.KeyframeLook
import kotlin.math.max

// Contas de keyframe dos painéis — tudo lido do store, nada guardado.

/** Keyframes da camada principal (tempo LOCAL). */
internal fun EditorStore.primaryKeys(): List<KeyframeRow> = primary?.let { keyframes[it] } ?: emptyList()

/** A mesma trilha? Transform = mesma propriedade; efeito = mesmo efeito e mesmo `param*4+componente`. */
internal fun KeyframeRow.sameTrack(o: KeyframeRow): Boolean =
    property == o.property && (property != TrackProperty.EFFECT_PARAM || (effectIndex == o.effectIndex && paramIndex == o.paramIndex))

/**
 * Trilhas IRMÃS: as que andam juntas no mesmo instante — X/Y de posição, escala
 * e âncora; os componentes de um mesmo parâmetro de efeito. A curva vale para o
 * grupo (como na A.01, onde posição era UMA propriedade 2D).
 */
internal fun KeyframeRow.sameGroup(o: KeyframeRow): Boolean {
    if (property == TrackProperty.EFFECT_PARAM || o.property == TrackProperty.EFFECT_PARAM) {
        return property == o.property && effectIndex == o.effectIndex && paramIndex / 4 == o.paramIndex / 4
    }
    return transformGroup(property) == transformGroup(o.property)
}

private fun transformGroup(p: Int): Int = when (p) {
    TrackProperty.POSITION_X, TrackProperty.POSITION_Y, TrackProperty.POSITION_Z -> 0
    TrackProperty.SCALE_X, TrackProperty.SCALE_Y, TrackProperty.SCALE_Z -> 1
    TrackProperty.ROTATION_X, TrackProperty.ROTATION_Y, TrackProperty.ROTATION_Z -> 2
    TrackProperty.ANCHOR_X, TrackProperty.ANCHOR_Y, TrackProperty.ANCHOR_Z -> 3
    TrackProperty.SKEW_X, TrackProperty.SKEW_Y -> 4
    else -> 100 + p
}

/** As marcas de UMA trilha, em ordem de tempo. */
internal fun List<KeyframeRow>.track(of: KeyframeRow): List<KeyframeRow> = filter { it.sameTrack(of) }.sortedBy { it.time }

/** As marcas da trilha de uma propriedade de transform. */
internal fun List<KeyframeRow>.transformTrack(property: Int): List<KeyframeRow> =
    filter { it.property == property }.sortedBy { it.time }

/** As marcas da trilha de um componente de parâmetro de efeito. */
internal fun List<KeyframeRow>.effectTrack(effectId: Int, param: Int, component: Int): List<KeyframeRow> =
    filter { it.property == TrackProperty.EFFECT_PARAM && it.effectIndex == effectId && it.paramIndex == param * 4 + component }
        .sortedBy { it.time }

/**
 * A MARCA QUE ABRE O TRECHO sob o cabeçote (a curva de um keyframe é a do trecho
 * que SAI dele). Fora dos trechos: o primeiro (antes) ou o último (depois).
 */
internal fun List<KeyframeRow>.segmentStart(localPlayhead: Int): KeyframeRow? {
    if (size < 2) return null
    val i = indexOfLast { it.time <= localPlayhead }.coerceAtLeast(0).coerceAtMost(size - 2)
    return this[i]
}

/** Losango de um parâmetro de efeito ([component] nulo = qualquer componente). */
internal fun effectLook(store: EditorStore, effectId: Int, param: Int, component: Int? = null): KeyframeLook {
    val t = store.detail?.localPlayhead ?: return KeyframeLook.None
    var animated = false
    val keys = store.primaryKeys()
    // Laço por índice: roda a cada quadro da reprodução, sem criar iterador.
    for (i in keys.indices) {
        val k = keys[i]
        if (k.property != TrackProperty.EFFECT_PARAM || k.effectIndex != effectId || k.paramIndex / 4 != param) continue
        if (component != null && k.paramIndex % 4 != component) continue
        if (k.time == t) return KeyframeLook.KeyHere
        animated = true
    }
    return if (animated) KeyframeLook.Animated else KeyframeLook.None
}

/**
 * Losango de um grupo de transform. "Marca aqui" só quando TODAS têm marca no
 * cabeçote — é exatamente quando o toque vai APAGAR (`toggleTransformKeyframe`).
 */
internal fun transformLook(d: LayerDetail?, props: IntArray): KeyframeLook {
    d ?: return KeyframeLook.None
    if (props.isNotEmpty() && props.all { d.hasKeyAtPlayhead(it) }) return KeyframeLook.KeyHere
    return if (props.any { d.isAnimated(it) }) KeyframeLook.Animated else KeyframeLook.None
}

/** O parâmetro de efeito ATUAL no store (lido no toque, nunca guardado). */
internal fun EditorStore.paramOf(effectId: Int, index: Int): EffectParam? {
    val list = effectParams[effectId] ?: return null
    for (i in list.indices) if (list[i].index == index) return list[i]
    return null
}

/**
 * Escreve vários componentes de um parâmetro. `setEffectParam` monta o vetor a
 * partir do `EffectParam` que recebe; escrevendo componente a componente com o
 * MESMO objeto, o segundo desfaria o primeiro. Aqui cada passo recebe o vetor já
 * atualizado. Componentes iguais são pulados quando o parâmetro não anima.
 */
internal fun EditorStore.writeParamVector(effectId: Int, p: EffectParam, values: FloatArray) {
    val n = max(1, ParamType.componentCount(p.type))
    var cur = p
    for (c in 0 until n) {
        val v = values.getOrElse(c) { cur.value[c] }
        if (!p.animated && cur.value[c] == v) continue
        setEffectParam(effectId, cur, v, c)
        val nv = cur.value.copyOf().also { it[c] = v }
        cur = EffectParam(cur.index, cur.type, cur.flags, cur.min, cur.max, nv, cur.defaultValue, cur.label, cur.unit, cur.enumLabels, cur.animated)
    }
}
