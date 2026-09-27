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
