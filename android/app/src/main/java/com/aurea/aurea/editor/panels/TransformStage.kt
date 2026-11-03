package com.aurea.aurea.editor.panels

import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import com.aurea.aurea.editor.StageMapper
import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors

// =============================================================================
// Omega aparência (área 05, P1-2 / P1-3): o que o painel Transformar desenha no
// palco. Chamado UMA vez por `drawStageOverlay` (Stage.kt), depois das linhas
// de encaixe do arrasto no palco:
//   • as GUIAS DO ÍMÃ do trackpad (`store.snapGuideX/Y`, px da composição):
//     linhas finas no destaque, enquanto o dedo está num patamar;
//   • a TRAJETÓRIA da posição (`store.showMotionPath`, ligada pela face Mover):
//     a linha pelos keyframes de posição X/Y, amostrada pelo motor
//     (`queryTrackCurve`) e levada do espaço do pai para a composição.
// Nada aloca por quadro: a curva fica num cache que só refaz quando os
// keyframes, o pai ou o valor parado mudam.
// =============================================================================

/** Desenha as guias do ímã e a trajetória (no-op quando nenhum dos dois está ligado). */
internal fun drawTransformStageGuides(scope: DrawScope, store: EditorStore, m: StageMapper) {
    if (!m.valid) return
    with(scope) {
        val stroke = 1.dp.toPx()
        val gx = store.snapGuideX
        if (gx != null && gx.isFinite()) {
            drawLine(AureaColors.Accent, Offset(m.sx(gx), m.sy(0f)), Offset(m.sx(gx), m.sy(m.compH)), stroke)
        }
        val gy = store.snapGuideY
        if (gy != null && gy.isFinite()) {
            drawLine(AureaColors.Accent, Offset(m.sx(0f), m.sy(gy)), Offset(m.sx(m.compW), m.sy(gy)), stroke)
        }
        if (store.showMotionPath) drawMotionPath(store, m)
    }
}

/**
 * 0.4.6: a trajetória como o motor a AVALIA (`query_position_path`): com as
 * alças de bézier espacial, o caminho curvo de verdade. Pontos em px da
 * COMPOSIÇÃO; os keyframes guardam também o espaço do pai (para o arrasto).
 */
internal object MotionPathCache {
    var layer = Long.MIN_VALUE
    var keys: List<KeyframeRow>? = null
    var detail: Any? = null
    var revision = -1
    var points = FloatArray(0)
    var count = 0
    /** Por keyframe: tempo, x, y (comp), entrada x/y, saída x/y (comp, ABSOLUTAS) e x, y no pai. */
    var keyData = FloatArray(0)
    var keyCount = 0
    val path = Path()
    val scratch = FloatArray(2)
    var strokeDensity = 0f
    var stroke = Stroke(1f)
    const val KEY_STRIDE = 9

    /** Refaz a curva se a camada, os keyframes, o detalhe ou as alças mudaram. Falso = sem caminho. */
    fun refresh(store: EditorStore): Boolean {
        val d = store.detail ?: return false
        if (d.perspective) return false
        val keys = store.keyframes[d.id] ?: return false
        if (layer == d.id && this.keys === keys && detail === d && revision == store.motionPathRevision) return count >= 2
        layer = d.id; this.keys = keys; detail = d; revision = store.motionPathRevision
        count = 0; keyCount = 0
        val raw = store.queryPositionPath(d.id) ?: return false
        if (raw.size < 2) return false
        val n = raw[0].toInt(); val k = raw[1].toInt()
        if (n < 2 || raw.size < 2 + n * 2 + k * 7) return false
        if (points.size < n * 2) points = FloatArray(n * 2)
        for (i in 0 until n) {
            d.parentToComp(raw[2 + i * 2], raw[3 + i * 2], scratch)
            points[i * 2] = scratch[0]; points[i * 2 + 1] = scratch[1]
        }
        count = n
        if (keyData.size < k * KEY_STRIDE) keyData = FloatArray(k * KEY_STRIDE)
        val base = 2 + n * 2
        for (i in 0 until k) {
            val o = base + i * 7; val q = i * KEY_STRIDE
            val px = raw[o + 1]; val py = raw[o + 2]
            keyData[q] = raw[o]
            d.parentToComp(px, py, scratch); keyData[q + 1] = scratch[0]; keyData[q + 2] = scratch[1]
            d.parentToComp(px + raw[o + 3], py + raw[o + 4], scratch); keyData[q + 3] = scratch[0]; keyData[q + 4] = scratch[1]
            d.parentToComp(px + raw[o + 5], py + raw[o + 6], scratch); keyData[q + 5] = scratch[0]; keyData[q + 6] = scratch[1]
            keyData[q + 7] = px; keyData[q + 8] = py
        }
        keyCount = k
        return true
    }
}

private fun DrawScope.drawMotionPath(store: EditorStore, m: StageMapper) {
    val c = MotionPathCache
    if (!c.refresh(store) || c.count < 2) return
    if (c.strokeDensity != density) {
        c.strokeDensity = density
        c.stroke = Stroke(1.5f * density, cap = StrokeCap.Round, join = StrokeJoin.Round)
    }
    val ink = AureaColors.Text.copy(alpha = 0.6f)
    val p = c.path
    p.reset()
    p.moveTo(m.sx(c.points[0]), m.sy(c.points[1]))
    for (i in 1 until c.count) p.lineTo(m.sx(c.points[i * 2]), m.sy(c.points[i * 2 + 1]))
    drawPath(p, ink, style = c.stroke)
    val r = 3.dp.toPx()
    val handle = AureaColors.Accent
    for (i in 0 until c.keyCount) {
        val q = i * MotionPathCache.KEY_STRIDE
        val v = Offset(m.sx(c.keyData[q + 1]), m.sy(c.keyData[q + 2]))
        val vin = Offset(m.sx(c.keyData[q + 3]), m.sy(c.keyData[q + 4]))
        val vout = Offset(m.sx(c.keyData[q + 5]), m.sy(c.keyData[q + 6]))
        // Alças (como no After Effects): linha do canto até cada ponta, com um ponto na ponta.
        if ((vin - v).getDistance() > 0.5f) { drawLine(handle, v, vin, 1.dp.toPx()); drawCircle(handle, r, vin) }
        if ((vout - v).getDistance() > 0.5f) { drawLine(handle, v, vout, 1.dp.toPx()); drawCircle(handle, r, vout) }
        // O canto: quadrado vazado (o ponto onde se puxa para arredondar).
        val s = 3.5f.dp.toPx()
        drawRect(ink, Offset(v.x - s, v.y - s), androidx.compose.ui.geometry.Size(s * 2, s * 2))
    }
}

/**
 * 0.4.6: o que o dedo pegou no caminho de movimento: keyframe `key` e a parte
 * (0 = o canto, 1 = ponta da entrada, 2 = ponta da saída). Nulo = nada perto.
 */
internal fun motionPathHit(store: EditorStore, m: StageMapper, x: Float, y: Float, radius: Float): Pair<Int, Int>? {
    if (!store.showMotionPath || !MotionPathCache.refresh(store)) return null
    val c = MotionPathCache
    var best: Pair<Int, Int>? = null
    var bestD = radius
    for (i in 0 until c.keyCount) {
        val q = i * MotionPathCache.KEY_STRIDE
        for (part in 0..2) {
            val px = m.sx(c.keyData[q + 1 + part * 2]); val py = m.sy(c.keyData[q + 2 + part * 2])
            if (part > 0 && kotlin.math.hypot(px - m.sx(c.keyData[q + 1]), py - m.sy(c.keyData[q + 2])) < 0.5f) continue
            val dd = kotlin.math.hypot(px - x, py - y)
            // As pontas das alças ganham do canto quando os dois estão sob o dedo.
            if (dd < bestD || (part > 0 && dd < bestD + radius * 0.3f)) { bestD = dd; best = i to part }
        }
    }
    return best
}

/**
 * Arrasto no caminho de movimento (como as alças da trajetória do After
 * Effects): puxar o CANTO arranca as duas alças, simétricas (o canto vira
 * curva suave); puxar a PONTA de uma alça a ajusta e a outra acompanha
 * espelhada. Tocar no canto que já tem alças sem arrastar o volta a reto.
 */
internal fun motionPathDrag(store: EditorStore, m: StageMapper, key: Int, part: Int, x: Float, y: Float, first: Boolean) {
    val c = MotionPathCache
    val d = store.detail ?: return
    if (key !in 0 until c.keyCount) return
    val q = key * MotionPathCache.KEY_STRIDE
    val time = c.keyData[q].toInt()
    val vx = c.keyData[q + 7]; val vy = c.keyData[q + 8]
    d.compToParent(m.cx(x), m.cy(y), c.scratch)
    val dx = c.scratch[0] - vx; val dy = c.scratch[1] - vy
    if (first) store.beginGesture("curvar caminho")
    if (part == 1) store.setPositionTangents(d.id, time, dx, dy, -dx, -dy)
    else store.setPositionTangents(d.id, time, -dx, -dy, dx, dy)
}

/** Toque sem arrasto no canto: volta a canto reto (tira as alças). */
internal fun motionPathReset(store: EditorStore, key: Int) {
    val c = MotionPathCache
    val d = store.detail ?: return
    if (key !in 0 until c.keyCount) return
    val q = key * MotionPathCache.KEY_STRIDE
    store.beginGesture("canto reto")
    store.setPositionTangents(d.id, c.keyData[q].toInt(), 0f, 0f, 0f, 0f)
    store.endGesture()
}

private fun sameOrBothNaN(a: Float, b: Float): Boolean = (a.isNaN() && b.isNaN()) || a == b
