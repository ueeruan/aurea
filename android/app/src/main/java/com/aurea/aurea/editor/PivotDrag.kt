package com.aurea.aurea.editor

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.aurea.aurea.engine.LayerDetail
import com.aurea.aurea.state.EditorStore
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.sin

/**
 * MOVER O PIVÔ SEM A IMAGEM PULAR. O motor desenha a camada com
 * `posição · R · S · (p − âncora)` (Renderer.cpp `layer_matrix` no 2D,
 * `layer_matrix_3d` com R = Rz·Ry·Rx no 3D). Mudar só a âncora arrasta a
 * imagem; o jeito padrão (AE) é mover o PIVÔ: a posição anda Δ e a âncora
 * anda `(R·S)⁻¹·Δ` — a conta dá o mesmo ponto para todo pixel da camada.
 */
internal object PivotMath {

    /**
     * Δâncora (espaço da camada, XYZ) que compensa a posição andar
     * ([dx], [dy], 0) no espaço do pai. `null` quando a escala zera um eixo
     * (não há inversa: a camada está achatada).
     */
    fun anchorDelta(
        dx: Float,
        dy: Float,
        rotationDeg: FloatArray,
        scale: FloatArray,
        threeD: Boolean,
        depthFollowsWidth: Boolean,
    ): FloatArray? {
        val s = effectiveScale(scale, threeD, depthFollowsWidth)
        // Rᵀ·Δ (R é ortonormal), depois ÷ S.
        val r = rotation(rotationDeg, threeD)
        val lx = r[0] * dx + r[3] * dy
        val ly = r[1] * dx + r[4] * dy
        val lz = r[2] * dx + r[5] * dy
        val out = floatArrayOf(lx, ly, lz)
        for (i in 0..2) {
            if (abs(out[i]) < 1e-9f) { out[i] = 0f; continue }
            if (abs(s[i]) < 1e-6f) return null
            out[i] /= s[i]
        }
        if (!threeD) out[2] = 0f
        return out
    }

    /** Onde o ponto [p] da camada cai no espaço do pai (a conta do motor). */
    fun place(
        position: FloatArray,
        rotationDeg: FloatArray,
        scale: FloatArray,
        anchor: FloatArray,
        threeD: Boolean,
        depthFollowsWidth: Boolean,
        p: FloatArray,
    ): FloatArray {
        val s = effectiveScale(scale, threeD, depthFollowsWidth)
        val r = rotation(rotationDeg, threeD)
        val vx = (p[0] - anchor[0]) * s[0]
        val vy = (p[1] - anchor[1]) * s[1]
        val vz = if (threeD) (p[2] - anchor[2]) * s[2] else 0f
        val pz = if (threeD) position[2] else 0f
        return floatArrayOf(
            position[0] + r[0] * vx + r[1] * vy + r[2] * vz,
            position[1] + r[3] * vx + r[4] * vy + r[5] * vz,
            pz + r[6] * vx + r[7] * vy + r[8] * vz,
        )
    }

    private fun effectiveScale(scale: FloatArray, threeD: Boolean, depthFollowsWidth: Boolean): FloatArray {
        val sz = if (!threeD) 1f else if (depthFollowsWidth) scale[2] * scale[0] else scale[2]
        return floatArrayOf(scale[0], scale[1], sz)
    }

    /** Matriz 3×3 por linhas: 2D = só Rz; 3D = Rz·Ry·Rx (`Quat::from_euler_zyx`). */
    private fun rotation(deg: FloatArray, threeD: Boolean): FloatArray {
        val z = Math.toRadians(deg[2].toDouble())
        val x = if (threeD) Math.toRadians(deg[0].toDouble()) else 0.0
        val y = if (threeD) Math.toRadians(deg[1].toDouble()) else 0.0
        val cx = cos(x); val sx = sin(x)
        val cy = cos(y); val sy = sin(y)
        val cz = cos(z); val sz = sin(z)
        return floatArrayOf(
            (cz * cy).toFloat(), (cz * sy * sx - sz * cx).toFloat(), (cz * sy * cx + sz * sx).toFloat(),
            (sz * cy).toFloat(), (sz * sy * sx + cz * cx).toFloat(), (sz * sy * cx - cz * sx).toFloat(),
            (-sy).toFloat(), (cy * sx).toFloat(), (cy * cx).toFloat(),
        )
    }
}

/**
 * A face Pivô do painel Transformar está aberta: o arrasto no palco move o
 * PIVÔ (não a camada). O painel liga ao compor e desliga ao sair.
 */
internal object PivotStage {
    var active by mutableStateOf(false)
}

/**
 * Um arrasto do pivô: guarda posição/âncora/giro/escala do COMEÇO e escreve
 * valores absolutos (nada acumula por evento). [moveTo] recebe onde o pivô
 * deve ficar, em px da composição.
 */
internal class PivotDragSession(private val store: EditorStore, d: LayerDetail, pivotCompX: Float, pivotCompY: Float) {
    private val detail = d
    private val pos0 = FloatArray(3) { d.position.getOrElse(it) { 0f } }
    private val anchor0 = FloatArray(3) { d.anchor.getOrElse(it) { 0f } }
    private val rot = FloatArray(3) { d.rotation.getOrElse(it) { 0f } }
    private val scale = FloatArray(3) { d.scale.getOrElse(it) { 1f } }
    val threeD = d.kind in 8..10 || d.flags and com.aurea.aurea.engine.PodLayout.FLAG_THREE_D != 0
    private val depthFollowsWidth = d.kind != 8 && d.kind != 9
    val startX = pivotCompX
    val startY = pivotCompY
    private val p0 = FloatArray(2).also { d.compToParent(pivotCompX, pivotCompY, it) }
    private val pt = FloatArray(2)

    fun moveTo(compX: Float, compY: Float) {
        detail.compToParent(compX, compY, pt)
        val dx = pt[0] - p0[0]
        val dy = pt[1] - p0[1]
        val da = PivotMath.anchorDelta(dx, dy, rot, scale, threeD, depthFollowsWidth) ?: return
        store.setPivot(
            floatArrayOf(anchor0[0] + da[0], anchor0[1] + da[1], anchor0[2] + da[2]),
            floatArrayOf(pos0[0] + dx, pos0[1] + dy, pos0[2]),
            detail.id,
        )
    }
}
