package com.aurea.aurea.editor

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.unit.dp
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.editor.panels.paramOf
import com.aurea.aurea.state.EditorStore
import kotlin.math.hypot
import kotlin.math.roundToInt

/**
 * SABER no palco (aurea.light.saber): com o cartão do efeito aberto e o
 * núcleo "Único", o Início e o Fim do núcleo aparecem como dois pontos sobre
 * a camada, ligados por uma linha tracejada. Arrastar um ponto o move — o
 * valor é a fração da caixa da camada, o mesmo dos campos X/Y do cartão; o
 * arrasto inteiro é UM passo de desfazer e, com o ponto animado, vira
 * keyframe no cabeçote. Toque fora dos pontos segue para o palco normal.
 * Mesmo padrão do Fantoche (PuppetStage.kt): cantos da camada → composição.
 */
internal object SaberStage {
    const val TYPE_PARAM = 6
    const val START = 7
    const val END = 8
    val TYPE: Int get() = effectTypeId("aurea.light.saber")
    var layer by mutableLongStateOf(0L)
    var effect by mutableIntStateOf(-1)

    fun open(layerId: Long, effectId: Int) {
        layer = layerId
        effect = effectId
    }

    fun close(effectId: Int) {
        if (effect == effectId) effect = -1
    }
}

/** Cartão do Saber aberto: liga os pontos no palco enquanto ele estiver à vista. */
@Composable
internal fun SaberCardTools(store: EditorStore, effectId: Int) {
    val layer = store.primary ?: return
    DisposableEffect(layer, effectId) {
        SaberStage.open(layer, effectId)
        onDispose { SaberStage.close(effectId) }
    }
}

/** Os pontos do Saber estão no palco (cartão aberto, núcleo único, camada livre)? */
internal fun saberActive(store: EditorStore): Boolean {
    if (SaberStage.effect < 0 || store.selection.size != 1 || store.primary != SaberStage.layer) return false
    val d = store.detail ?: return false
    if (d.id != SaberStage.layer || d.locked) return false
    val type = store.paramOf(SaberStage.effect, SaberStage.TYPE_PARAM)?.value?.firstOrNull() ?: return false
    return type.roundToInt() == 0
}

/** Cantos da caixa da camada na composição; a camada de ajuste cobre a composição. */
internal fun saberCorners(store: EditorStore, m: StageMapper): FloatArray? {
    val d = store.detail ?: return null
    if (d.adjustment) return floatArrayOf(0f, 0f, m.compW, 0f, m.compW, m.compH, 0f, m.compH)
    val c = FloatArray(8)
    return if (LayerGeometry.corners(d, c)) c else null
}

/**
 * Homografia do quadrado unitário da camada (u, v) para os 4 cantos na
 * composição — (0,0) (1,0) (1,1) (0,1). Exata também com a camada girada em
 * 3D (cantos em perspectiva, que um paralelogramo de 3 cantos deslocava);
 * com os cantos num paralelogramo vira a mesma conta afim de antes.
 */
private fun saberHomography(c: FloatArray): FloatArray {
    val x0 = c[0]; val y0 = c[1]; val x1 = c[2]; val y1 = c[3]
    val x2 = c[4]; val y2 = c[5]; val x3 = c[6]; val y3 = c[7]
    val sx = x0 - x1 + x2 - x3
    val sy = y0 - y1 + y2 - y3
    var g = 0f
    var h = 0f
    if (kotlin.math.abs(sx) > 1e-4f || kotlin.math.abs(sy) > 1e-4f) {
        val dx1 = x1 - x2; val dx2 = x3 - x2
        val dy1 = y1 - y2; val dy2 = y3 - y2
        val den = dx1 * dy2 - dx2 * dy1
        if (kotlin.math.abs(den) > 1e-9f) {
            g = (sx * dy2 - dx2 * sy) / den
            h = (dx1 * sy - sx * dy1) / den
        }
    }
    return floatArrayOf(x1 - x0 + g * x1, x3 - x0 + h * x3, x0,
                        y1 - y0 + g * y1, y3 - y0 + h * y3, y0, g, h)
}

internal fun saberUvToComp(c: FloatArray, u: Float, v: Float, out: FloatArray) {
    val m = saberHomography(c)
    val w = (m[6] * u + m[7] * v + 1f).let { if (kotlin.math.abs(it) < 1e-6f) 1e-6f else it }
    out[0] = (m[0] * u + m[1] * v + m[2]) / w
    out[1] = (m[3] * u + m[4] * v + m[5]) / w
}

internal fun saberCompToUv(c: FloatArray, x: Float, y: Float, out: FloatArray): Boolean {
    // x·(g u + h v + 1) = a u + b v + c  →  sistema 2×2 em (u, v).
    val m = saberHomography(c)
    val a11 = m[0] - m[6] * x; val a12 = m[1] - m[7] * x
    val a21 = m[3] - m[6] * y; val a22 = m[4] - m[7] * y
    val det = a11 * a22 - a12 * a21
    if (kotlin.math.abs(det) < 1e-6f) return false
    val r1 = x - m[2]; val r2 = y - m[5]
    out[0] = (r1 * a22 - a12 * r2) / det
    out[1] = (a11 * r2 - r1 * a21) / det
    return true
}

/** Ponto do parâmetro `index` (fração da camada) na tela. */
private fun saberScreen(store: EditorStore, m: StageMapper, c: FloatArray, index: Int): Offset? {
    val v = store.paramOf(SaberStage.effect, index)?.value ?: return null
    if (v.size < 2) return null
    val p = FloatArray(2)
    saberUvToComp(c, v[0], v[1], p)
    return Offset(m.sx(p[0]), m.sy(p[1]))
}

private val SaberInk = Color(0xFF6FC3FF)

internal fun DrawScope.drawSaberOverlay(store: EditorStore, m: StageMapper) {
    store.playhead   // andar o cabeçote redesenha (ponto animado)
    val c = saberCorners(store, m) ?: return
    val a = saberScreen(store, m, c, SaberStage.START) ?: return
    val b = saberScreen(store, m, c, SaberStage.END) ?: return
    val under = ShellColors.OutlineUnder
    drawLine(under, a, b, 3.dp.toPx())
    drawLine(Color.White.copy(alpha = 0.85f), a, b, 1.5.dp.toPx(),
        pathEffect = PathEffect.dashPathEffect(floatArrayOf(6.dp.toPx(), 4.dp.toPx())))
    val r = 8.dp.toPx()
    // Início: anel; fim: cheio (a ponta do traço).
    drawCircle(under, r + 2.dp.toPx(), a)
    drawCircle(SaberInk, r - 1.dp.toPx(), a, style = Stroke(2.5.dp.toPx()))
    drawCircle(under, r + 2.dp.toPx(), b)
    drawCircle(SaberInk, r, b)
}

/** Ponto do Saber sob o dedo: [SaberStage.START], [SaberStage.END] ou −1. */
internal fun pickSaberHandle(store: EditorStore, m: StageMapper, x: Float, y: Float, reach: Float): Int {
    val c = saberCorners(store, m) ?: return -1
    var best = reach
    var hit = -1
    for (index in intArrayOf(SaberStage.END, SaberStage.START)) {
        val o = saberScreen(store, m, c, index) ?: continue
        val d = hypot(x - o.x, y - o.y)
        if (d < best) { best = d; hit = index }
    }
    return hit
}

/** Arrasto de um ponto do Saber. Chamado com o primeiro toque; consome até o dedo subir. */
internal suspend fun AwaitPointerEventScope.saberGesture(store: EditorStore, m: StageMapper, down: PointerInputChange, index: Int) {
    val effect = SaberStage.effect
    val c = saberCorners(store, m) ?: return
    down.consume()
    val uv = FloatArray(2)
    var began = false
    while (true) {
        val e = awaitPointerEvent()
        val ch = e.changes.firstOrNull { it.id == down.id } ?: break
        ch.consume()
        if (!ch.pressed) break
        if (!m.valid || !saberCompToUv(c, m.cx(ch.position.x), m.cy(ch.position.y), uv)) continue
        val param = store.paramOf(effect, index) ?: continue
        if (!began) { store.beginGesture("mover ponto do Saber"); began = true }
        store.setEffectPoint(effect, param, uv[0], uv[1])
    }
    if (began) store.endGesture()
}
