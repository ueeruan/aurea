package com.aurea.aurea.editor

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.GIZMO_ROTATE
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.sin

/*
 * Trackball do gizmo 3D de girar (ferramenta Girar): só os anéis no pivô,
 * anéis vermelho/verde/azul = grandes círculos perpendiculares aos eixos
 * LOCAIS X/Y/Z, anel cinza de fora = giro em volta da vista. Toda a conta
 * (orientação na vista, toque, arrasto e Euler sem salto) é do motor
 * (core/Trackball.hpp); aqui só se desenha e se repassa o dedo.
 * Par do iOS: TrackballOverlay.swift.
 */

/** Raio da esfera na tela (o anel cinza fica em ×1,25). */
internal val TrackballRadius = 62.dp
private const val ViewRingScale = 1.25f   // trackball::kViewRingScale

private val RingX = Color(0xFFFF5A5A)
private val RingY = Color(0xFF5AD27A)
private val RingZ = Color(0xFF5AA8FF)
private val RingView = Color(0xFFC4C4CC)

/** Parte agarrada agora (−1 = nenhuma): o anel em destaque enquanto o dedo arrasta. */
internal object TrackballDrag {
    var part by mutableIntStateOf(-1)
}

/** Ferramenta Girar numa camada 3D (sem parte de forma 3D escolhida): o trackball substitui as alças. */
internal fun trackballActive(store: EditorStore): Boolean =
    store.gizmoTool == GIZMO_ROTATE && store.gizmo != null && store.selection.size == 1 &&
        store.shapePartOf(store.primary) < 0

/** Dica de acessibilidade da ferramenta Girar (o trackball não tem nó próprio: é desenho). */
@Composable
internal fun trackballHint(store: EditorStore): String? =
    if (trackballActive(store)) stringResource(R.string.gizmo_trackball_hint) else null

/** Desenha o trackball. Falso = não há (a ferramenta usa o desenho antigo). */
internal fun DrawScope.drawTrackball(store: EditorStore, m: StageMapper): Boolean {
    if (!trackballActive(store)) return false
    val id = store.primary ?: return false
    val t = store.queryTrackball(id) ?: return false
    val c = Offset(m.sx(t[0]), m.sy(t[1]))
    val r = TrackballRadius.toPx()
    val active = TrackballDrag.part
    // Sem esfera pintada (o dono pediu só o gizmo): os anéis desenham a bola;
    // o miolo continua sendo a área do giro livre e só aparece, leve, no arrasto.
    if (active == 4) drawCircle(Color.White.copy(alpha = .12f), r, c)
    // Anel cinza de fora: girar em volta da vista.
    val rv = r * ViewRingScale
    drawCircle(ShellColors.OutlineUnder, rv, c, style = Stroke(5.dp.toPx()))
    drawCircle(if (active == 3) Color.White else RingView, rv, c, style = Stroke(if (active == 3) 3.5.dp.toPx() else 2.5.dp.toPx()))
    // Anéis: a metade de trás bem fraca por baixo, a da frente por cima.
    val colors = arrayOf(RingX, RingY, RingZ)
    val back = Array(3) { Path() }
    val front = Array(3) { Path() }
    val n = 96
    for (ring in 0..2) {
        val u = ((ring + 1) % 3) * 3
        val v = ((ring + 2) % 3) * 3
        var px = 0f; var py = 0f; var pz = 0f
        for (i in 0..n) {
            val a = (i * 2.0 * Math.PI / n).toFloat()
            val ca = cos(a); val sa = sin(a)
            val x = c.x + r * (ca * t[2 + u] + sa * t[2 + v])
            val y = c.y + r * (ca * t[2 + u + 1] + sa * t[2 + v + 1])
            val z = ca * t[2 + u + 2] + sa * t[2 + v + 2]
            if (i > 0) {
                val p = if ((z + pz) * .5f <= 0f) front[ring] else back[ring]
                p.moveTo(px, py); p.lineTo(x, y)
            }
            px = x; py = y; pz = z
        }
    }
    for (ring in 0..2) drawPath(back[ring], colors[ring].copy(alpha = .28f), style = Stroke(1.5.dp.toPx()))
    val under = Stroke(5.5.dp.toPx(), cap = StrokeCap.Round)
    for (ring in 0..2) {
        val hot = active == ring
        drawPath(front[ring], ShellColors.OutlineUnder, style = if (hot) Stroke(7.dp.toPx(), cap = StrokeCap.Round) else under)
        drawPath(front[ring], if (hot) colors[ring].copy(red = minOf(1f, colors[ring].red + .15f), green = minOf(1f, colors[ring].green + .15f),
            blue = minOf(1f, colors[ring].blue + .15f)) else colors[ring],
            style = Stroke(if (hot) 4.5.dp.toPx() else 3.dp.toPx(), cap = StrokeCap.Round))
    }
    drawCircle(ShellColors.OutlineUnder, 4.dp.toPx(), c)
    drawCircle(Color.White, 3.dp.toPx(), c)
    return true
}

/**
 * Toque no trackball: anéis coloridos primeiro (~14 dp), depois o anel cinza,
 * depois a esfera. A parte tocada vale o gesto inteiro; o motor devolve a
 * Rotação XYZ a cada passo (absoluta desde o toque, sem salto em ±180°) e ela
 * entra pelo caminho do gizmo (chave automática + um passo de desfazer).
 * Falso = o dedo não pegou o trackball (o palco segue com o toque).
 */
internal suspend fun AwaitPointerEventScope.trackballGesture(
    store: EditorStore,
    m: StageMapper,
    down: PointerInputChange,
    slop: Float,
): Boolean {
    if (!trackballActive(store)) return false
    val id = store.primary ?: return false
    if (store.detail?.locked != false) return false
    val t = store.queryTrackball(id) ?: return false
    val cx = m.sx(t[0])
    val cy = m.sy(t[1])
    val r = TrackballRadius.toPx()
    val gx = down.position.x - cx
    val gy = down.position.y - cy
    val part = AureaEngine.trackballHit(t.copyOfRange(2, 11), gx, gy, r, 14.dp.toPx())
    if (part < 0) return false
    down.consume()
    val args = FloatArray(27)
    t.copyInto(args, 0, 11, 20)           // F
    t.copyInto(args, 9, 20, 23)           // Rotação no toque
    t.copyInto(args, 12, 20, 23)          // anterior = início
    args[18] = 1f                         // Q acumulado = identidade
    args[19] = part.toFloat()
    args[20] = gx; args[21] = gy
    args[26] = r
    var prevX = gx
    var prevY = gy
    var began = false
    var moved = false
    TrackballDrag.part = part
    val label = when (part) {
        0, 1, 2 -> "girar no eixo ${"XYZ"[part]}"
        3 -> "girar na vista"
        else -> "girar livre"
    }
    try {
        while (true) {
            val e = awaitPointerEvent()
            if (e.changes.count { it.pressed } > 1) break
            val ch = e.changes.firstOrNull { it.id == down.id } ?: break
            ch.consume()
            if (!ch.pressed) break
            val x = ch.position.x - cx
            val y = ch.position.y - cy
            if (!moved && hypot(x - gx, y - gy) < slop) continue
            moved = true
            if (x == prevX && y == prevY) continue
            args[22] = prevX; args[23] = prevY
            args[24] = x; args[25] = y
            val out = AureaEngine.trackballDrag(args)
            prevX = x; prevY = y
            if (out.size != 7) continue
            if (!began) {
                if (store.playing) store.pause()
                store.beginGesture(label)
                began = true
            }
            out.copyInto(args, 12, 0, 3)      // continuidade: o próximo passo parte daqui
            out.copyInto(args, 15, 3, 7)
            store.gizmoSetComponents(TrackProperty.ROTATION_X, floatArrayOf(out[0], out[1], out[2]))
        }
    } finally {
        if (began) store.endGesture()
        TrackballDrag.part = -1
    }
    return true
}
