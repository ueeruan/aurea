package com.aurea.aurea.editor

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.unit.dp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.Shape3DCatalog
import com.aurea.aurea.ui.theme.AureaColors
import kotlin.math.atan2
import kotlin.math.hypot

/**
 * PARTES DA FORMA 3D no palco. Com uma parte escolhida (chips "Partes" do
 * painel 3D), o dedo mexe só nela:
 *  - tocar perto do centro de outra parte a escolhe; tocar no vazio volta à
 *    forma inteira;
 *  - arrastar move a parte no plano que a câmera vê de frente;
 *  - pinça com dois dedos escala (uniforme) e gira a parte na tela.
 * Canal com keyframe grava keyframe no cabeçote (o motor decide). Cada gesto
 * = UM passo de desfazer. Fora disso o palco fica limpo: os pontos das partes
 * só aparecem no modo de parte.
 */
internal fun shapePartActive(store: EditorStore): Boolean {
    if (store.selection.size != 1 || store.sceneEditor) return false
    val d = store.detail ?: return false
    return !d.locked && store.shapePartOf(store.primary) >= 0
}

internal fun DrawScope.drawShapePartOverlay(store: EditorStore, m: StageMapper) {
    val p = store.shapeParts ?: return
    val selected = store.shapePartOf(store.primary)
    val f = Shape3DCatalog.PART_FLOATS
    val under = ShellColors.OutlineUnder
    for (i in 0 until p.size / f) {
        val o = i * f
        if (p[o + 13] < 0.5f) continue
        val c = Offset(m.sx(p[o + 11]), m.sy(p[o + 12]))
        val chosen = i == selected
        val r = (if (chosen) 7.dp else 4.5.dp).toPx()
        drawCircle(under, r + 1.5.dp.toPx(), c)
        drawCircle(if (chosen) AureaColors.Accent else Color.White, r, c)
        // Keyframe no cabeçote: anel em volta, como as juntas do rig.
        if (p[o + 10] > 0.5f) drawCircle(AureaColors.Accent, r + 4.dp.toPx(), c, style = Stroke(2.dp.toPx()))
    }
}

/** Parte cujo centro está mais perto do dedo (até [reach] px do palco); −1 = nenhuma. */
private fun pickPart(store: EditorStore, m: StageMapper, x: Float, y: Float, reach: Float): Int {
    val p = store.shapeParts ?: return -1
    val f = Shape3DCatalog.PART_FLOATS
    var best = reach
    var hit = -1
    for (i in 0 until p.size / f) {
        val o = i * f
        if (p[o + 13] < 0.5f) continue
        val d = hypot(x - m.sx(p[o + 11]), y - m.sy(p[o + 12]))
        if (d < best) { best = d; hit = i }
    }
    return hit
}

/** Gesto no modo de parte. Chamado com o primeiro toque; consome até o último dedo subir. */
internal suspend fun AwaitPointerEventScope.shapePartGesture(store: EditorStore, m: StageMapper, down: PointerInputChange, slop: Float) {
    down.consume()
    var moved = false
    var began = false
    var last = down.position
    // Pinça: base congelada quando o 2º dedo entra.
    var pinchStart: FloatArray? = null
    var dist0 = 0f
    var angle0 = 0f
    try {
        while (true) {
            val e = awaitPointerEvent()
            e.changes.forEach { it.consume() }
            val pressed = e.changes.filter { it.pressed }
            if (pressed.isEmpty()) break
            if (pressed.size >= 2) {
                val a = pressed[0].position
                val b = pressed[1].position
                val dist = hypot(b.x - a.x, b.y - a.y)
                val angle = atan2(b.y - a.y, b.x - a.x)
                if (pinchStart == null) {
                    pinchStart = store.shapePartValues(store.shapePartOf(store.primary)) ?: continue
                    dist0 = dist.coerceAtLeast(1f)
                    angle0 = angle
                    moved = true
                    continue
                }
                if (!began) { store.beginGesture("pinça da parte"); began = true }
                var twist = Math.toDegrees((angle - angle0).toDouble()).toFloat()
                while (twist > 180f) twist -= 360f
                while (twist < -180f) twist += 360f
                store.shapePartPinch(pinchStart, dist / dist0, twist)
                continue
            }
            // Voltou a um dedo depois da pinça: não vira arrasto no meio do gesto.
            if (pinchStart != null) continue
            val c = pressed.firstOrNull { it.id == down.id } ?: pressed[0]
            if (!moved && hypot(c.position.x - down.position.x, c.position.y - down.position.y) < slop) continue
            if (!moved) last = down.position
            moved = true
            val dx = (c.position.x - last.x) / m.fit
            val dy = (c.position.y - last.y) / m.fit
            last = c.position
            if (dx == 0f && dy == 0f) continue
            if (!began) { store.beginGesture("mover parte"); began = true }
            store.shapePartDragScreen(dx, dy)
        }
    } finally {
        if (began) store.endGesture()
    }
    if (moved) return
    // Toque: outra parte = escolhe; vazio = volta à forma inteira.
    store.chooseShapePart(pickPart(store, m, down.position.x, down.position.y, 36.dp.toPx()))
}
