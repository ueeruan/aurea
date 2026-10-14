package com.aurea.aurea.editor

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.paramOf
import com.aurea.aurea.editor.panels.stagePointEligible
import com.aurea.aurea.state.EditorStore
import kotlin.math.hypot
import kotlin.math.roundToInt

/**
 * PONTO DE EFEITO NO PALCO (P1-5, generaliza o SaberStage.kt): o parâmetro
 * Point2D (ou os X/Y de um Point3D) em fração da camada que a pessoa tocou no
 * cartão aparece como uma alça sobre a camada. Arrastar a alça escreve o ponto —
 * o mesmo valor dos campos X/Y — num passo de desfazer; com o ponto animado,
 * vira keyframe no cabeçote. Só aparece com o parâmetro ATIVO (cartão aberto e
 * o ponto escolhido por toque): o palco 3D continua seco no resto do tempo.
 * Mesma geometria do Saber: homografia da caixa da camada (exata com a camada
 * girada em 3D). Toque fora da alça segue para o palco normal.
 */
internal object EffectPointStage {
    var layer by mutableLongStateOf(0L)
    var effect by mutableIntStateOf(-1)
    var param by mutableIntStateOf(-1)

    fun show(layerId: Long, effectId: Int, paramIndex: Int) {
        layer = layerId
        effect = effectId
        param = paramIndex
    }

    fun hide() {
        param = -1
    }
}

/** A alça do ponto ativo está no palco? (camada única, livre, parâmetro elegível). */
internal fun effectPointActive(store: EditorStore): Boolean {
    val s = EffectPointStage
    if (s.param < 0 || s.effect < 0 || store.selection.size != 1 || store.primary != s.layer) return false
    val d = store.detail ?: return false
    if (d.id != s.layer || d.locked) return false
    val p = store.paramOf(s.effect, s.param) ?: return false
    if (!stagePointEligible(p.type, p.flags)) return false
    // O Saber (núcleo único) já desenha os dois pontos dele.
    if (saberActive(store) && s.effect == SaberStage.effect && (s.param == SaberStage.START || s.param == SaberStage.END)) return false
    return true
}

/** O ponto ativo na tela (px do palco), ou nulo. */
private fun effectPointScreen(store: EditorStore, m: StageMapper): Offset? {
    val c = saberCorners(store, m) ?: return null
    val v = store.paramOf(EffectPointStage.effect, EffectPointStage.param)?.value ?: return null
    if (v.size < 2 || !v[0].isFinite() || !v[1].isFinite()) return null
    val p = FloatArray(2)
    saberUvToComp(c, v[0], v[1], p)
    return Offset(m.sx(p[0]), m.sy(p[1]))
}

private val PointInk = Color(0xFF6FC3FF)

/** Alça: anel com miolo e cruz fina (lê como "posição", não como o anel do Saber). */
internal fun DrawScope.drawEffectPointOverlay(store: EditorStore, m: StageMapper) {
    store.playhead   // andar o cabeçote redesenha (ponto animado)
    val o = effectPointScreen(store, m) ?: return
    val r = 9.dp.toPx()
    val under = ShellColors.OutlineUnder
    val arm = r + 6.dp.toPx()
    drawLine(under, Offset(o.x - arm, o.y), Offset(o.x + arm, o.y), 3.dp.toPx())
    drawLine(under, Offset(o.x, o.y - arm), Offset(o.x, o.y + arm), 3.dp.toPx())
    drawLine(Color.White, Offset(o.x - arm, o.y), Offset(o.x - r, o.y), 1.5.dp.toPx())
    drawLine(Color.White, Offset(o.x + r, o.y), Offset(o.x + arm, o.y), 1.5.dp.toPx())
    drawLine(Color.White, Offset(o.x, o.y - arm), Offset(o.x, o.y - r), 1.5.dp.toPx())
    drawLine(Color.White, Offset(o.x, o.y + r), Offset(o.x, o.y + arm), 1.5.dp.toPx())
    drawCircle(under, r + 2.dp.toPx(), o)
    drawCircle(PointInk, r - 1.dp.toPx(), o, style = Stroke(2.5.dp.toPx()))
    drawCircle(PointInk, 3.dp.toPx(), o)
}

/** O dedo caiu na alça? */
internal fun pickEffectPoint(store: EditorStore, m: StageMapper, x: Float, y: Float, reach: Float): Boolean {
    val o = effectPointScreen(store, m) ?: return false
    return hypot(x - o.x, y - o.y) < reach
}

/** Arrasto da alça. Chamado com o primeiro toque; consome até o dedo subir. */
internal suspend fun AwaitPointerEventScope.effectPointGesture(store: EditorStore, m: StageMapper, down: PointerInputChange) {
    val effect = EffectPointStage.effect
    val index = EffectPointStage.param
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
        if (!began) { store.beginGesture("mover ponto do efeito"); began = true }
        store.setEffectPoint(effect, param, uv[0], uv[1])
    }
    if (began) store.endGesture()
}

/**
 * Nó de acessibilidade da alça (o desenho mora no Canvas do palco): mesmo id do
 * iOS, `stage.effect_point`, com o nome do parâmetro e a dica do gesto.
 */
@Composable
internal fun EffectPointA11y(store: EditorStore, m: StageMapper) {
    // Derivados: o detalhe muda a cada quadro da reprodução; o nó só recompõe
    // quando a alça aparece/some ou troca de parâmetro.
    val active by remember(store) { derivedStateOf { effectPointActive(store) } }
    if (!active) return
    val name by remember(store) {
        derivedStateOf { store.paramOf(EffectPointStage.effect, EffectPointStage.param)?.label.orEmpty() }
    }
    val label = stringResource(R.string.fxui_stage_point, name)
    val hint = stringResource(R.string.fxui_stage_point_hint)
    val half = with(LocalDensity.current) { 22.dp.toPx() }
    Box(
        Modifier
            .offset {
                val o = if (m.valid) effectPointScreen(store, m) else null
                if (o == null) IntOffset(-10000, -10000) else IntOffset((o.x - half).roundToInt(), (o.y - half).roundToInt())
            }
            .size(44.dp)
            .testTag("stage.effect_point")
            .semantics {
                contentDescription = label
                stateDescription = hint
            },
    )
}
