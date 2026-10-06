package com.aurea.aurea.state

/**
 * Como um gesto de transform (palco, almofada, gizmo, dedo na cena 3D)
 * escreve no motor. UMA regra para a timeline e para a cena 3D: a cena
 * "seca" anima pelo dedo, e ali "deslocar a curva inteira" nunca marcava
 * keyframe nenhum — o nulo 3D arrastado no quadro 30 mudava também a pose
 * do quadro 0.
 */
internal enum class TransformWrite {
    /** Trilha animada com Auto-Key ligado: keyframe no cabeçote; os outros quadros ficam. */
    Keyframe,
    /** Desloca a animação inteira (Auto-Key desligado) ou o valor parado na cena 3D. */
    Layout,
    /** Trilha parada fora da cena: muda o valor fixo. */
    Static,
}

/** [animated] = a trilha da propriedade já tem keyframe (o losango foi cravado). */
internal fun transformWrite(sceneEditor: Boolean, autoKey: Boolean, animated: Boolean): TransformWrite = when {
    !autoKey -> TransformWrite.Layout
    animated -> TransformWrite.Keyframe
    sceneEditor -> TransformWrite.Layout
    else -> TransformWrite.Static
}

/** Bits de `KeyframePayload::onlyIfChanged` (Command.hpp, `kAutoKey*`). */
internal const val AUTO_KEY_ONLY_IF_CHANGED = 1
internal const val AUTO_KEY_AT_PLAYHEAD = 2
internal const val AUTO_KEY_STATIC_WHEN_UNANIMATED = 4

/**
 * O gesto de transform com Auto-Key (palco, almofada, gizmo). O MOTOR decide
 * com o estado vivo: trilha com keyframe ganha chave no quadro que a prévia
 * MOSTRA; trilha parada muda o valor fixo. Antes a UI escolhia pelo detalhe
 * que tinha lido — atrasado, o arrasto de um texto animado gravava o valor
 * parado (que a animação esconde) e a camada não andava.
 * [wholeGroup]: o grupo XYZ do 3D animado — todo eixo ganha chave, mesmo o
 * que ainda não tinha (decisão do dono, build 2125).
 */
internal fun gestureKeyFlags(wholeGroup: Boolean): Int =
    AUTO_KEY_ONLY_IF_CHANGED or AUTO_KEY_AT_PLAYHEAD or (if (wholeGroup) 0 else AUTO_KEY_STATIC_WHEN_UNANIMATED)

/** Animated 3D vectors are one keyframe group, even if only one axis was animated. */
internal fun transformKeyGroup(property: Int, threeD: Boolean, animatedMask: Int): IntArray {
    if (!threeD || property !in 0..11) return intArrayOf()
    val base = property / 3 * 3
    return if (animatedMask and (7 shl base) != 0) intArrayOf(base, base + 1, base + 2) else intArrayOf()
}

/** Absolute scale from the gesture-start snapshot; never accumulate a ratio per event. */
internal fun linkedScale(original: FloatArray, axis: Int, value: Float): FloatArray {
    require(original.size == 3 && axis in 0..2)
    val from = original[axis]
    return FloatArray(3) { if (it == axis || from == 0f) value else original[it] * (value / from) }
}
