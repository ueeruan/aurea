package com.aurea.aurea.editor

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.unit.dp
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.editor.panels.ShapeEditState
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.LayerType
import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.min

/**
 * O PALCO NO EDITAR FORMA (Frente D, ref18): no lugar das alças de escala e
 * giro da camada, oito alças de TAMANHO (4 cantos + 4 lados) que mudam a
 * largura/altura da silhueta em volta do centro (o motor move a âncora junto)
 * e, no retângulo, a alça do RAIO (disco em destaque perto do canto de cima à
 * esquerda) que arredonda os cantos. Arrastar o corpo da forma continua
 * movendo a camada. Cada arrasto = um passo de desfazer.
 *
 * Índices: 0 TL, 1 TR, 2 BR, 3 BL, 4 cima, 5 direita, 6 baixo, 7 esquerda,
 * 8 raio.
 */
internal object ShapeEditStage {
    /** Alças na tela (x,y intercalados, 9 alças) desenhadas no último quadro. */
    val handles = FloatArray(18)
    var count = 0
    var grabbed = -1
}

private val SIGN_X = intArrayOf(-1, 1, 1, -1, 0, 1, 0, -1)
private val SIGN_Y = intArrayOf(-1, -1, 1, 1, -1, 0, 1, 0)

/** O palco está no modo de editar a silhueta da forma escolhida? */
internal fun shapeEditMode(store: EditorStore, ui: EditorUi): Boolean {
    if (ui.panel != EditorPanel.ShapeEdit || store.selection.size != 1) return false
    val d = store.detail ?: return false
    return d.kind == LayerType.Shape.kind && !store.isVectorLayer && !d.locked &&
        store.playhead >= d.startFrame && store.playhead < d.endFrame
}

/** Desenha as alças a partir dos cantos da camada na tela ([StageMapper.corners]). */
internal fun DrawScope.drawShapeEditHandles(store: EditorStore, m: StageMapper) {
    val d = store.detail ?: return
    val c = m.corners
    val h = ShapeEditStage.handles
    for (i in 0 until 4) { h[i * 2] = c[i * 2]; h[i * 2 + 1] = c[i * 2 + 1] }
    for (i in 0 until 4) {
        val j = (i + 1) % 4
        h[8 + i * 2] = (c[i * 2] + c[j * 2]) / 2
        h[8 + i * 2 + 1] = (c[i * 2 + 1] + c[j * 2 + 1]) / 2
    }
    ShapeEditStage.count = 8
    val type = d.shapeTypePoints and 0xFFFF
    val w = d.sourceWidth.toFloat()
    val hh = d.sourceHeight.toFloat()
    if (type == 0 && w > 0f && hh > 0f) {
        // Raio: o ponto (ρ, ρ) da camada, no mínimo a 24 dp do canto (senão a
        // alça some embaixo da alça do canto quando o raio é 0).
        val rho = min(d.shapeCorner, min(w, hh) / 2f)
        val ux = (c[2] - c[0]) / w
        val uy = (c[3] - c[1]) / w
        val vx = (c[6] - c[0]) / hh
        val vy = (c[7] - c[1]) / hh
        var px = ux * rho + vx * rho
        var py = uy * rho + vy * rho
        val minD = 24.dp.toPx()
        val dist = hypot(px, py)
        if (dist < minD) {
            val dx = ux + vx
            val dy = uy + vy
            val l = hypot(dx, dy)
            if (l > 1e-6f) { px = dx / l * minD; py = dy / l * minD }
        }
        h[16] = c[0] + px
        h[17] = c[1] + py
        ShapeEditStage.count = 9
    }
    val grabbed = ShapeEditStage.grabbed
    val under = ShellColors.OutlineUnder
    for (i in 0 until 8) {
        val r = (if (grabbed == i) 7.dp else if (i < 4) 6.dp else 5.dp).toPx()
        val o = Offset(h[i * 2], h[i * 2 + 1])
        drawCircle(under, r + 1.5.dp.toPx(), o)
        drawCircle(Color.White, r, o)
        drawCircle(AureaColors.Accent, r, o, style = m.ring15)
    }
    if (ShapeEditStage.count == 9) {
        val o = Offset(h[16], h[17])
        val r = (if (grabbed == 8) 8.dp else 7.dp).toPx()
        drawCircle(under, r + 1.5.dp.toPx(), o)
        drawCircle(AureaColors.Accent, r, o)
        drawCircle(Color.White, r * 0.35f, o)
    }
}

/** Alça sob o dedo (a mais próxima dentro de [reach]); o raio ganha empate. */
internal fun pickShapeHandle(x: Float, y: Float, reach: Float): Int {
    val h = ShapeEditStage.handles
    var best = reach
    var hit = -1
    for (i in ShapeEditStage.count - 1 downTo 0) {
        val dd = hypot(x - h[i * 2], y - h[i * 2 + 1])
        if (dd < best - 0.5f) { best = dd; hit = i }
    }
    return hit
}

/** O arrasto de uma alça do Editar forma, até o dedo subir. */
internal suspend fun AwaitPointerEventScope.shapeEditGesture(store: EditorStore, m: StageMapper, handle: Int, down: PointerInputChange) {
    val d = store.detail
    val c = FloatArray(8)
    if (d == null || !m.valid || m.fit <= 0f || !LayerGeometry.corners(d, c)) return
    val w0 = d.sourceWidth.toFloat().coerceAtLeast(1f)
    val h0 = d.sourceHeight.toFloat().coerceAtLeast(1f)
    val r0 = d.shapeCorner
    val lenU = hypot(c[2] - c[0], c[3] - c[1])
    val lenV = hypot(c[6] - c[0], c[7] - c[1])
    if (lenU < 1e-4f || lenV < 1e-4f) return
    val ux = (c[2] - c[0]) / lenU
    val uy = (c[3] - c[1]) / lenU
    val vx = (c[6] - c[0]) / lenV
    val vy = (c[7] - c[1]) / lenV
    val ku = lenU / w0            // px da composição por px da forma
    val kv = lenV / h0
    val linked = ShapeEditState.linked
    ShapeEditStage.grabbed = handle
    store.beginGesture(if (handle == 8) "raio da forma" else "tamanho da forma")
    down.consume()
    try {
        while (true) {
            val e = awaitPointerEvent()
            val ch = e.changes.firstOrNull { it.id == down.id } ?: break
            ch.consume()
            if (!ch.pressed) break
            val dx = (ch.position.x - down.position.x) / m.fit
            val dy = (ch.position.y - down.position.y) / m.fit
            val du = (dx * ux + dy * uy) / ku
            val dv = (dx * vx + dy * vy) / kv
            if (handle == 8) {
                val r = (r0 + (du + dv) / 2f).coerceIn(0f, min(w0, h0) / 2f)
                store.setShapeParam(1, r)
                continue
            }
            val sx = SIGN_X[handle]
            val sy = SIGN_Y[handle]
            // Em volta do centro: cada lado anda o que o dedo andou, o oposto espelha.
            var w = if (sx != 0) w0 + 2f * sx * du else w0
            var h = if (sy != 0) h0 + 2f * sy * dv else h0
            if (linked) {
                val kx = w / w0
                val ky = h / h0
                val k = when {
                    sx == 0 -> ky
                    sy == 0 -> kx
                    abs(kx - 1f) >= abs(ky - 1f) -> kx
                    else -> ky
                }
                w = w0 * k
                h = h0 * k
            }
            w = w.coerceIn(1f, 16384f)
            h = h.coerceIn(1f, 16384f)
            if (sx != 0 || linked) store.setShapeParam(5, w)
            if (sy != 0 || linked) store.setShapeParam(6, h)
        }
    } finally {
        ShapeEditStage.grabbed = -1
        store.endGesture()
    }
}
