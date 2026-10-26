package com.aurea.aurea.editor

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameMillis
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
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
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.paramOf
import com.aurea.aurea.editor.panels.stagePointEligible
import com.aurea.aurea.editor.panels.writeParamVector
import com.aurea.aurea.engine.ParamControl
import com.aurea.aurea.engine.ParamType
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.FxParamMath
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import kotlin.math.hypot
import kotlin.math.roundToInt

/**
 * PARÂMETRO DE EFEITO ATIVO NO PREVIEW.
 *
 * Omega efeitos (0.3.2, área 07, P1-2), como a referência (`EditActivity.bL`,
 * `lG.Pl3`/`lG.Q`): tocar no rótulo (ou num eixo) de um ponto, XYZ ou orientação
 * ATIVA o parâmetro. Aparece no centro do preview o GUIA "deslize para ajustar"
 * com o nome do parâmetro, e QUALQUER arrasto no preview (fora das alças das
 * outras ferramentas) ajusta o valor de forma RELATIVA:
 *
 *  - ponto em fração da camada (`kParamRelative`): a variação do dedo em fração
 *    da camada — o ponto segue o dedo (homografia da caixa da camada, exata com
 *    a camada girada em 3D);
 *  - outros pontos: Δ px de tela × passo do motor (regra da referência);
 *  - XYZ: os dois primeiros componentes;
 *  - orientação: trackball sobre α/β/γ ([FxParamMath.trackball]).
 *
 * Um passo de desfazer por gesto. Tocar sem arrastar SAI do modo. A alça do
 * ponto relativo (P1-5 antigo) fica como MARCADOR: arrastar nela continua
 * absoluto. Palco 3D seco: o modo só existe com o parâmetro ativo.
 * Par do iOS: `SaberStage.swift` (`EffectPointStageOverlay`) e `EditorView.swift`.
 */
internal object EffectPointStage {
    var layer by mutableLongStateOf(0L)
    var effect by mutableIntStateOf(-1)
    var param by mutableIntStateOf(-1)
    /** Eixo escolhido (Point/XYZ) e o controle do motor (Point, Xyz, Orientation). */
    var component by mutableIntStateOf(0)
    var control by mutableIntStateOf(ParamControl.POINT)

    /** Guia: rótulo, tempos e um contador que redispara a animação. */
    var guideLabel by mutableStateOf("")
    var guide by mutableStateOf(FxParamMath.Guide.Short)
    var guideToken by mutableLongStateOf(0L)

    fun show(layerId: Long, effectId: Int, paramIndex: Int, axis: Int = 0, kind: Int = ParamControl.POINT) {
        layer = layerId
        effect = effectId
        param = paramIndex
        component = axis
        control = kind
    }

    /** Mostra o guia "deslize para ajustar" com o nome do parâmetro. */
    fun showGuide(label: String, duration: FxParamMath.Guide) {
        guideLabel = label
        guide = duration
        guideToken++
    }

    fun hide() {
        param = -1
        guideToken++   // apaga o guia que ainda estivesse na tela
        guideLabel = ""
    }

    /** O parâmetro ativo é este? */
    fun isActive(layerId: Long?, effectId: Int, paramIndex: Int): Boolean =
        param >= 0 && layerId != null && layer == layerId && effect == effectId && param == paramIndex
}

/** Parâmetro que pode ser ativado no preview: ponto 2D/3D visível. */
internal fun effectParamDraggable(type: Int, flags: Int): Boolean =
    (type == ParamType.POINT2D || type == ParamType.POINT3D) && (flags and ParamType.FLAG_HIDDEN) == 0

/** O modo "ajustar parâmetro" vale agora? (camada única, livre, parâmetro ativo existente). */
internal fun effectParamDragActive(store: EditorStore): Boolean {
    val s = EffectPointStage
    if (s.param < 0 || s.effect < 0 || store.selection.size != 1 || store.primary != s.layer) return false
    val d = store.detail ?: return false
    if (d.id != s.layer || d.locked) return false
    val p = store.paramOf(s.effect, s.param) ?: return false
    return effectParamDraggable(p.type, p.flags) && p.enabled
}

/** A alça (marcador) do ponto ativo está no palco? (ponto em fração da camada). */
internal fun effectPointActive(store: EditorStore): Boolean {
    if (!effectParamDragActive(store)) return false
    val s = EffectPointStage
    val p = store.paramOf(s.effect, s.param) ?: return false
    if (!stagePointEligible(p.type, p.flags) || s.control == ParamControl.ORIENTATION) return false
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

/** Arrasto da alça (absoluto). Chamado com o primeiro toque; consome até o dedo subir. */
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
 * Omega efeitos (P1-2): ARRASTO RELATIVO em qualquer lugar do preview com o
 * parâmetro ativo. Passou do [slop] = gesto (um passo de desfazer); soltou sem
 * andar = toque, que sai do modo.
 */
internal suspend fun AwaitPointerEventScope.effectParamRelativeGesture(
    store: EditorStore,
    m: StageMapper,
    down: PointerInputChange,
    slop: Float,
) {
    val s = EffectPointStage
    val effect = s.effect
    val index = s.param
    val control = s.control
    val first = store.paramOf(effect, index) ?: return
    val start = first.value.copyOf()
    val relative = (first.flags and ParamType.FLAG_RELATIVE) != 0
    val corners = saberCorners(store, m)
    val uv0 = FloatArray(2)
    val uv = FloatArray(2)
    val hasUv = relative && corners != null && saberCompToUv(corners, m.cx(down.position.x), m.cy(down.position.y), uv0)
    val step = if (first.step > 0f && first.step.isFinite()) first.step else 1f
    var moved = false
    var began = false
    var prev = down.position
    down.consume()
    fun norm(p: Offset): Offset {
        val w = m.compW.takeIf { it > 0f } ?: 1f
        val h = m.compH.takeIf { it > 0f } ?: 1f
        return Offset(m.cx(p.x) / w * 2f - 1f, m.cy(p.y) / h * 2f - 1f)
    }
    while (true) {
        val e = awaitPointerEvent()
        val ch = e.changes.firstOrNull { it.id == down.id } ?: break
        if (!ch.pressed) break
        if (!moved) {
            if ((ch.position - down.position).getDistance() < slop) continue
            moved = true
        }
        ch.consume()
        if (!m.valid) continue
        val param = store.paramOf(effect, index) ?: continue
        if (!began) {
            store.beginGesture("ajustar ${param.label}")
            began = true
        }
        when {
            control == ParamControl.ORIENTATION && param.type == ParamType.POINT3D -> {
                val a = norm(prev)
                val b = norm(ch.position)
                prev = ch.position
                val v = param.value
                val r = FxParamMath.trackball(v[0], v[1], v[2], a.x, a.y, b.x, b.y)
                store.writeParamVector(effect, param, floatArrayOf(r[0], r[1], r[2], v.getOrElse(3) { 0f }))
            }
            hasUv && corners != null -> {
                if (!saberCompToUv(corners, m.cx(ch.position.x), m.cy(ch.position.y), uv)) continue
                store.setEffectPoint(effect, param, start[0] + (uv[0] - uv0[0]), start[1] + (uv[1] - uv0[1]))
            }
            else -> {
                // Referência: valor + (dx, dy em px de tela) × passo.
                val dx = (ch.position.x - down.position.x) * step
                val dy = (ch.position.y - down.position.y) * step
                store.setEffectPoint(effect, param, start[0] + dx, start[1] + dy)
            }
        }
    }
    if (began) store.endGesture()
    else if (!moved) s.hide()
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

/**
 * O GUIA "DESLIZE PARA AJUSTAR" no centro do preview (referência:
 * `previewSwipeGuide*`): glifo de deslizar + "Deslize para ajustar" 10 sp no
 * texto + o nome do parâmetro 10 sp negrito no destaque. Tempos
 * [FxParamMath.Guide]: entra, espera, sai. Id `stage.effect.guide`.
 */
@Composable
internal fun EffectParamGuide(modifier: Modifier = Modifier) {
    val token = EffectPointStage.guideToken
    val label = EffectPointStage.guideLabel
    val kind = EffectPointStage.guide
    var alpha by remember { mutableFloatStateOf(0f) }
    LaunchedEffect(token) {
        if (label.isEmpty()) {
            alpha = 0f
            return@LaunchedEffect
        }
        val begin = withFrameMillis { it }
        while (true) {
            val t = withFrameMillis { it } - begin
            alpha = kind.alphaAt(t)
            if (t >= kind.total) break
        }
        alpha = 0f
    }
    if (alpha <= 0f || label.isEmpty()) return
    val text = stringResource(R.string.om_fx_swipe_to_adjust)
    Box(modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
        Column(
            Modifier
                .alpha(alpha)
                .width(100.dp)
                .testTag("stage.effect.guide")
                .semantics(mergeDescendants = true) {
                    contentDescription = "$text · $label"
                    liveRegion = LiveRegionMode.Polite
                },
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            CupertinoIcon(CupertinoGlyph.ArrowLeftRight, 22.dp, AureaColors.Text)
            Text(
                text,
                style = AureaType.Base.merge(TextStyle(fontSize = 10.sp, color = AureaColors.Text, textAlign = TextAlign.Center)),
            )
            Text(
                label,
                maxLines = 1,
                style = AureaType.Base.merge(
                    TextStyle(fontSize = 10.sp, fontWeight = FontWeight.Bold, color = AureaColors.Accent, textAlign = TextAlign.Center),
                ),
            )
        }
    }
}
