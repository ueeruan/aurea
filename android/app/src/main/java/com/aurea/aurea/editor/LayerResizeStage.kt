package com.aurea.aurea.editor

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.unit.dp
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.ui.theme.AureaColors
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.min

/**
 * ALÇAS BRANCAS DE ESTICAR (pedido da beta 11/10): camada 2D escolhida e
 * grande o bastante na tela ganha 8 discos brancos — 4 cantos escalam por
 * igual, 4 lados esticam só aquele eixo. A conta é em volta da ÂNCORA (onde
 * fica a posição), igual à pinça; cada arrasto = um passo de desfazer e grava
 * chave com o auto-key, pelo mesmo `setTransform2` da pinça.
 *
 * Camada pequena não ganha alças: o disco ficava em cima do corpo e roubava o
 * arrasto de mover (por isso elas tinham saído). Ampliar a vista mostra.
 *
 * Índices: 0 TL, 1 TR, 2 BR, 3 BL, 4 cima, 5 direita, 6 baixo, 7 esquerda.
 */
internal object LayerResizeStage {
    /** Alças na tela (x,y intercalados) desenhadas no último quadro; 0 = sem alças. */
    val handles = FloatArray(16)
    var count = 0
    var grabbed = -1
}

/** Menor lado da camada na tela para ganhar alças (abaixo disso, só a pinça). */
private val RESIZE_MIN_SIDE = 96.dp

/** Desenha as alças a partir de [StageMapper.corners]; chamada só para camada 2D, 1 escolhida e destravada. */
internal fun DrawScope.drawLayerResizeHandles(m: StageMapper) {
    val c = m.corners
    val lenU = hypot(c[2] - c[0], c[3] - c[1])
    val lenV = hypot(c[6] - c[0], c[7] - c[1])
    if (min(lenU, lenV) < RESIZE_MIN_SIDE.toPx()) return
    val h = LayerResizeStage.handles
    for (i in 0 until 4) { h[i * 2] = c[i * 2]; h[i * 2 + 1] = c[i * 2 + 1] }
    for (i in 0 until 4) {
        val j = (i + 1) % 4
        h[8 + i * 2] = (c[i * 2] + c[j * 2]) / 2
        h[8 + i * 2 + 1] = (c[i * 2 + 1] + c[j * 2 + 1]) / 2
    }
    LayerResizeStage.count = 8
    val grabbed = LayerResizeStage.grabbed
    val under = ShellColors.OutlineUnder
    for (i in 0 until 8) {
        val r = (if (grabbed == i) 7.dp else if (i < 4) 6.dp else 5.dp).toPx()
        val o = Offset(h[i * 2], h[i * 2 + 1])
        drawCircle(under, r + 1.5.dp.toPx(), o)
        drawCircle(Color.White, r, o)
        drawCircle(AureaColors.Accent, r, o, style = m.ring15)
    }
}

/** Alça sob o dedo (a mais próxima dentro de [reach]), ou −1. */
internal fun pickLayerResizeHandle(x: Float, y: Float, reach: Float): Int {
    val h = LayerResizeStage.handles
    var best = reach
    var hit = -1
    for (i in 0 until LayerResizeStage.count) {
        val dd = hypot(x - h[i * 2], y - h[i * 2 + 1])
        if (dd < best) { best = dd; hit = i }
    }
    return hit
}

/**
 * O arrasto de uma alça de esticar, até o dedo subir. Canto: fator uniforme
 * pela projeção do dedo na reta âncora→canto. Lado: fator só daquele eixo
 * pela projeção no eixo da camada (gira junto com ela).
 */
internal suspend fun AwaitPointerEventScope.layerResizeGesture(store: EditorStore, m: StageMapper, handle: Int, down: PointerInputChange) {
    val d = store.detail
    val c = FloatArray(8)
    if (d == null || !m.valid || m.fit <= 0f || !LayerGeometry.corners(d, c)) return
    val a = FloatArray(2)
    d.parentToComp(d.position[0], d.position[1], a)
    // A alça na composição.
    val hx: Float
    val hy: Float
    if (handle < 4) { hx = c[handle * 2]; hy = c[handle * 2 + 1] }
    else {
        val i = handle - 4
        val j = (i + 1) % 4
        hx = (c[i * 2] + c[j * 2]) / 2; hy = (c[i * 2 + 1] + c[j * 2 + 1]) / 2
    }
    // Direção da conta: canto = âncora→canto; lado = o eixo da camada daquele lado.
    var ex: Float
    var ey: Float
    val stretchX = handle == 5 || handle == 7
    val stretchY = handle == 4 || handle == 6
    if (stretchX) { ex = c[2] - c[0]; ey = c[3] - c[1] }
    else if (stretchY) { ex = c[6] - c[0]; ey = c[7] - c[1] }
    else { ex = hx - a[0]; ey = hy - a[1] }
    val el = hypot(ex, ey)
    if (el < 1e-4f) return
    ex /= el; ey /= el
    // Distância da âncora até a alça ao longo da direção: perto de zero (âncora
    // em cima da alça) não há o que esticar.
    val base = (hx - a[0]) * ex + (hy - a[1]) * ey
    if (abs(base) * m.fit < 8.dp.toPx()) return
    val sx0 = d.scale[0]
    val sy0 = d.scale[1]
    val sz0 = d.scale.getOrElse(2) { 1f }
    LayerResizeStage.grabbed = handle
    if (store.playing) store.pause()
    store.beginGesture(if (stretchX || stretchY) "esticar camada" else "escalar camada")
    down.consume()
    try {
        while (true) {
            val e = awaitPointerEvent()
            val ch = e.changes.firstOrNull { it.id == down.id } ?: break
            ch.consume()
            if (!ch.pressed) break
            val fx = hx + (ch.position.x - down.position.x) / m.fit
            val fy = hy + (ch.position.y - down.position.y) / m.fit
            val f = AureaEngine.clampPinchFactor(((fx - a[0]) * ex + (fy - a[1]) * ey) / base, sx0, sy0, sz0, false)
            when {
                stretchX -> store.setTransform(TrackProperty.SCALE_X, sx0 * f)
                stretchY -> store.setTransform(TrackProperty.SCALE_Y, sy0 * f)
                else -> store.setTransform2(TrackProperty.SCALE_X, sx0 * f, TrackProperty.SCALE_Y, sy0 * f)
            }
        }
    } finally {
        LayerResizeStage.grabbed = -1
        store.endGesture()
    }
}
