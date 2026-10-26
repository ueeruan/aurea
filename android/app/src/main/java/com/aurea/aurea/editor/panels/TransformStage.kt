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

/** A curva da trajetória em px da COMPOSIÇÃO (x,y intercalados) e os pontos dos keyframes. */
private object MotionPathCache {
    var layer = Long.MIN_VALUE
    var keys: List<KeyframeRow>? = null
    val affine = FloatArray(6)
    var staticX = Float.NaN
    var staticY = Float.NaN
    var points = FloatArray(0)
    var count = 0
    var dots = FloatArray(0)
    var dotCount = 0
    val path = Path()
    val scratch = FloatArray(2)
    var strokeDensity = 0f
    var stroke = Stroke(1f)
}

private fun DrawScope.drawMotionPath(store: EditorStore, m: StageMapper) {
    val d = store.detail ?: return
    // Câmera em perspectiva/3D: o caminho no plano do pai mentiria na tela.
    if (d.perspective) return
    val keys = store.keyframes[d.id] ?: return
    var lo = Int.MAX_VALUE
    var hi = Int.MIN_VALUE
    var keyX: KeyframeRow? = null
    var keyY: KeyframeRow? = null
    for (i in keys.indices) {
        val k = keys[i]
        if (k.effectIndex != EditorStore.NO_EFFECT) continue
        if (k.property != TrackProperty.POSITION_X && k.property != TrackProperty.POSITION_Y) continue
        if (k.property == TrackProperty.POSITION_X) keyX = keyX ?: k else keyY = keyY ?: k
        if (k.time < lo) lo = k.time
        if (k.time > hi) hi = k.time
    }
    if (hi <= lo) return
    val c = MotionPathCache
    val staticX = if (keyX == null) d.position[0] else Float.NaN
    val staticY = if (keyY == null) d.position[1] else Float.NaN
    val stale = c.layer != d.id || c.keys !== keys || !c.affine.contentEquals(d.parentAffine) ||
        !sameOrBothNaN(c.staticX, staticX) || !sameOrBothNaN(c.staticY, staticY)
    if (stale) {
        val xs = keyX?.let { store.queryTrackCurve(d.id, it, lo, hi) }
        val ys = keyY?.let { store.queryTrackCurve(d.id, it, lo, hi) }
        val n = minOf(xs?.size ?: Int.MAX_VALUE, ys?.size ?: Int.MAX_VALUE).takeIf { it != Int.MAX_VALUE } ?: 0
        if (c.points.size < n * 2) c.points = FloatArray(n * 2)
        for (i in 0 until n) {
            d.parentToComp(xs?.get(i) ?: staticX, ys?.get(i) ?: staticY, c.scratch)
            c.points[i * 2] = c.scratch[0]
            c.points[i * 2 + 1] = c.scratch[1]
        }
        c.count = n
        // Um ponto por instante com keyframe (de X ou de Y), na amostra mais perto.
        val times = keys.asSequence()
            .filter { it.effectIndex == EditorStore.NO_EFFECT && (it.property == TrackProperty.POSITION_X || it.property == TrackProperty.POSITION_Y) }
            .map { it.time }.distinct().toList()
        if (c.dots.size < times.size * 2) c.dots = FloatArray(times.size * 2)
        var dots = 0
        if (n > 1) for (t in times) {
            val i = ((t - lo).toFloat() / (hi - lo) * (n - 1)).let { kotlin.math.round(it).toInt() }.coerceIn(0, n - 1)
            c.dots[dots * 2] = c.points[i * 2]
            c.dots[dots * 2 + 1] = c.points[i * 2 + 1]
            dots++
        }
        c.dotCount = dots
        c.layer = d.id
        c.keys = keys
        d.parentAffine.copyInto(c.affine)
        c.staticX = staticX
        c.staticY = staticY
    }
    if (c.count < 2) return
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
    for (i in 0 until c.dotCount) drawCircle(ink, r, Offset(m.sx(c.dots[i * 2]), m.sy(c.dots[i * 2 + 1])))
}

private fun sameOrBothNaN(a: Float, b: Float): Boolean = (a.isNaN() && b.isNaN()) || a == b
