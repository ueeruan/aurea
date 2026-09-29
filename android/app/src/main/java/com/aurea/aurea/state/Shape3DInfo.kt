package com.aurea.aurea.state

import androidx.annotation.StringRes
import com.aurea.aurea.R

/**
 * Receita de uma FORMA 3D (motor: scene3d/Shape3D.hpp): a forma e, por parte,
 * a cor (RGBA sRGB) e se tem imagem. Lida de `AureaEngine.queryShape3d`.
 */
data class Shape3DInfo(val kind: Int, val colors: List<FloatArray>, val images: List<Boolean>) {
    val partCount: Int get() = colors.size

    fun sameAs(o: Shape3DInfo?): Boolean = o != null && o.kind == kind && o.images == images &&
        o.colors.size == colors.size && o.colors.indices.all { o.colors[it].contentEquals(colors[it]) }

    companion object {
        /** Floats do `queryShape3d`: forma, nº de partes e 5 por parte (até 8). */
        const val FIELDS = 2 + 5 * 8

        fun of(parts: Int, f: FloatArray): Shape3DInfo? {
            if (parts <= 0 || f.size < FIELDS) return null
            val n = parts.coerceAtMost(8)
            return Shape3DInfo(
                f[0].toInt(),
                List(n) { i -> floatArrayOf(f[2 + i * 5], f[3 + i * 5], f[4 + i * 5], f[5 + i * 5]) },
                List(n) { i -> f[6 + i * 5] > 0.5f },
            )
        }
    }
}

/** As 10 formas, na ordem do `Shape3DKind` do motor. */
object Shape3DCatalog {
    const val COUNT = 10
    /** Floats por parte do `query_shape3d_parts` (Engine::kShapePartFloats). */
    const val PART_FLOATS = 14

    @StringRes
    val names = intArrayOf(
        R.string.shape3d_cube, R.string.shape3d_sphere, R.string.shape3d_cylinder, R.string.shape3d_cone,
        R.string.shape3d_pyramid, R.string.shape3d_torus, R.string.shape3d_star, R.string.shape3d_heart,
        R.string.shape3d_capsule, R.string.shape3d_diamond,
    )

    /** Nome curto de cada parte (a mesma ordem das partes do motor). */
    fun partName(kind: Int, part: Int): Int = when (kind) {
        0 -> intArrayOf(R.string.shape3d_part_front, R.string.shape3d_part_back, R.string.shape3d_part_right,
            R.string.shape3d_part_left, R.string.shape3d_part_top, R.string.shape3d_part_bottom).getOrElse(part) { R.string.shape3d_part_n }
        1 -> intArrayOf(R.string.shape3d_part_upper, R.string.shape3d_part_lower).getOrElse(part) { R.string.shape3d_part_n }
        2 -> intArrayOf(R.string.shape3d_part_top, R.string.shape3d_part_bottom, R.string.shape3d_part_side).getOrElse(part) { R.string.shape3d_part_n }
        3 -> intArrayOf(R.string.shape3d_part_base, R.string.shape3d_part_side).getOrElse(part) { R.string.shape3d_part_n }
        4 -> intArrayOf(R.string.shape3d_part_base, R.string.shape3d_part_front, R.string.shape3d_part_right,
            R.string.shape3d_part_back, R.string.shape3d_part_left).getOrElse(part) { R.string.shape3d_part_n }
        6 -> if (part == 5) R.string.shape3d_part_center else R.string.shape3d_part_tip
        7 -> intArrayOf(R.string.shape3d_part_left, R.string.shape3d_part_right).getOrElse(part) { R.string.shape3d_part_n }
        8 -> intArrayOf(R.string.shape3d_part_top, R.string.shape3d_part_body, R.string.shape3d_part_bottom).getOrElse(part) { R.string.shape3d_part_n }
        else -> R.string.shape3d_part_n   // toro (quartos) e diamante (facetas): "Parte N"
    }

    /** Partes numeradas (pontas da estrela, quartos do toro, facetas): o rótulo leva o número. */
    fun numbered(kind: Int, part: Int): Boolean = partName(kind, part) == R.string.shape3d_part_n ||
        (kind == 6 && part < 5)
}
