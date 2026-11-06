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
import kotlin.math.hypot

/** Floats por osso de `Engine::query_model_bones`. */
internal const val BONE_FLOATS = 10

/**
 * POSE DO RIG 3D na Cena 3D (0.1.8, "como no Blender"; refeito em nov/2026 porque
 * ninguém conseguia acertar a junta). Com o modo de pose ligado:
 *  - tocar numa junta ou no meio de um osso escolhe o mais perto; tocar de novo
 *    em juntas sobrepostas passa para a de baixo; tocar no vazio solta;
 *  - arrastar no vazio SEMPRE orbita e a pinça aproxima, com ou sem osso escolhido;
 *  - arrastar a partir de um anel gira naquele eixo; a partir da junta, gira como
 *    trackball (ou leva a cadeia, com IK; ou desloca, com Mover);
 *  - na ferramenta Selecionar o dedo nunca mexe no osso.
 * O keyframe é marcado pelos botões da Cena 3D (osso ou todos). Osso que já tem
 * keyframe grava keyframe no cabeçote ao girar (o motor decide). Cada gesto = UM
 * passo de desfazer.
 */
internal fun rigPoseActive(store: EditorStore): Boolean =
    store.sceneEditor && store.rigPoseMode && store.selection.size == 1 && store.poseBones != null

/** Floats de `Engine::query_bone_gizmo` (anéis da articulação). */
internal const val BONE_GIZMO_FLOATS = 21

/** Raio dos anéis na tela e a folga do toque neles (0.4.5). */
private val RingRadius = 52.dp
private val RingReach = 16.dp
/** Anel de fora (em volta do olhar) = raio × este fator. */
private const val OUTER_RING = 1.3f
/** Cores dos eixos locais (X vermelho, Y verde, Z azul, como no Blender/Maya). */
internal val PoseAxisColors = arrayOf(Color(0xFFFF5A5F), Color(0xFF5BD86A), Color(0xFF4F9BFF))

internal fun DrawScope.drawRigPoseOverlay(store: EditorStore, m: StageMapper) {
    val b = store.poseBones ?: return
    val chosen = store.poseBone
    val count = b.size / BONE_FLOATS
    if (!store.skeletonHidden) {
        // Ossos como no Blender: peça sólida em facetas, larga no pai e fina no filho. Os
        // ramos laterais (ombros no peito, pernas no quadril) são só uma linha tracejada.
        val side = poseSideBones(store)
        for (i in 0 until count) {
            val o = i * BONE_FLOATS
            val parent = b[o + 1].toInt()
            if (parent < 0 || b[o + 9] < .5f || b[parent * BONE_FLOATS + 9] < .5f) continue
            val p = parent * BONE_FLOATS
            val a = Offset(m.sx(b[p + 7]), m.sy(b[p + 8]))
            val c = Offset(m.sx(b[o + 7]), m.sy(b[o + 8]))
            if (side.getOrElse(i) { false }) {
                // Fio escuro por baixo: o tracejado aparece no fundo e em cima de modelo claro.
                drawLine(BoneLine.copy(alpha = .6f), a, c, 2.dp.toPx())
                drawLine(Color.White.copy(alpha = .85f), a, c, 1.dp.toPx(),
                    pathEffect = androidx.compose.ui.graphics.PathEffect.dashPathEffect(floatArrayOf(4.dp.toPx(), 3.dp.toPx())))
            } else drawBone(a, c, b[p].toInt() == chosen)
        }
        // Juntas: bolinhas de arame; a escolhida acesa; anel destaque = keyframe no cabeçote.
        for (i in 0 until count) {
            val o = i * BONE_FLOATS
            if (b[o + 9] < .5f) continue
            val c = Offset(m.sx(b[o + 7]), m.sy(b[o + 8]))
            val on = b[o].toInt() == chosen
            // A bolinha acompanha o osso: num dedo ela não pode tampar o desenho.
            val parent = b[o + 1].toInt()
            val bone = if (parent >= 0) hypot(c.x - m.sx(b[parent * BONE_FLOATS + 7]), c.y - m.sy(b[parent * BONE_FLOATS + 8])) else Float.MAX_VALUE
            val r = if (on) 6.dp.toPx() else (bone * .12f).coerceIn(1.75.dp.toPx(), 4.5.dp.toPx())
            drawCircle(if (on) AureaColors.Accent else BoneLight, r, c)
            drawCircle(BoneLine, r, c, style = Stroke(.75.dp.toPx()))
            if (r >= 3.dp.toPx()) {
                val line = .75.dp.toPx()
                drawLine(BoneLine, Offset(c.x - r, c.y), Offset(c.x + r, c.y), line)
                drawLine(BoneLine, Offset(c.x, c.y - r), Offset(c.x, c.y + r), line)
            }
            if (b[o + 6] > .5f) drawCircle(AureaColors.Keyframe, r + 3.5.dp.toPx(), c, style = Stroke(2.dp.toPx()))
        }
    }
    drawPoseRings(store, m)
}

private val BoneLight = Color(0xFFC9CDD3)
private val BoneMid = Color(0xFFA6ABB3)
private val BoneDark = Color(0xFF7F858E)
private val BoneLine = Color(0xFF0B0E12)
private val BoneOnLight = Color(0xFFE3F2FB)
private val BoneOnMid = Color(0xFFA9D3EC)
private val BoneOnDark = Color(0xFF6FAED9)

/** Um osso de [a] (pai) a [c] (filho), como o octaedro do Blender visto de lado; [on] = escolhido. */
private fun DrawScope.drawBone(a: Offset, c: Offset, on: Boolean) {
    val dx = c.x - a.x
    val dy = c.y - a.y
    val len = hypot(dx, dy)
    if (len < 1f) return
    val ux = dx / len
    val uy = dy / len
    val w = (len * .12f).coerceIn(2.5.dp.toPx(), 12.dp.toPx())
    val k = Offset(a.x + ux * len * .12f, a.y + uy * len * .12f)
    val left = Offset(k.x - uy * w, k.y + ux * w)
    val right = Offset(k.x + uy * w, k.y - ux * w)
    fun facet(p: Offset, q: Offset, r: Offset, color: Color) {
        drawPath(androidx.compose.ui.graphics.Path().apply { moveTo(p.x, p.y); lineTo(q.x, q.y); lineTo(r.x, r.y); close() }, color)
    }
    facet(left, c, k, if (on) BoneOnLight else BoneLight)
    facet(right, c, k, if (on) BoneOnDark else BoneDark)
    facet(a, left, k, if (on) BoneOnMid else BoneMid)
    facet(a, right, k, if (on) BoneOnDark else BoneDark)
    val thin = .75.dp.toPx()
    drawLine(BoneLine, k, c, thin)
    drawLine(BoneLine, left, right, thin)
    drawPath(androidx.compose.ui.graphics.Path().apply {
        moveTo(a.x, a.y); lineTo(left.x, left.y); lineTo(c.x, c.y); lineTo(right.x, right.y); close()
    }, BoneLine, style = Stroke(1.dp.toPx()))
}

/** Último cálculo de [poseSideBones]: só refaz com outro modelo. */
private object PoseSides {
    var key = 0
    var side = BooleanArray(0)
}

/**
 * Por osso: é um ramo lateral (desenhado tracejado)? Quando um pai tem filhos que são
 * o par esquerda/direita um do outro e também um filho do meio, o do meio continua a
 * cadeia e o par só parte dali. Vem do espelhamento do motor, então não muda ao orbitar.
 */
internal fun poseSideBones(store: EditorStore): BooleanArray {
    val b = store.poseBones ?: return BooleanArray(0)
    val count = b.size / BONE_FLOATS
    var key = store.primary.hashCode()
    for (i in 0 until count) key = key * 31 + b[i * BONE_FLOATS].toInt() * 7 + b[i * BONE_FLOATS + 1].toInt()
    if (key == PoseSides.key && PoseSides.side.size == count) return PoseSides.side
    val side = BooleanArray(count)
    val children = (0 until count).groupBy { b[it * BONE_FLOATS + 1].toInt() }.filterKeys { it >= 0 }
    for (kids in children.values) {
        if (kids.size < 2) continue
        val nodes = kids.map { b[it * BONE_FLOATS].toInt() }
        val paired = kids.filter { i -> store.bonePartner(b[i * BONE_FLOATS].toInt()).let { it >= 0 && it in nodes } }
        if (paired.isNotEmpty() && paired.size < kids.size) paired.forEach { side[it] = true }
    }
    PoseSides.key = key
    PoseSides.side = side
    return side
}

/** Anéis de rotação do osso escolhido: X/Y/Z locais (metade de trás apagada) e o de fora (olhar). */
private fun DrawScope.drawPoseRings(store: EditorStore, m: StageMapper) {
    val g = store.poseGizmo ?: return
    if (g[2] < .5f || store.poseMove || store.poseIk || store.sceneSelectOnly) return
    val c = Offset(m.sx(g[0]), m.sy(g[1]))
    val r = RingRadius.toPx()
    drawCircle(ShellColors.OutlineUnder, r * OUTER_RING, c, style = Stroke(3.5.dp.toPx()))
    drawCircle(Color.White.copy(alpha = .75f), r * OUTER_RING, c, style = Stroke(1.5.dp.toPx()))
    val steps = 48
    for (axis in 0 until 3) {
        val o = 3 + axis * 6
        val color = PoseAxisColors[axis]
        var prev: Offset? = null
        var prevFront = true
        for (s in 0..steps) {
            val t = (2.0 * Math.PI * s / steps).toFloat()
            val cs = kotlin.math.cos(t); val sn = kotlin.math.sin(t)
            val p = Offset(c.x + (g[o] * cs + g[o + 3] * sn) * r, c.y + (g[o + 1] * cs + g[o + 4] * sn) * r)
            val front = g[o + 2] * cs + g[o + 5] * sn >= -0.05f
            prev?.let {
                if (prevFront && front) drawLine(ShellColors.OutlineUnder, it, p, 4.5.dp.toPx(), StrokeCap.Round)
                drawLine(if (prevFront && front) color else color.copy(alpha = .3f), it, p,
                    (if (prevFront && front) 2.5.dp else 1.5.dp).toPx(), StrokeCap.Round)
            }
            prev = p
            prevFront = front
        }
    }
}

/** Anel sob o dedo (0..2 eixo local, 3 = o de fora) ou −1. A junta em si (miolo) não conta. */
internal fun pickPoseRing(store: EditorStore, m: StageMapper, x: Float, y: Float, ringPx: Float, reach: Float, core: Float): Int {
    val g = store.poseGizmo ?: return -1
    if (g[2] < .5f || store.poseMove || store.poseIk || store.sceneSelectOnly) return -1
    val cx = m.sx(g[0]); val cy = m.sy(g[1])
    val dc = hypot(x - cx, y - cy)
    if (dc < core) return -1
    var best = reach
    var hit = -1
    val steps = 64
    for (axis in 0 until 3) {
        val o = 3 + axis * 6
        for (s in 0 until steps) {
            val t = (2.0 * Math.PI * s / steps).toFloat()
            val cs = kotlin.math.cos(t); val sn = kotlin.math.sin(t)
            // Só a metade da frente do anel pega o dedo.
            if (g[o + 2] * cs + g[o + 5] * sn < -0.05f) continue
            val d = hypot(x - (cx + (g[o] * cs + g[o + 3] * sn) * ringPx), y - (cy + (g[o + 1] * cs + g[o + 4] * sn) * ringPx))
            if (d < best) { best = d; hit = axis }
        }
    }
    if (hit < 0 && kotlin.math.abs(dc - ringPx * OUTER_RING) < reach) hit = 3
    return hit
}

/**
 * O que o dedo pegou: as juntas até [jointReach] (da mais perto à mais longe) e
 * depois os ossos até [boneReach] do traço. O traço pai → filho é o osso do PAI
 * (é ele que gira aquele pedaço), então tocar no meio do braço escolhe o ombro.
 */
internal fun pickPoseTargets(store: EditorStore, m: StageMapper, x: Float, y: Float, jointReach: Float, boneReach: Float): List<Int> {
    val b = store.poseBones ?: return emptyList()
    if (store.skeletonHidden) return emptyList()
    val out = ArrayList(pickPoseBones(store, m, x, y, jointReach))
    val segments = ArrayList<Pair<Float, Int>>()
    val side = poseSideBones(store)
    for (i in 0 until b.size / BONE_FLOATS) {
        val o = i * BONE_FLOATS
        val parent = b[o + 1].toInt()
        if (parent < 0 || side.getOrElse(i) { false } || b[o + 9] < .5f || b[parent * BONE_FLOATS + 9] < .5f) continue
        val p = parent * BONE_FLOATS
        val ax = m.sx(b[p + 7]); val ay = m.sy(b[p + 8])
        val vx = m.sx(b[o + 7]) - ax; val vy = m.sy(b[o + 8]) - ay
        val len2 = vx * vx + vy * vy
        if (len2 < 1f) continue
        val t = (((x - ax) * vx + (y - ay) * vy) / len2).coerceIn(0f, 1f)
        val d = hypot(x - (ax + vx * t), y - (ay + vy * t))
        if (d < boneReach) segments += d to b[p].toInt()
    }
    segments.sortedBy { it.first }.forEach { if (it.second !in out) out += it.second }
    return out
}

/** Juntas perto do dedo (até [reach] px do palco), da mais perto à mais longe: os NÓS. */
internal fun pickPoseBones(store: EditorStore, m: StageMapper, x: Float, y: Float, reach: Float): List<Int> {
    val b = store.poseBones ?: return emptyList()
    if (store.skeletonHidden) return emptyList()
    val hits = ArrayList<Pair<Float, Int>>()
    for (i in 0 until b.size / BONE_FLOATS) {
        val o = i * BONE_FLOATS
        if (b[o + 9] < .5f) continue
        val d = hypot(x - m.sx(b[o + 7]), y - m.sy(b[o + 8]))
        if (d < reach) hits += d to b[o].toInt()
    }
    return hits.sortedBy { it.first }.map { it.second }
}

/** Junta mais perto do dedo (até [reach] px do palco): o NÓ do osso, ou −1. */
internal fun pickPoseBone(store: EditorStore, m: StageMapper, x: Float, y: Float, reach: Float): Int =
    pickPoseBones(store, m, x, y, reach).firstOrNull() ?: -1

/** Até onde a junta e o traço do osso pegam o dedo, e o raio das juntas sobrepostas. */
private val JointReach = 26.dp
private val BoneReach = 14.dp
private val OverlapReach = 18.dp

/** Gesto no modo de pose (regras no alto do arquivo). Toda decisão sai do toque ou do arrasto. */
internal suspend fun AwaitPointerEventScope.rigPoseGesture(store: EditorStore, m: StageMapper, down: PointerInputChange, slop: Float) {
    down.consume()
    val x = down.position.x
    val y = down.position.y
    val ringPx = RingRadius.toPx()
    val ring = if (store.poseBone >= 0) pickPoseRing(store, m, x, y, ringPx, RingReach.toPx(), 18.dp.toPx()) else -1
    val targets = pickPoseTargets(store, m, x, y, JointReach.toPx(), BoneReach.toPx())
    // Toque ou arrasto? Só decide depois de o dedo andar ou de entrar o segundo dedo.
    var dragged = false
    var pinch = false
    while (true) {
        val e = awaitPointerEvent()
        e.changes.forEach { it.consume() }
        val pressed = e.changes.filter { it.pressed }
        if (pressed.isEmpty()) break
        if (pressed.size >= 2) { pinch = true; break }
        val c = pressed.firstOrNull { it.id == down.id } ?: pressed[0]
        if (hypot(c.position.x - x, c.position.y - y) >= slop) { dragged = true; break }
    }
    when {
        pinch -> poseViewGesture(store, down, true)
        !dragged -> poseTap(store, m, x, y, ring, targets)
        ring >= 0 -> poseRingGesture(store, m, down, slop, ring, ringPx / m.fit)
        targets.isEmpty() || store.sceneSelectOnly -> poseViewGesture(store, down, false)
        else -> {
            if (store.poseBone !in targets) store.choosePoseBone(targets.first())
            poseBoneGesture(store, m, down)
        }
    }
}

/**
 * Toque: escolhe o mais perto. Só muda de osso sozinho quando o escolhido está
 * sob o dedo junto com outras juntas: aí o toque passa para a de baixo.
 */
private fun androidx.compose.ui.unit.Density.poseTap(store: EditorStore, m: StageMapper, x: Float, y: Float, ring: Int, targets: List<Int>) {
    if (targets.isEmpty()) {
        if (ring < 0) store.choosePoseBone(-1)     // tocar sem querer no anel não solta o osso
        return
    }
    val overlap = pickPoseBones(store, m, x, y, OverlapReach.toPx())
    val i = overlap.indexOf(store.poseBone)
    store.choosePoseBone(if (i >= 0) overlap[(i + 1) % overlap.size] else targets.first())
}

/** Dedo no vazio: orbita; dois dedos aproximam a vista. Só a vista muda. */
private suspend fun AwaitPointerEventScope.poseViewGesture(store: EditorStore, down: PointerInputChange, startedPinching: Boolean) {
    val degPerPx = 0.35f / 1.dp.toPx()
    var last = down.position
    var span0 = 0f
    var distance0 = store.sceneDistance
    var pinching = startedPinching
    while (true) {
        val e = awaitPointerEvent()
        e.changes.forEach { it.consume() }
        val pressed = e.changes.filter { it.pressed }
        if (pressed.isEmpty()) break
        if (pressed.size >= 2) {
            pinching = true
            val span = hypot(pressed[0].position.x - pressed[1].position.x, pressed[0].position.y - pressed[1].position.y).coerceAtLeast(1f)
            if (span0 == 0f) { span0 = span; distance0 = store.sceneDistance }
            else store.updateSceneView(store.sceneYaw, store.scenePitch, distance0 * span0 / span)
            continue
        }
        if (pinching) continue      // sobrou um dedo da pinça: nada
        val c = pressed.firstOrNull { it.id == down.id } ?: pressed[0]
        store.updateSceneView(store.sceneYaw - (c.position.x - last.x) * degPerPx, store.scenePitch + (c.position.y - last.y) * degPerPx, store.sceneDistance)
        last = c.position
    }
}

/** Arrasto a partir da junta: gira como trackball, leva a cadeia (IK) ou desloca (Mover). Um desfazer. */
private suspend fun AwaitPointerEventScope.poseBoneGesture(store: EditorStore, m: StageMapper, down: PointerInputChange) {
    var began = false
    var last = down.position
    try {
        while (true) {
            val e = awaitPointerEvent()
            e.changes.forEach { it.consume() }
            val c = e.changes.firstOrNull { it.id == down.id } ?: break
            if (!c.pressed) break
            val dx = (c.position.x - last.x) / m.fit
            val dy = (c.position.y - last.y) / m.fit
            last = c.position
            if (dx == 0f && dy == 0f) continue
            if (!began) { store.beginGesture("posar osso"); began = true }
            if (store.poseIk && !store.poseMove) store.dragPoseIk(dx, dy) else store.dragPoseBone(dx, dy, 0f)
        }
    } finally {
        if (began) { store.endGesture(); store.endPoseGesture() }
    }
}

/** Arrasto num anel: gira o osso no eixo dele (0..2) ou em volta do olhar (3). Um desfazer. */
private suspend fun AwaitPointerEventScope.poseRingGesture(store: EditorStore, m: StageMapper, down: PointerInputChange,
                                                           slop: Float, axis: Int, radiusComp: Float) {
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
            moved = true
            if (c.position == last) continue
            if (!began) { store.beginGesture("girar osso"); began = true }
            store.dragPoseRing(axis, m.cx(last.x), m.cy(last.y), m.cx(c.position.x), m.cy(c.position.y), radiusComp)
            last = c.position
        }
    } finally {
        if (began) { store.endGesture(); store.endPoseGesture() }
    }
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
    val hit = pickIkHandle(store, m, down.position.x, down.position.y, 24.dp.toPx()) ?: return
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
