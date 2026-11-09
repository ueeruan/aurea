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
 * Índices: 0 TL, 1 TR, 2 BR, 3 BL, 4 cima, 5 direita, 6 baixo, 7 esquerda;
 * de 8 em diante, as alças dos parâmetros da silhueta (0.5.0, como no Alight
 * Motion): raio, ponta, abertura… Onde cada uma fica e o que o arrasto muda é
 * do motor (`query_shape_param_handles`/`drag_shape_param_handle`).
 */
internal object ShapeEditStage {
    /** Alças na tela (x,y intercalados) desenhadas no último quadro. */
    val handles = FloatArray(24)
    var count = 0
    var grabbed = -1
    /** Parâmetro, estilo e posição real (px da forma) de cada alça de 8 em diante. */
    val params = IntArray(4)
    val styles = IntArray(4)
    val shapeX = FloatArray(4)
    val shapeY = FloatArray(4)
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
    val w = d.sourceWidth.toFloat()
    val hh = d.sourceHeight.toFloat()
    val raw = store.shapeParamHandles()
    if (raw != null && w > 0f && hh > 0f) {
        val ux = (c[2] - c[0]) / w
        val uy = (c[3] - c[1]) / w
        val vx = (c[6] - c[0]) / hh
        val vy = (c[7] - c[1]) / hh
        val minD = 24.dp.toPx()
        val n = min(raw.size / 6, 4)
        for (k in 0 until n) {
            val sx = raw[k * 6]
            val sy = raw[k * 6 + 1]
            ShapeEditStage.shapeX[k] = sx
            ShapeEditStage.shapeY[k] = sy
            ShapeEditStage.params[k] = raw[k * 6 + 2].toInt()
            ShapeEditStage.styles[k] = raw[k * 6 + 3].toInt()
            var px = c[0] + ux * (sx + w / 2f) + vx * (sy + hh / 2f)
            var py = c[1] + uy * (sx + w / 2f) + vy * (sy + hh / 2f)
            // Só o desenho se afasta das alças de tamanho; o arrasto parte da posição real.
            val pushX = ux * raw[k * 6 + 4] + vx * raw[k * 6 + 5]
            val pushY = uy * raw[k * 6 + 4] + vy * raw[k * 6 + 5]
            val pushLen = hypot(pushX, pushY)
            if (pushLen > 1e-6f) {
                for (i in 0 until 8) {
                    val gap = hypot(px - h[i * 2], py - h[i * 2 + 1])
                    if (gap < minD) {
                        px += pushX / pushLen * (minD - gap)
                        py += pushY / pushLen * (minD - gap)
                    }
                }
            }
            h[(8 + k) * 2] = px
            h[(8 + k) * 2 + 1] = py
        }
        ShapeEditStage.count = 8 + n
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
    for (i in 8 until ShapeEditStage.count) {
        val o = Offset(h[i * 2], h[i * 2 + 1])
        val r = (if (grabbed == i) 8.dp else 7.dp).toPx()
        drawCircle(under, r + 1.5.dp.toPx(), o)
        drawCircle(AureaColors.Accent, r, o)
        // Ângulo: miolo escuro em anel; os outros, ponto branco.
        if (ShapeEditStage.styles[i - 8] == 1) drawCircle(Color.White, r * 0.55f, o, style = m.ring15)
        else drawCircle(Color.White, r * 0.35f, o)
    }
}

/** Alça sob o dedo (a mais próxima dentro de [reach]); as de parâmetro ganham empate. */
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
    val lenU = hypot(c[2] - c[0], c[3] - c[1])
    val lenV = hypot(c[6] - c[0], c[7] - c[1])
    if (lenU < 1e-4f || lenV < 1e-4f) return
    val ux = (c[2] - c[0]) / lenU
    val uy = (c[3] - c[1]) / lenU
    val vx = (c[6] - c[0]) / lenV
    val vy = (c[7] - c[1]) / lenV
    val ku = lenU / w0            // px da composição por px da forma
    val kv = lenV / h0
    // Omega conteúdo (área 06, P0-6/P1-9): as chaves da barra da seção (salvas no
    // aparelho); a conta da borda/centro e da proporção é do motor.
    val keepRatio = ContentPrefs.lockAspect
    val fromCenter = ContentPrefs.fromCenter
    var lastU = 0f
    var lastV = 0f
    var sent = false
    ShapeEditStage.grabbed = handle
    val param = if (handle >= 8) ShapeEditStage.params[handle - 8] else -1
    val startX = if (handle >= 8) ShapeEditStage.shapeX[handle - 8] else 0f
    val startY = if (handle >= 8) ShapeEditStage.shapeY[handle - 8] else 0f
    store.beginGesture(if (handle >= 8) "parâmetro da forma" else "tamanho da forma")
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
            if (handle >= 8) {
                store.dragShapeParamHandle(param, startX + du, startY + dv, continuing = sent)
                sent = true
                continue
            }
            // O passo desde o último envio (px da forma, eixos da camada).
            store.resizeShapeHandle(handle, du - lastU, dv - lastV, keepRatio, fromCenter, continuing = sent)
            lastU = du
            lastV = dv
            sent = true
        }
    } finally {
        ShapeEditStage.grabbed = -1
        store.endGesture()
    }
}
