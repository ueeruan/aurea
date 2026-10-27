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

// =============================================================================
// AutoRig 3D (0.3.6 BETA): "Corrigir esqueleto" e os controladores IK no palco.
// Par do iOS: RigStage.swift (bloco "AutoRig 3D").
// =============================================================================

/** Corrigir esqueleto ligado na Cena 3D (as 17 juntas canônicas). */
internal fun rigFixActive(store: EditorStore): Boolean =
    store.sceneEditor && store.rigFixMode && store.selection.size == 1 && store.humanoidJoints != null

/** Junta arrastada agora (osso canônico + ponto em px do palco), para o desenho. */
private val fixDrag = androidx.compose.runtime.mutableStateOf<Triple<Int, Float, Float>?>(null)

internal fun DrawScope.drawRigFixOverlay(store: EditorStore, m: StageMapper) {
    val j = store.humanoidJoints ?: return
    val f = com.aurea.aurea.editor.panels.HUMANOID_JOINT_FLOATS
    val count = j.size / f
    val drag = fixDrag.value
    fun at(b: Int): Offset? {
        val o = b * f
        if (j[o] < 0f || j[o + 3] < .5f) return null
        if (drag != null && drag.first == b) return Offset(drag.second, drag.third)
        return Offset(m.sx(j[o + 1]), m.sy(j[o + 2]))
    }
    for (b in 0 until count) {
        val p = j[b * f + 7].toInt()
        if (p < 0) continue
        val a = at(p) ?: continue
        val c = at(b) ?: continue
        drawLine(ShellColors.OutlineUnder, a, c, 4.dp.toPx(), StrokeCap.Round)
        drawLine(Color.White.copy(alpha = .85f), a, c, 2.dp.toPx(), StrokeCap.Round)
    }
    for (b in 0 until count) {
        val c = at(b) ?: continue
        val on = drag?.first == b
        val r = (if (on) 8.dp else 5.5.dp).toPx()
        drawCircle(ShellColors.OutlineUnder, r + 1.5.dp.toPx(), c)
        drawCircle(if (on) AureaColors.Accent else Color(0xFFFFCC55), r, c)
    }
}

/** Junta canônica mais perto do dedo (até [reach] px), ou −1. */
internal fun pickFixJoint(store: EditorStore, m: StageMapper, x: Float, y: Float, reach: Float): Int {
    val j = store.humanoidJoints ?: return -1
    val f = com.aurea.aurea.editor.panels.HUMANOID_JOINT_FLOATS
    var best = reach
    var hit = -1
    for (b in 0 until j.size / f) {
        val o = b * f
        if (j[o] < 0f || j[o + 3] < .5f) continue
        val d = hypot(x - m.sx(j[o + 1]), y - m.sy(j[o + 2]))
        if (d < best) { best = d; hit = b }
    }
    return hit
}

/** Arrasta a junta; ao soltar, o motor refaz os pesos (assíncrono, com progresso). */
internal suspend fun AwaitPointerEventScope.rigFixGesture(store: EditorStore, m: StageMapper, down: PointerInputChange, slop: Float) {
    down.consume()
    val bone = pickFixJoint(store, m, down.position.x, down.position.y, 36.dp.toPx())
    if (bone < 0) return
    var moved = false
    var last = down.position
    try {
        while (true) {
            val e = awaitPointerEvent()
            e.changes.forEach { it.consume() }
            val c = e.changes.firstOrNull { it.id == down.id } ?: break
            if (!c.pressed) break
            if (!moved && hypot(c.position.x - down.position.x, c.position.y - down.position.y) < slop) continue
            moved = true
            last = c.position
            fixDrag.value = Triple(bone, last.x, last.y)
        }
    } finally {
        fixDrag.value = null
    }
    if (moved) store.moveHumanoidJoint(bone, m.cx(last.x), m.cy(last.y))
}

/** Alvo (quadrado) e polo (losango) dos membros com IK ligado. */
internal fun DrawScope.drawRigIkHandles(store: EditorStore, m: StageMapper) {
    val ik = store.humanoidIk ?: return
    val f = com.aurea.aurea.editor.panels.HUMANOID_IK_FLOATS
    for (limb in 0 until ik.size / f) {
        val o = limb * f
        if (ik[o] < .5f) continue
        val t = Offset(m.sx(ik[o + 1]), m.sy(ik[o + 2]))
        val p = Offset(m.sx(ik[o + 3]), m.sy(ik[o + 4]))
        val s = 7.dp.toPx()
        drawLine(Color.White.copy(alpha = .35f), t, p, 1.dp.toPx())
        drawRect(ShellColors.OutlineUnder, Offset(t.x - s - 1.5f, t.y - s - 1.5f), androidx.compose.ui.geometry.Size(2 * s + 3f, 2 * s + 3f))
        drawRect(Color(0xFF55CCFF), Offset(t.x - s, t.y - s), androidx.compose.ui.geometry.Size(2 * s, 2 * s))
        if (ik[o + 5] > .5f) drawCircle(AureaColors.Accent, s + 5.dp.toPx(), t, style = Stroke(2.dp.toPx()))
        val path = androidx.compose.ui.graphics.Path().apply {
            moveTo(p.x, p.y - s); lineTo(p.x + s, p.y); lineTo(p.x, p.y + s); lineTo(p.x - s, p.y); close()
        }
        drawPath(path, Color(0xFF55CCFF).copy(alpha = .8f))
    }
}

/** Controlador IK perto do dedo: (membro, é o polo) ou nulo. */
internal fun pickIkHandle(store: EditorStore, m: StageMapper, x: Float, y: Float, reach: Float): Pair<Int, Boolean>? {
    val ik = store.humanoidIk ?: return null
    val f = com.aurea.aurea.editor.panels.HUMANOID_IK_FLOATS
    var best = reach
    var hit: Pair<Int, Boolean>? = null
    for (limb in 0 until ik.size / f) {
        val o = limb * f
        if (ik[o] < .5f) continue
        val dt = hypot(x - m.sx(ik[o + 1]), y - m.sy(ik[o + 2]))
        if (dt < best) { best = dt; hit = limb to false }
        val dp = hypot(x - m.sx(ik[o + 3]), y - m.sy(ik[o + 4]))
        if (dp < best) { best = dp; hit = limb to true }
    }
    return hit
}

/** Arrasta o alvo/polo do IK no plano da vista. Um gesto = um passo de desfazer. */
internal suspend fun AwaitPointerEventScope.rigIkGesture(store: EditorStore, m: StageMapper, down: PointerInputChange, slop: Float) {
    down.consume()
    val hit = pickIkHandle(store, m, down.position.x, down.position.y, 36.dp.toPx()) ?: return
    var moved = false
    var began = false
    var last = down.position
    try {
        while (true) {
            val e = awaitPointerEvent()
            e.changes.forEach { it.consume() }
            val c = e.changes.firstOrNull { it.id == down.id } ?: break
            if (!c.pressed) break
            if (!moved && hypot(c.position.x - down.position.x, c.position.y - down.position.y) < slop) continue
            if (!moved) last = down.position
            moved = true
            val dx = (c.position.x - last.x) / m.fit
            val dy = (c.position.y - last.y) / m.fit
            last = c.position
            if (dx == 0f && dy == 0f) continue
            if (!began) { store.beginGesture("IK"); began = true }
            store.dragHumanoidIk(hit.first, hit.second, dx, dy)
        }
    } finally {
        if (began) { store.endGesture(); store.endPoseGesture() }
    }
}
