package com.aurea.aurea.editor

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.unit.dp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import kotlin.math.atan2
import kotlin.math.hypot

/** Floats por osso de `Engine::query_model_bones`. */
internal const val BONE_FLOATS = 10

/**
 * POSE DO RIG 3D na Cena 3D (0.1.8, "como no Blender"). Com o modo de pose
 * ligado, a cena mostra as juntas e os ossos do modelo:
 *  - tocar numa junta escolhe o osso; tocar no vazio solta (o dedo volta a
 *    orbitar a cena);
 *  - com um osso escolhido, arrastar gira o osso como uma trackball e o giro
 *    de dois dedos gira em volta do olhar;
 *  - o keyframe é marcado pelos botões da Cena 3D (osso ou todos). Osso que já
 *    tem keyframe grava keyframe no cabeçote ao girar (o motor decide).
 * Cada gesto = UM passo de desfazer.
 */
internal fun rigPoseActive(store: EditorStore): Boolean =
    store.sceneEditor && store.rigPoseMode && store.selection.size == 1 && store.poseBones != null

internal fun DrawScope.drawRigPoseOverlay(store: EditorStore, m: StageMapper) {
    val b = store.poseBones ?: return
    val chosen = store.poseBone
    val count = b.size / BONE_FLOATS
    val bone = Color.White.copy(alpha = .85f)
    // Ossos: do pai até a junta.
    for (i in 0 until count) {
        val o = i * BONE_FLOATS
        val parent = b[o + 1].toInt()
        if (parent < 0 || b[o + 9] < .5f || b[parent * BONE_FLOATS + 9] < .5f) continue
        val p = parent * BONE_FLOATS
        val a = Offset(m.sx(b[p + 7]), m.sy(b[p + 8]))
        val c = Offset(m.sx(b[o + 7]), m.sy(b[o + 8]))
        val on = b[o].toInt() == chosen || b[p].toInt() == chosen
        drawLine(ShellColors.OutlineUnder, a, c, 4.dp.toPx(), StrokeCap.Round)
        drawLine(if (on) AureaColors.Accent else bone, a, c, 2.dp.toPx(), StrokeCap.Round)
    }
    // Juntas: a escolhida maior e acesa; anel = keyframe no cabeçote.
    for (i in 0 until count) {
        val o = i * BONE_FLOATS
        if (b[o + 9] < .5f) continue
        val c = Offset(m.sx(b[o + 7]), m.sy(b[o + 8]))
        val on = b[o].toInt() == chosen
        val r = (if (on) 7.dp else 4.5.dp).toPx()
        drawCircle(ShellColors.OutlineUnder, r + 1.5.dp.toPx(), c)
        drawCircle(if (on) AureaColors.Accent else Color.White, r, c)
        if (b[o + 6] > .5f) drawCircle(AureaColors.Accent, r + 4.dp.toPx(), c, style = Stroke(2.dp.toPx()))
    }
}

/** Junta mais perto do dedo (até [reach] px do palco): o NÓ do osso, ou −1. */
internal fun pickPoseBone(store: EditorStore, m: StageMapper, x: Float, y: Float, reach: Float): Int {
    val b = store.poseBones ?: return -1
    var best = reach
    var hit = -1
    for (i in 0 until b.size / BONE_FLOATS) {
        val o = i * BONE_FLOATS
        if (b[o + 9] < .5f) continue
        val d = hypot(x - m.sx(b[o + 7]), y - m.sy(b[o + 8]))
        if (d < best) { best = d; hit = b[o].toInt() }
    }
    return hit
}

/** Gesto no modo de pose (só começa com uma junta perto do dedo ou um osso escolhido). */
internal suspend fun AwaitPointerEventScope.rigPoseGesture(store: EditorStore, m: StageMapper, down: PointerInputChange, slop: Float) {
    down.consume()
    val near = pickPoseBone(store, m, down.position.x, down.position.y, 36.dp.toPx())
    if (near >= 0 && near != store.poseBone) store.choosePoseBone(near)
    var moved = false
    var began = false
    var last = down.position
    var angle0: Float? = null
    try {
        while (true) {
            val e = awaitPointerEvent()
            e.changes.forEach { it.consume() }
            val pressed = e.changes.filter { it.pressed }
            if (pressed.isEmpty()) break
            if (pressed.size >= 2) {
                val a = pressed[0].position
                val c = pressed[1].position
                val angle = Math.toDegrees(atan2(c.y - a.y, c.x - a.x).toDouble()).toFloat()
                val prev = angle0
                angle0 = angle
                moved = true
                if (prev == null) continue
                var twist = angle - prev
                while (twist > 180f) twist -= 360f
                while (twist < -180f) twist += 360f
                if (!began) { store.beginGesture("posar osso"); began = true }
                store.dragPoseBone(0f, 0f, twist)
                continue
            }
            if (angle0 != null) continue   // voltou a um dedo depois do giro: não vira arrasto
            val c = pressed.firstOrNull { it.id == down.id } ?: pressed[0]
            if (!moved && hypot(c.position.x - down.position.x, c.position.y - down.position.y) < slop) continue
            if (!moved) last = down.position
            moved = true
            val dx = (c.position.x - last.x) / m.fit
            val dy = (c.position.y - last.y) / m.fit
            last = c.position
            if (dx == 0f && dy == 0f) continue
            if (!began) { store.beginGesture("posar osso"); began = true }
            store.dragPoseBone(dx, dy, 0f)
        }
    } finally {
        if (began) { store.endGesture(); store.endPoseGesture() }
    }
    // Toque no vazio: solta o osso (o dedo volta a orbitar a cena).
    if (!moved && near < 0) store.choosePoseBone(-1)
}
