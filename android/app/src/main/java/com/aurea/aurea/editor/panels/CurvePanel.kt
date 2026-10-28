package com.aurea.aurea.editor.panels

import com.aurea.aurea.ui.i18n.KeepLtr
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.snapshotFlow
import androidx.compose.runtime.LaunchedEffect
import kotlinx.coroutines.flow.drop
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.annotation.StringRes
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaActionSheet
import com.aurea.aurea.ui.ds.AureaNamePrompt
import com.aurea.aurea.ui.ds.SheetAction
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
// Omega painel: (área 04, P1-11) o editor de curva no layout da referência.
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.semantics.disabled
import com.aurea.aurea.ui.ds.AnchoredMenu
import com.aurea.aurea.ui.ds.MenuEntry
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

/** `aurea::Interpolation` (engine/include/aurea/core/Types.hpp). */
internal object Interp {
    const val HOLD = 0
    const val LINEAR = 1
    const val BEZIER = 2
    const val EASE_IN = 3
    const val EASE_OUT = 4
    const val EASE_IN_OUT = 5
    const val CUSTOM = 6
    const val BOUNCE = 7
    const val ELASTIC = 8
    const val STEPS = 9
    /** "back": passa do valor final e volta (`Interpolation::Overshoot`). */
    const val OVERSHOOT = 10
}

/**
 * Curvas com parâmetros (`kEaseParamMarker`, core/Math.hpp): com y2 = −10 o
 * Quique, o Elástico e o Overshoot leem os parâmetros em x1 (A), y1 (B) e x2
 * (sentido: < 0,5 = invertida). Sem o marcador, os padrões de sempre.
 */
internal const val EASE_PARAM_MARKER = -10f
/** Overshoot padrão: s = 1,70158 (≈ 10 % além do fim) em 0..1 → s 0..5. */
internal const val OVERSHOOT_DEFAULT_AMOUNT = 1.70158f / 5f
internal const val ELASTIC_DEFAULT_CYCLES = 3
internal const val ELASTIC_DEFAULT_DAMPING = .25f

/** `overshoot_ease` (core/Math.hpp), mesma conta em f32. */
internal fun overshootCurve(t: Float, amount: Float, reverse: Boolean): Float {
    if (t <= 0f) return 0f
    if (t >= 1f) return 1f
    val s = amount.coerceIn(0f, 1f) * 5f
    val u = (if (reverse) 1f - t else t) - 1f
    val v = 1f + (s + 1f) * u * u * u + s * u * u
    return if (reverse) 1f - v else v
}

/**
 * O `std::round` do motor (meio para longe do zero; nos valores ≥ 0 daqui é
 * floor(x+½)). O `kotlin.math.round` arredonda o meio para o PAR: x1 = 0,3125
 * dava 2 ciclos aqui e 3 no motor.
 */
internal fun roundHalfUp(x: Float): Int = kotlin.math.floor(x + 0.5f).toInt()

/** `elastic_ease` (core/Math.hpp): ciclos inteiros (1..8), amortecimento k = 2 + 16·B. */
internal fun elasticCurve(t: Float, cycles01: Float, damping01: Float, reverse: Boolean): Float {
    if (t <= 0f) return 0f
    if (t >= 1f) return 1f
    val n = roundHalfUp(cycles01 * 8f).coerceIn(1, 8).toFloat()
    val k = 2f + 16f * damping01.coerceIn(0f, 1f)
    val time = if (reverse) 1f - t else t
    val v = (1f - kotlin.math.exp(-k * time) * kotlin.math.cos(2f * Math.PI.toFloat() * n * time)) / (1f - kotlin.math.exp(-k))
    return if (reverse) 1f - v else v
}

/**
 * Um easing como o motor o avalia (`apply_easing`): nomeado ou bézier com dois
 * pontos de controle. Para mostrar alças, os nomeados viram a bézier equivalente
 * (EaseIn t² = bézier (⅓,0)(⅔,⅓) exata).
 */
@Immutable
internal data class Ease(val interp: Int, val x1: Float, val y1: Float, val x2: Float, val y2: Float, val power: Int = 1) {
    val isBezier get() = interp == Interp.BEZIER || interp == Interp.CUSTOM

    /** Tem alças arrastáveis (bézier ou reta, que vira bézier ao ser puxada). */
    val hasHandles get() = isBezier || interp == Interp.LINEAR || interp == Interp.EASE_IN || interp == Interp.EASE_OUT || interp == Interp.EASE_IN_OUT
    val supportsInversion get() = interp in Interp.LINEAR..Interp.CUSTOM || interp == Interp.BOUNCE ||
        interp == Interp.OVERSHOOT || (interp == Interp.ELASTIC && y2 == EASE_PARAM_MARKER)
    /** Família com parâmetros (Quique, Elástico, Overshoot): x1..y2 são os parâmetros. */
    val isParametric get() = interp == Interp.BOUNCE || interp == Interp.ELASTIC || interp == Interp.OVERSHOOT
    private val marked get() = y2 == EASE_PARAM_MARKER

    val overshootAmount get() = if (interp == Interp.OVERSHOOT && marked) x1.coerceIn(0f, 1f) else OVERSHOOT_DEFAULT_AMOUNT
    fun overshoot(amount: Float = overshootAmount) =
        Ease(Interp.OVERSHOOT, amount.coerceIn(0f, 1f), 0f, if (interp == Interp.OVERSHOOT && marked) x2 else 1f, EASE_PARAM_MARKER)

    val elasticCycles get() = if (interp == Interp.ELASTIC && marked) roundHalfUp(x1 * 8).coerceIn(1, 8) else ELASTIC_DEFAULT_CYCLES
    val elasticDamping get() = if (interp == Interp.ELASTIC && marked) y1.coerceIn(0f, 1f) else ELASTIC_DEFAULT_DAMPING
    fun elastic(cycles: Int = elasticCycles, damping: Float = elasticDamping) =
        Ease(Interp.ELASTIC, cycles.coerceIn(1, 8) / 8f, damping.coerceIn(0f, 1f), if (interp == Interp.ELASTIC && marked) x2 else 1f, EASE_PARAM_MARKER)

    val bounceCount get() = if (y2 == -10f) roundHalfUp(x1 * 8).coerceIn(1, 8) else 3
    val bounceStrength get() = if (y2 == -10f) y1.coerceIn(.1f, .9f) else .5f
    fun bounce(count: Int = bounceCount, strength: Float = bounceStrength) =
        Ease(Interp.BOUNCE, count / 8f, strength, if (y2 == -10f) x2 else 1f, -10f)

    private fun ballistic(t: Float): Float {
        if (t <= 0f) return 0f
        if (t >= 1f) return 1f
        val reverse = x2 < .5f
        val time = if (reverse) 1f-t else t
        val r = bounceStrength
        var power = r
        var total = 1f
        repeat(bounceCount) { total += 2f*power; power *= r }
        val landing = 1f/total
        var result = (time/landing)*(time/landing)
        if (time >= landing) {
            var start = landing; power = r
            for (i in 0 until bounceCount) {
                val span = 2f*landing*power
                if (time <= start+span || i == bounceCount-1) {
                    val u = ((time-start)/span).coerceIn(0f,1f)
                    result = 1f-4f*power*power*u*(1f-u); break
                }
                start += span; power *= r
            }
        }
        return if (reverse) 1f-result else result
    }
    fun transform(t: Float): Float = when (interp) {
        Interp.HOLD -> if (t < 1f) 0f else 1f
        Interp.LINEAR -> t
        Interp.EASE_IN -> t * t
        Interp.EASE_OUT -> 1f - (1f - t) * (1f - t)
        Interp.EASE_IN_OUT -> if (t < 0.5f) 2f * t * t else 1f - 2f * (1f - t) * (1f - t)
        Interp.BOUNCE -> if (y2 == -10f) ballistic(t) else {
            val u = t.coerceIn(0f, 1f)
            if (u < .5f) 4f*u*u else {
                val (start, span, height) = if (u < .75f) Triple(.5f,.25f,.25f) else if (u < .9f) Triple(.75f,.15f,.0625f) else Triple(.9f,.1f,.015625f)
                val p = (u-start)/span
                1f-4f*height*p*(1f-p)
            }
        }
        Interp.ELASTIC -> if (marked) elasticCurve(t, x1, y1, x2 < .5f) else if (t <= 0f) 0f else if (t >= 1f) 1f else ((1-kotlin.math.exp(-6.0*t)*kotlin.math.cos(6*Math.PI*t))/(1-kotlin.math.exp(-6.0))).toFloat()
        Interp.STEPS -> kotlin.math.floor(t.coerceIn(0f,1f)*4f)/4f
        Interp.OVERSHOOT -> if (marked) overshootCurve(t, x1, x2 < .5f) else overshootCurve(t, OVERSHOOT_DEFAULT_AMOUNT, false)
        // `keyframe_ease` (Curve.hpp): a força repete a MESMA bézier sobre o resultado.
        else -> {
            var u = cubicBezier(x1, y1, x2, y2, t)
            repeat(power.coerceIn(1, 3) - 1) { u = cubicBezier(x1, y1, x2, y2, u) }
            u
        }
    }

    /**
     * As alças mostradas: as da bézier. A reta mostra a bézier equivalente
     * (⅓,⅓)(⅔,⅔) — nas pontas as alças ficavam em cima das marcas, sem ter
     * onde pôr o dedo.
     */
    fun handles(): FloatArray = when (interp) {
        Interp.LINEAR -> floatArrayOf(1f / 3f, 1f / 3f, 2f / 3f, 2f / 3f)
        Interp.EASE_IN -> floatArrayOf(1f / 3f, 0f, 2f / 3f, 1f / 3f)
        Interp.EASE_OUT -> floatArrayOf(1f / 3f, 2f / 3f, 2f / 3f, 1f)
        // O suave nos dois lados do motor são duas parábolas (2t² / 1−2(1−t)²);
        // a bézier simétrica mais próxima delas (erro máximo 0,003) — a
        // (0,5;0)(0,5;1) de antes errava 0,02, e o primeiro arrasto da alça
        // mudava o trecho sem movê-la.
        Interp.EASE_IN_OUT -> floatArrayOf(0.484f, 0.042f, 0.516f, 0.958f)
        else -> floatArrayOf(x1, y1, x2, y2)
    }

    /** Alças espelhadas; nulo significa simétrica ou família sem inversão disponível. */
    fun inverted(): Ease? = when (interp) {
        Interp.BOUNCE -> bounce().copy(x2 = if (y2 == -10f && x2 < .5f) 1f else 0f)
        Interp.OVERSHOOT -> overshoot().copy(x2 = if (marked && x2 < .5f) 1f else 0f)
        Interp.ELASTIC -> if (marked) copy(x2 = if (x2 < .5f) 1f else 0f) else null
        Interp.EASE_IN -> copy(interp = Interp.EASE_OUT)
        Interp.EASE_OUT -> copy(interp = Interp.EASE_IN)
        Interp.BEZIER, Interp.CUSTOM -> {
            val m = Ease(Interp.BEZIER, 1f - x2, 1f - y2, 1f - x1, 1f - y1, power)
            if (abs(m.x1 - x1) < 0.01f && abs(m.y1 - y1) < 0.01f && abs(m.x2 - x2) < 0.01f && abs(m.y2 - y2) < 0.01f) null else m
        }
        else -> null
    }

    fun same(o: Ease): Boolean {
        if (interp != o.interp) return false
        if (!isBezier && !isParametric) return true
        return power == o.power && abs(x1 - o.x1) < 0.01f && abs(y1 - o.y1) < 0.01f && abs(x2 - o.x2) < 0.01f && abs(y2 - o.y2) < 0.01f
    }
}

/** Um preset de uma família: nome [A] e o easing do motor. */
/** [name] nulo = preset salvo (o nome dele está em [entry]). */
private class CurvePreset(@StringRes val name: Int?, val ease: Ease, val entry: com.aurea.aurea.presets.PresetEntry? = null)

private class CurveFamily(@StringRes val name: Int, val glyph: Char, val presets: List<CurvePreset>)

private fun bez(x1: Float, y1: Float, x2: Float, y2: Float) = Ease(Interp.BEZIER, x1, y1, x2, y2)

/**
 * Os presets prontos do menu "Presets" do editor (do app antigo): um menu
 * compacto, não blocos — o painel mantém o tamanho. O Bounce segue no botão.
 */
internal val StandardCurves: List<Pair<Int, Ease>> = listOf(
    R.string.pn_curve_std_smooth to bez(0.33f, 0f, 0.66f, 1f),
    R.string.pn_curve_std_in to bez(0.42f, 0f, 1f, 1f),
    R.string.pn_curve_std_out to bez(0f, 0f, 0.58f, 1f),
    R.string.pn_curve_std_overshoot to bez(0.34f, 1.56f, 0.64f, 1f),
    R.string.pn_curve_std_anticipate to bez(0.36f, 0f, 0.66f, -0.56f),
)

/**
 * O que vai para o preset de curva salvo ([interp, x1, y1, x2, y2, força]),
 * o inverso de [curvePresetEase]: a interpolação como está, as alças
 * mostradas e a força só na bézier.
 */
internal fun curvePresetValues(e: Ease): FloatArray {
    val h = e.handles()
    return floatArrayOf(e.interp.toFloat(), h[0], h[1], h[2], h[3], (if (e.isBezier) e.power else 1).toFloat())
}

/**
 * AS FAMÍLIAS DA CURVA [A] (`_familiasDaCurva`). As bézier da A.01 são as
 * mesmas alças (0,42 / 0,58); as famílias Bounce, Elastic e Steps usam
 * os avaliadores compartilhados reais (interp7/8/9), sem alças fictícias.
 */
private val Families = listOf(
    CurveFamily(
        R.string.pn_curve_family_bezier, CupertinoGlyph.Scribble,
        listOf(
            CurvePreset(R.string.panel_linear, Ease(Interp.LINEAR, 0f, 0f, 1f, 1f)),
            CurvePreset(R.string.pn_ease_in, bez(0.42f, 0f, 1f, 1f)),
            CurvePreset(R.string.pn_ease_out, bez(0f, 0f, 0.58f, 1f)),
            CurvePreset(R.string.pn_ease_in_out, bez(0.42f, 0f, 0.58f, 1f)),
        ),
    ),
    CurveFamily(R.string.pn_textpreset_bounce, CupertinoGlyph.Scribble,
        listOf(CurvePreset(R.string.pn_textpreset_bounce, Ease(Interp.BOUNCE,.375f,.5f,1f,-10f)),
               CurvePreset(R.string.pn_textpreset_elastic, Ease(Interp.ELASTIC,0f,0f,1f,1f)))),
    CurveFamily(R.string.pn_curve_steps4, CupertinoGlyph.ChartBarAltFill,
        listOf(CurvePreset(R.string.pn_curve_steps4, Ease(Interp.STEPS,0f,0f,1f,1f)),
               CurvePreset(R.string.pn_ease_hold, Ease(Interp.HOLD,0f,0f,1f,1f)))) ,
)

@StringRes
private fun nameOf(e: Ease): Int {
    for (f in Families) for (p in f.presets) if (e.same(p.ease) && p.name != null) return p.name
    return when (e.interp) {
        Interp.BOUNCE -> R.string.pn_textpreset_bounce
        Interp.ELASTIC -> R.string.pn_textpreset_elastic
        Interp.OVERSHOOT -> R.string.curve_type_overshoot
        Interp.EASE_IN -> R.string.pn_ease_in
        Interp.EASE_OUT -> R.string.pn_ease_out
        Interp.EASE_IN_OUT -> R.string.pn_ease_in_out
        else -> R.string.pn_ease_bezier_custom
    }
}

/** Área de transferência da curva (Copiar curva / Colar curva). */
private object CurveClipboard {
    var ease: Ease? = null
}

/** Reads the authoritative project curve, including after undo and reopen. */
internal fun easeOf(store: EditorStore, layer: Long, k: KeyframeRow): Ease {
    val h = store.queryKeyframeEasing(layer, k)
    return Ease(k.interpolation, h?.get(0) ?: 0.33f, h?.get(1) ?: 0f,
        h?.get(2) ?: 0.67f, h?.get(3) ?: 1f, h?.getOrNull(4)?.toInt()?.coerceIn(1, 3) ?: 1)
}

/**
 * Grava o trecho que sai de [start]. A curva é da PROPRIEDADE: o motor leva a
 * mesma curva a todos os eixos dela com keyframe no mesmo instante (Posição
 * X/Y/Z, Escala, Rotação, Âncora — também numa camada 2D), para que X e Y
 * cheguem juntos (beta: "o Y não acompanha o X"). Componentes de efeito
 * continuam independentes (o motor não os liga).
 */
internal fun applyEase(store: EditorStore, layer: Long, start: KeyframeRow, e: Ease) {
    val keys = store.keyframes[layer] ?: return
    keys.filter { it.time == start.time && it.sameTrack(start) }.forEach { k ->
        store.setKeyframeEasing(layer, k, e.interp, e.x1, e.y1, e.x2, e.y2, e.power, linkAxes = true)
    }
}

/**
 * A curva em TODOS os trechos da propriedade de [start] (e dos eixos do grupo),
 * num passo de desfazer. Falso = menos de 2 keyframes (nada a aplicar).
 */
internal fun applyEaseToProperty(store: EditorStore, layer: Long, start: KeyframeRow, e: Ease): Boolean {
    val keys = store.keyframes[layer] ?: return false
    val starts = propertySegmentStarts(keys, keys.track(start))
    if (starts.isEmpty()) return false
    store.setKeyframesEasing(layer, starts, e.interp, e.x1, e.y1, e.x2, e.y2, e.power, linkAxes = true)
    return true
}

/**
 * O EDITOR DE CURVA (Omega painel, área 04, P1-11; `fragment_animation_curve`
 * da referência), na altura dos painéis de propriedade (200 dp):
 *
 *   ┌60──┬──── gráfico (fundo P10) ───────────┬60 presets┬60 grupos┐
 *   │ ‹  │ [parâmetros, só nos tipos com eles] │ curva 0   │ Curvas  │◇ cue
 *   │ ◆± │  grade 32×32, curva, alças          │ curva 1   │ Saltos  │
 *   │ ⇄  │                                     │ curva 2   │ Degraus │
 *   │ ⋯  │ [◀]      RÓTULO (9, 50 %)      [▶] │ curva 3   │         │
 *   └────┴─────────────────────────────────────┴───────────┴─────────┘
 *
 * O ⋯ traz Copiar, Colar, Colar em todos os keyframes e Passar do ponto, e os
 * extras do AUREA (Valor, Velocidade, Presets, Curvas salvas, Salvar como
 * preset, Tela cheia). No trecho de VELOCIDADE só o grupo Curvas responde (as
 * outras curvas não se dividem). O keyframe vem de `store.selectedKeyframe`.
 */
private val CurveGreen get() = AureaColors.Accent
private val CurvePanelFill get() = AureaColors.EditorPanel
private val CurveRailFill get() = AureaColors.EditorPanelHigh

/** "Passar do ponto" do ⋯: desligado, a alça fica em 0..1 (como a referência sem *overshoot*). */
internal object CurveSettings {
    var overshoot by mutableStateOf(true)
}

@Composable
internal fun CurvePanel(env: PanelEnv) { ReferenceCurvePanel(env) }

@Composable
private fun ReferenceCurvePanel(env: PanelEnv, expanded: Boolean = false, collapse: () -> Unit = {}) {
    var fullscreen by remember { mutableStateOf(false) }
    if (fullscreen) androidx.compose.ui.window.Dialog(onDismissRequest = { fullscreen = false },
        properties = androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth = false)) {
        Box(Modifier.fillMaxSize().background(CurvePanelFill).padding(8.dp)) {
            ReferenceCurvePanel(env, expanded = true, collapse = { fullscreen = false })
        }
    }
    val store = env.store
    val back = {
        val r = env.returnTo()
        if (expanded) collapse() else if (r != null && r != EditorPanel.Curve) env.onOpenPanel(r) else env.onClose()
    }
    // O trecho: a marca escolhida (relida FRESCA do store) e a seguinte na trilha.
    val focusKey = store.selectedKeyframe?.second
    androidx.compose.runtime.DisposableEffect(store, focusKey?.property, focusKey?.effectIndex, focusKey?.paramIndex) {
        store.timelineFocus = focusKey?.let { listOf(com.aurea.aurea.engine.TrackKey(it.property, it.effectIndex, it.paramIndex)) }
        onDispose { store.timelineFocus = null }
    }
    // O gráfico segue o que a pessoa olha: outra camada traz a trilha animada
    // dela; o cabeçote parado noutro trecho escolhe esse trecho.
    LaunchedEffect(store) { snapshotFlow { store.primary }.collect { followGraphLayer(store) } }
    LaunchedEffect(store) {
        snapshotFlow { store.playhead to store.playing }.drop(1)
            .collect { (_, playing) -> if (!playing) followGraphPlayhead(store) }
    }
    val segment by remember(store) {
        derivedStateOf {
            val (layer, sel) = store.selectedKeyframe ?: return@derivedStateOf null
            val keys = store.keyframes[layer] ?: return@derivedStateOf null
            val track = keys.track(sel)
            val detail = store.queryDetail(layer) ?: return@derivedStateOf null
            val target = com.aurea.aurea.engine.TrackKey(sel.property, sel.effectIndex, sel.paramIndex)
            val navigation = store.keyframeNavigation(layer, target, detail.timelineFrame(sel.time)) ?: return@derivedStateOf null
            if (!navigation.hasSegment) return@derivedStateOf null
            val a = track.firstOrNull { it.time == navigation.segmentStart } ?: return@derivedStateOf null
            val b = track.firstOrNull { it.time == navigation.segmentEnd } ?: return@derivedStateOf null
            Triple(layer, a, b)
        }
    }
    val seg = segment
    if (seg == null) {
        // Propriedade com menos de 2 keyframes: não há curva — avisa e volta.
        if (store.selectedKeyframe != null) {
            val needTwo = stringResource(R.string.pn_curve_need_two_keys)
            LaunchedEffect(Unit) {
                store.showToast(needTwo)
                back()
            }
        }
        Column(Modifier.fillMaxSize().background(AureaColors.PanelFrameBackground), verticalArrangement = Arrangement.Center, horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                if (store.selectedKeyframe == null) stringResource(R.string.panel_toque_num_keyframe_timeline_npara_editar) else stringResource(R.string.panel_crie_pelo_menos_2_keyframes_npara),
                textAlign = TextAlign.Center,
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)),
            )
            Spacer(Modifier.height(12.dp))
            Text(
                stringResource(R.string.panel_voltar),
                modifier = Modifier.tocavel { back() }.padding(horizontal = 16.dp, vertical = 8.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Accent)),
            )
        }
        return
    }
    val (layer, start, end) = seg
    val ease = remember(layer, start, store.curveRevision) { easeOf(store, layer, start) }
    // A curva que se desenha é a do MOTOR (a mesma conta que anima o projeto).
    val samples = remember(ease) { engineEaseSamples(ease) }
    val graphMode = store.curveGraphMode
    val copiedMsg = stringResource(R.string.curve_copied)
    val symmetricMsg = stringResource(R.string.pn_curve_symmetric)
    val needTwoMsg = stringResource(R.string.pn_curve_need_two_keys)
    var savePrompt by remember { mutableStateOf(false) }
    // As curvas que a pessoa salvou (Presets › Curva): lidas uma vez por lista.
    val savedEntries = store.presets.user[com.aurea.aurea.presets.PresetKind.Curve].orEmpty()
    val saved = remember(savedEntries) {
        savedEntries.mapNotNull { e -> store.curveOfPreset(e)?.takeIf { it.size >= 5 }?.let { e to curvePresetEase(it) } }
    }
    fun setCurve(label: String, e: Ease) {
        store.beginGesture(label)
        applyEase(store, layer, start, e)
        store.endGesture()
    }

    // Onde o cabeçote está dentro do trecho (0..1), lido NO DESENHO: só repinta.
    val progress: () -> Float? = {
        val t = store.detail?.localPlayhead
        if (t == null || t < start.time || t >= end.time || end.time <= start.time) null
        else (t - start.time).toFloat() / (end.time - start.time)
    }

    fun jump(dir: Int) {
        val detail = store.queryDetail(layer) ?: return
        val target = com.aurea.aurea.engine.TrackKey(start.property, start.effectIndex, start.paramIndex)
        val edge = detail.timelineFrame(if (dir < 0) start.time else end.time)
        val navigation = store.keyframeNavigation(layer, target, edge) ?: return
        val frame = if (dir < 0) {
            if (!navigation.hasPrevious) return
            navigation.previousFrame
        } else {
            if (!navigation.hasNext) return
            edge
        }
        val segment = store.keyframeNavigation(layer, target, frame)?.takeIf { it.hasSegment } ?: return
        val a = store.keyframes[layer].orEmpty().track(start).firstOrNull { it.time == segment.segmentStart } ?: return
        store.selectKeyframe(layer, a)
        store.pause()
        store.seek(frame)
    }

    val trackTarget = com.aurea.aurea.engine.TrackKey(start.property, start.effectIndex, start.paramIndex)
    val curveDetail = store.queryDetail(layer)
    val before = curveDetail?.let { store.keyframeNavigation(layer, trackTarget, it.timelineFrame(start.time)) }?.hasPrevious == true
    val after = curveDetail?.let { store.keyframeNavigation(layer, trackTarget, it.timelineFrame(end.time)) }?.hasNext == true
    // Velocidade: só o grupo Curvas (as outras curvas não se dividem; `disableNonSplittableEasing`).
    val onlyBezier = start.property == com.aurea.aurea.engine.TrackProperty.SPEED
    val category = curveCategoryOf(ease)
    val track = store.keyframes[layer].orEmpty().track(start)
    val local = store.detail?.takeIf { it.id == layer }?.localPlayhead
    val keyHere = local != null && track.any { it.time == local }
    // ◆±: marca ou tira a chave da trilha do trecho no cabeçote (o valor que a curva dá ali).
    val toggleKey = {
        val p = progress()
        val value = when {
            p != null -> start.value + (end.value - start.value) * (samples?.let { easeSampleAt(it, p) } ?: ease.transform(p))
            local != null && local >= end.time -> end.value
            else -> start.value
        }
        store.toggleTrackKeyAtPlayhead(layer, trackTarget, value)
    }
    val menuEntries = buildList<MenuEntry> {
        add(MenuEntry.Item(stringResource(R.string.panel_copiar_curva), glyph = CupertinoGlyph.DocOnDoc, tag = "curve.copy") {
            CurveClipboard.ease = ease
            store.showToast(copiedMsg)
        })
        add(MenuEntry.Item(stringResource(R.string.panel_colar_curva), glyph = CupertinoGlyph.DocOnClipboard,
            enabled = CurveClipboard.ease != null, tag = "curve.paste") { CurveClipboard.ease?.let { setCurve("colar curva", it) } })
        add(MenuEntry.Item(stringResource(R.string.om_curve_paste_all), enabled = CurveClipboard.ease != null, tag = "curve.pasteAll") {
            CurveClipboard.ease?.let { if (!applyEaseToProperty(store, layer, start, it)) store.showToast(needTwoMsg) }
        })
        add(MenuEntry.Item(
            stringResource(if (CurveSettings.overshoot) R.string.om_curve_overshoot_off else R.string.om_curve_overshoot_on),
            tag = "curve.overshoot",
        ) { CurveSettings.overshoot = !CurveSettings.overshoot })
        add(MenuEntry.Divider)
        // Mais do AUREA.
        add(MenuEntry.Item(stringResource(R.string.panel_curva), checked = graphMode == 0, tag = "curve.mode.0") { store.curveGraphMode = 0 })
        add(MenuEntry.Item(stringResource(R.string.particular_curve_value), checked = graphMode == 1, tag = "curve.mode.1") { store.curveGraphMode = 1 })
        add(MenuEntry.Item(stringResource(R.string.panel_velocidade), checked = graphMode == 2, tag = "curve.mode.2") { store.curveGraphMode = 2 })
        add(MenuEntry.Item(stringResource(R.string.panel_curve_apply_property), tag = "curve.applyAll") {
            if (!applyEaseToProperty(store, layer, start, ease)) store.showToast(needTwoMsg)
        })
        add(MenuEntry.Item(stringResource(R.string.panel_presets), tag = "curve.presets",
            submenu = StandardCurves.map { (name, e) -> MenuEntry.Item(stringResource(name)) { setCurve("preset de curva", e) } }))
        if (saved.isNotEmpty()) {
            add(MenuEntry.Item(stringResource(R.string.om_curve_saved), tag = "curve.saved",
                submenu = saved.map { (entry, e) ->
                    MenuEntry.Item(entry.name, checked = ease.same(e)) {
                        setCurve("preset de curva", e)
                        store.presets.markUsed(entry)
                    }
                }))
        }
        add(MenuEntry.Item(stringResource(R.string.panel_salvar_curva_como_preset), tag = "curve.save") { savePrompt = true })
        add(MenuEntry.Item(stringResource(if (expanded) R.string.editor_sair_tela_cheia else R.string.panel_expandir), tag = "curve.fullscreen") {
            if (expanded) collapse() else fullscreen = true
        })
    }

    Row(Modifier.fillMaxSize().background(AureaColors.PanelFrameBackground).testTag("curve.panel")) {
        CurveLeftColumn(
            onBack = back,
            keyHere = keyHere,
            onKey = toggleKey,
            invertible = ease.supportsInversion,
            inverted = ease.supportsInversion && isInvertedEase(ease),
            onInvert = {
                val inv = ease.inverted()
                if (inv == null) store.showToast(symmetricMsg) else setCurve("inverter curva", inv)
            },
            menuEntries = menuEntries,
        )
        CurveDivider()
        Column(Modifier.weight(1f).fillMaxHeight().background(AureaColors.CurveGraphBackground)) {
            // Os parâmetros (Quique, Elástico, Passar do ponto) numa faixa no TOPO do gráfico.
            if (graphMode == 0 && ease.isParametric) {
                Row(Modifier.fillMaxWidth().height(48.dp).padding(horizontal = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                    when (ease.interp) {
                        Interp.OVERSHOOT -> CurveParamSlider(stringResource(R.string.curve_param_amount), ease.overshootAmount, 0f..1f, 0,
                            "curve.overshoot.amount", null, store, "overshoot") { applyEase(store, layer, start, ease.overshoot(amount = it)) }
                        Interp.ELASTIC -> {
                            CurveParamSlider(stringResource(R.string.curve_param_oscillations), ease.elasticCycles.toFloat(), 1f..8f, 6, "curve.elastic.cycles",
                                stringResource(R.string.curve_param_oscillations_value, ease.elasticCycles), store, "elastic") {
                                val n = kotlin.math.round(it).toInt().coerceIn(1, 8)
                                if (n != ease.elasticCycles) applyEase(store, layer, start, ease.elastic(cycles = n))
                            }
                            CurveParamSlider(stringResource(R.string.curve_param_damping), ease.elasticDamping, 0f..1f, 0, "curve.elastic.damping", null, store,
                                "elastic") { applyEase(store, layer, start, ease.elastic(damping = it)) }
                        }
                        else -> {
                            CurveChip("×${ease.bounceCount}", tag = "curve.bounce.count") {
                                setCurve("bounce", ease.bounce(count = ease.bounceCount % 8 + 1))
                            }
                            CurveParamSlider(stringResource(R.string.fx_amplitude), ease.bounceStrength, .1f.. .9f, 0, "curve.bounce.strength", null, store,
                                "bounce") { applyEase(store, layer, start, ease.bounce(strength = it)) }
                        }
                    }
                }
            }
            // O gráfico é tempo × valor: não espelha em árabe (ver `KeepLtr`).
            KeepLtr {
                val segmentBar: @Composable (Modifier) -> Unit = { barModifier ->
                    CurveSegmentBar(
                        modifier = barModifier,
                        before = before,
                        after = after,
                        label = if (graphMode == 0) {
                            if (!ease.hasHandles) stringResource(nameOf(ease)).uppercase() else cubicBezierLabel(ease)
                        } else stringResource(if (graphMode == 1) R.string.particular_curve_value else R.string.panel_velocidade).uppercase(),
                        powered = graphMode == 0 && ease.hasHandles,
                        onPower = { setCurve("força da curva", nextPower(ease)) },
                        onJump = { jump(it) },
                    )
                }
                Column(Modifier.weight(1f).fillMaxWidth()) {
                    Box(Modifier.weight(1f).fillMaxWidth()) {
                        if (graphMode != 0) {
                            TrackGraph(store, layer, track, graphMode == 2)
                        } else {
                            CurveGraph(
                                ease = ease,
                                samples = samples,
                                progress = progress,
                                overshoot = CurveSettings.overshoot,
                                onBegin = { store.beginGesture("curva") },
                                onChange = { applyEase(store, layer, start, it) },
                                onEnd = { store.endGesture() },
                            )
                        }
                    }
                    // Bugs 0.3.4 (bug 10): no gráfico de Valor/Velocidade a faixa ◀ ▶ ficava
                    // POR CIMA dos últimos ~30 dp do gráfico (e da legenda): os pontos de
                    // valor baixo caíam embaixo dela e o toque ia para as setas/rótulo.
                    // Aqui ela fica ABAIXO do gráfico (como antes da interface nova).
                    // 0.4.0: também na Curva. Por cima, a faixa tomava o toque da alça
                    // de baixo (y perto de 0): "o slider de baixo às vezes não mexe".
                    segmentBar(Modifier)
                }
            }
        }
        if (graphMode == 0) {
            CurveDivider()
            CurvePresetColumn(category, ease, enabled = !onlyBezier || category == CurveCategory.Curves) { slot ->
                if (!slot.ease.same(ease) || slot.ease.interp != ease.interp) setCurve("preset de curva", slot.ease)
            }
            CurveGroupColumn(category, onlyBezier) { c ->
                curveCategoryEase(c, ease)?.let { setCurve("tipo de curva", it) }
            }
        }
    }

    if (savePrompt) {
        AureaNamePrompt(
            title = stringResource(R.string.panel_salvar_curva_como_preset),
            initial = stringResource(nameOf(ease)),
            onConfirm = { name ->
                // A interpolação vai como está (nomeada, reta, manter ou bézier com as alças).
                val v = curvePresetValues(ease)
                store.savePreset(com.aurea.aurea.presets.PresetKind.Curve, name, store.curvePresetJson(name, v[0].toInt(), v[1], v[2], v[3], v[4], v[5].toInt()))
            },
            onDismiss = { savePrompt = false },
        )
    }
}

/** A curva já está do lado "invertido" (o ⇄ fica aceso): bézier que começa rápido, ou parâmetro de sentido. */
private fun isInvertedEase(e: Ease): Boolean = when (e.interp) {
    Interp.BOUNCE, Interp.ELASTIC, Interp.OVERSHOOT -> e.y2 == EASE_PARAM_MARKER && e.x2 < .5f
    Interp.EASE_OUT -> true
    Interp.BEZIER, Interp.CUSTOM -> e.y1 > e.x1 && e.y2 > e.x2
    else -> false
}

@Composable
private fun CurveDivider() {
    Box(Modifier.width(PanelFrameDefaults.Divider).fillMaxHeight().background(AureaColors.PanelFrameDivider))
}

/** A coluna esquerda do editor (60): ‹ · ◆± · ⇄ · ⋯, células que repartem a altura. */
@Composable
private fun CurveLeftColumn(
    onBack: () -> Unit,
    keyHere: Boolean,
    onKey: () -> Unit,
    invertible: Boolean,
    inverted: Boolean,
    onInvert: () -> Unit,
    menuEntries: List<MenuEntry>,
) {
    var menu by remember { mutableStateOf(false) }
    Column(Modifier.width(PanelFrameDefaults.RailWidth).fillMaxHeight()) {
        CurveCell("curve.back", stringResource(R.string.panel_voltar), onBack) {
            CupertinoIcon(CupertinoGlyph.ChevronLeft, PanelMetrics.RailIcon, AureaColors.Text)
        }
        CurveCell("curve.keyframe", stringResource(if (keyHere) R.string.panel_tirar_keyframe_daqui else R.string.panel_marcar_keyframe_aqui), onKey) {
            com.aurea.aurea.ui.ds.KeyframeDiamondIcon(
                if (keyHere) com.aurea.aurea.ui.ds.KeyframeLook.KeyHere else com.aurea.aurea.ui.ds.KeyframeLook.Animated,
                enabled = true, modifier = Modifier.size(PanelMetrics.RailIcon),
            )
        }
        CurveCell("curve.reverse", stringResource(R.string.om_curve_reverse), onInvert, enabled = invertible, alpha = if (invertible) 1f else .5f) {
            CupertinoIcon(CupertinoGlyph.ArrowRightArrowLeft, 24.dp, if (inverted) AureaColors.Accent else AureaColors.Text)
        }
        Box(Modifier.weight(1f).fillMaxWidth()) {
            val more = stringResource(R.string.editor_mais)
            Box(
                Modifier.fillMaxSize().testTag("curve.menu").semantics { contentDescription = more }.tocavel(shrink = 1f) { menu = true },
                contentAlignment = Alignment.Center,
            ) { CupertinoIcon(CupertinoGlyph.Ellipsis, PanelMetrics.RailIcon, AureaColors.Text) }
            AnchoredMenu(expanded = menu, onDismiss = { menu = false }, entries = menuEntries, tag = "curve.menu.list")
        }
    }
}

@Composable
private fun ColumnScope.CurveCell(
    tag: String,
    label: String,
    onClick: () -> Unit,
    enabled: Boolean = true,
    alpha: Float = 1f,
    content: @Composable () -> Unit,
) {
    Box(
        Modifier.weight(1f).fillMaxWidth().testTag(tag)
            .semantics { contentDescription = label }
            .tocavel(enabled = enabled, shrink = 1f, onClick = onClick)
            .alpha(alpha),
        contentAlignment = Alignment.Center,
    ) { content() }
}

/** ◀ trecho anterior · rótulo (9, 50 %) · trecho seguinte ▶, na base do gráfico (LTR). */
@Composable
private fun CurveSegmentBar(
    modifier: Modifier,
    before: Boolean,
    after: Boolean,
    label: String,
    powered: Boolean,
    onPower: () -> Unit,
    onJump: (Int) -> Unit,
) {
    val previousLabel = stringResource(R.string.om_curve_prev)
    val nextLabel = stringResource(R.string.om_curve_next)
    // 0.3.4 painéis: faixa de 44 (antes 40), setas 18 e rótulo 12 (antes 9).
    Row(modifier.fillMaxWidth().height(PanelMetrics.Touch), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(44.dp, PanelMetrics.Touch).testTag("curve.prev").semantics { contentDescription = previousLabel }.tocavel(enabled = before) { onJump(-1) },
            contentAlignment = Alignment.Center) {
            CupertinoIcon(CupertinoGlyph.ChevronLeft, 18.dp, if (before) AureaColors.Text else AureaColors.Disabled)
        }
        // A curva do trecho; tocar troca a força (×1 → ×2 → ×3) — extra do AUREA.
        Text(
            label,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f).testTag("curve.power").tocavel(enabled = powered, shrink = 1f, onClick = onPower).padding(vertical = 12.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = PanelMetrics.Note, fontWeight = FontWeight.W500, color = Color.White.copy(alpha = .5f), textAlign = TextAlign.Center)),
        )
        Box(Modifier.size(44.dp, PanelMetrics.Touch).testTag("curve.next").semantics { contentDescription = nextLabel }.tocavel(enabled = after) { onJump(1) },
            contentAlignment = Alignment.Center) {
            CupertinoIcon(CupertinoGlyph.ChevronRight, 18.dp, if (after) AureaColors.Text else AureaColors.Disabled)
        }
    }
}

/** A coluna de presets (60, 4 vagas do grupo ativo; a acesa com o painel a 50 %). */
@Composable
private fun CurvePresetColumn(category: CurveCategory, ease: Ease, enabled: Boolean, onPick: (CurveSlot) -> Unit) {
    Column(Modifier.width(PanelFrameDefaults.RailWidth).fillMaxHeight().background(AureaColors.CurveColumn)) {
        curveSlots(category).forEachIndexed { i, slot ->
            if (slot == null) {
                Spacer(Modifier.weight(1f).fillMaxWidth())
            } else {
                val active = curveSlotActive(ease, slot.ease)
                val name = stringResource(slot.name)
                val points = remember(slot.ease) { engineEaseSamples(slot.ease, 49) ?: FloatArray(49) { slot.ease.transform(it / 48f) } }
                Box(
                    Modifier.weight(1f).fillMaxWidth().testTag("curve.slot.$i")
                        .semantics { contentDescription = name; selected = active }
                        .tocavel(enabled = enabled, shrink = 1f) { onPick(slot) }
                        .alpha(if (enabled) 1f else .25f)
                        .padding(horizontal = 8.dp, vertical = 4.dp)
                        .clip(RoundedCornerShape(5.dp))
                        .background(if (active) AureaColors.PanelFrameBackground.copy(alpha = .5f) else Color.Transparent),
                    contentAlignment = Alignment.Center,
                ) {
                    CurveThumb(slot.ease, points, if (active) CurveGreen else Color.White)
                }
            }
        }
    }
}

/** A coluna de grupos (60): o ativo com o fundo A11 e o losango (cue) apontando para os presets. */
@Composable
private fun CurveGroupColumn(category: CurveCategory, onlyBezier: Boolean, onPick: (CurveCategory) -> Unit) {
    val cue = AureaColors.PanelFrameDivider
    Column(Modifier.width(PanelFrameDefaults.RailWidth).fillMaxHeight().background(AureaColors.CurveColumn)) {
        CurveCategory.entries.forEach { c ->
            val active = c == category
            val off = onlyBezier && c != CurveCategory.Curves
            val name = stringResource(when (c) {
                CurveCategory.Curves -> R.string.om_curve_cat_curves
                CurveCategory.Bounces -> R.string.om_curve_cat_bounces
                CurveCategory.Steps -> R.string.om_curve_cat_steps
            })
            val first = curveSlots(c).first()?.ease ?: Ease(Interp.LINEAR, 0f, 0f, 1f, 1f)
            val points = remember(c) { engineEaseSamples(first, 49) ?: FloatArray(49) { first.transform(it / 48f) } }
            Box(
                Modifier.weight(1f).fillMaxWidth().testTag("curve.category.${c.tag}")
                    .semantics {
                        contentDescription = name
                        selected = active
                        if (off) disabled()
                    }
                    .tocavel(enabled = !off, shrink = 1f) { onPick(c) }
                    .alpha(if (off) .25f else 1f)
                    .drawBehind {
                        if (!active) return@drawBehind
                        // O losango de 45° na borda que encosta nos presets (lado de início).
                        val s = 7.dp.toPx()
                        val x = if (layoutDirection == androidx.compose.ui.unit.LayoutDirection.Rtl) size.width else 0f
                        val path = Path().apply {
                            moveTo(x, size.height / 2f - s); lineTo(x + s, size.height / 2f)
                            lineTo(x, size.height / 2f + s); lineTo(x - s, size.height / 2f); close()
                        }
                        drawPath(path, cue)
                    }
                    .padding(horizontal = 8.dp, vertical = 4.dp)
                    .clip(RoundedCornerShape(5.dp))
                    .background(if (active) cue else Color.Transparent),
                contentAlignment = Alignment.Center,
            ) {
                CurveThumb(first, points, if (active) CurveGreen else Color.White)
            }
        }
    }
}

/** A miniatura de uma curva (o motor amostra), na faixa que cabe o *overshoot*. */
@Composable
private fun CurveThumb(ease: Ease, points: FloatArray, color: Color) {
    Canvas(Modifier.size(36.dp)) {
        val (lo, hi) = easeRange(ease, points)
        val pad = 4.dp.toPx()
        val w = size.width - 2 * pad
        val h = size.height - 2 * pad
        val path = Path()
        points.forEachIndexed { i, v ->
            val x = pad + w * i / (points.size - 1)
            val y = pad + h - (v - lo) / (hi - lo) * h
            if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
        }
        drawPath(path, color, style = Stroke(1.8.dp.toPx(), cap = StrokeCap.Round))
    }
}

/**
 * O GRÁFICO DA CURVA (`EasingCurveView` da referência): margem de 15 dp, grade
 * 32 × 32 (grossa a cada 8, branco 6 %; fora de 0..1 a 3 %), 0 e 1 tracejados,
 * a curva em destaque de 3 dp, alças de 12 dp (×1,2 no dedo) ligadas às marcas e
 * a linha tracejada do cabeçote. O toque pega a alça MAIS PERTO e o arrasto é
 * RELATIVO (posição de partida + deslocamento); começar e soltar = um desfazer.
 */
@Composable
private fun CurveGraph(
    ease: Ease,
    samples: FloatArray?,
    progress: () -> Float?,
    overshoot: Boolean,
    onBegin: () -> Unit,
    onChange: (Ease) -> Unit,
    onEnd: () -> Unit,
) {
    var activeRange by remember { mutableStateOf<Pair<Float, Float>?>(null) }
    /** A alça no dedo (0 saída, 1 chegada, −1 nenhuma): cresce 1,2×. */
    var activeHandle by remember { mutableIntStateOf(-1) }
    val fitted = remember(ease, samples) { easeRange(ease, samples) }
    val yMin = activeRange?.first ?: fitted.first
    val yMax = activeRange?.second ?: fitted.second
    val current by rememberUpdatedState(ease)
    val range by rememberUpdatedState(Pair(yMin, yMax))
    val allow by rememberUpdatedState(overshoot)
    val begin by rememberUpdatedState(onBegin)
    val change by rememberUpdatedState(onChange)
    val end by rememberUpdatedState(onEnd)
    val green = CurveGreen
    Box(
        Modifier
            .fillMaxSize()
            .testTag("curve.easingGraph")
            .pointerInput(Unit) {
                awaitEachGesture {
                    val down = awaitFirstDown()
                    val e0 = current
                    if (!e0.hasHandles || size.width <= 0 || size.height <= 0) return@awaitEachGesture
                    val (lo, hi) = range
                    val inset = CURVE_INSET.toPx()
                    val w = max(1f, size.width - 2 * inset)
                    val h = max(1f, size.height - 2 * inset)
                    fun plot(x: Float, y: Float) = Offset(inset + x * w, inset + (1f - (y - lo) / (hi - lo)) * h)
                    val hh = e0.handles()
                    val p1 = plot(hh[0], hh[1])
                    val p2 = plot(hh[2], hh[3])
                    val k0 = plot(0f, 0f)
                    val k1 = plot(1f, 1f)
                    val which = grabHandle(down.position.x, down.position.y, floatArrayOf(p1.x, p1.y, p2.x, p2.y), floatArrayOf(k0.x, k0.y, k1.x, k1.y))
                    down.consume()
                    val first = which == 0
                    val x0 = if (first) hh[0] else hh[2]
                    val y0 = if (first) hh[1] else hh[3]
                    activeHandle = which
                    activeRange = Pair(lo, hi)
                    var began = false
                    var e = Ease(Interp.BEZIER, hh[0], hh[1], hh[2], hh[3], if (e0.isBezier) e0.power else 1)
                    val snap = CURVE_SNAP.toPx()
                    try {
                        while (true) {
                            val ev = awaitPointerEvent()
                            val ch = ev.changes.firstOrNull { it.id == down.id } ?: break
                            if (!ch.pressed) break
                            ch.consume()
                            if (!began) {
                                if ((ch.position - down.position).getDistance() < viewConfiguration.touchSlop) continue
                                began = true
                                begin()
                            }
                            val d = ch.position - down.position
                            val p = dragHandle(x0, y0, d.x, d.y, w, h, lo, hi, snap, allow)
                            if (!p[0].isFinite() || !p[1].isFinite()) continue
                            e = if (first) e.copy(x1 = p[0], y1 = p[1]) else e.copy(x2 = p[0], y2 = p[1])
                            change(e)
                        }
                    } finally {
                        activeRange = null
                        activeHandle = -1
                        if (began) end()
                    }
                }
            },
    ) {
        Canvas(Modifier.fillMaxSize()) {
            val inset = CURVE_INSET.toPx()
            val w = max(1f, size.width - 2 * inset)
            val h = max(1f, size.height - 2 * inset)
            fun pt(x: Float, y: Float) = Offset(inset + x * w, inset + (1f - (y - yMin) / (yMax - yMin)) * h)
            val y0 = pt(0f, 0f).y
            val y1 = pt(0f, 1f).y
            drawGrid(inset, w, h, y1, y0)
            val curve = Path()
            for (i in 0..256) {
                val t = i / 256f
                val q = pt(t, samples?.getOrNull(i) ?: ease.transform(t))
                if (i == 0) curve.moveTo(q.x, q.y) else curve.lineTo(q.x, q.y)
            }
            val p0 = pt(0f, 0f)
            val p1 = pt(1f, 1f)
            if (ease.hasHandles) {
                val hh = ease.handles()
                val h1 = pt(hh[0], hh[1])
                val h2 = pt(hh[2], hh[3])
                // A linha da marca até a alça, na cor da alça.
                drawLine(Color.White.copy(alpha = .8f), p0, h1, 1.5.dp.toPx())
                drawLine(Color.White.copy(alpha = .8f), p1, h2, 1.5.dp.toPx())
            }
            drawPath(curve, green, style = Stroke(3.dp.toPx(), cap = StrokeCap.Round))
            drawCircle(green, 3.dp.toPx(), p0)
            drawCircle(green, 3.dp.toPx(), p1)
            if (ease.hasHandles) {
                val hh = ease.handles()
                val r = 12.dp.toPx()
                for (k in 0..1) {
                    val c = if (k == 0) pt(hh[0], hh[1]) else pt(hh[2], hh[3])
                    val rr = if (activeHandle == k) r * 1.2f else r
                    drawCircle(Color.White, rr, c)
                    drawCircle(green, rr, c, style = Stroke(2.dp.toPx()))
                }
            }
        }
        // O cabeçote em camada própria: a linha tracejada no instante do trecho.
        Canvas(Modifier.fillMaxSize()) {
            val f = progress() ?: return@Canvas
            val inset = CURVE_INSET.toPx()
            val x = inset + f * max(1f, size.width - 2 * inset)
            drawLine(Color.White.copy(alpha = 0.6f), Offset(x, 0f), Offset(x, size.height),
                1.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(3.dp.toPx(), 3.dp.toPx())))
        }
    }
}

/**
 * A curva do trecho amostrada pelo MOTOR (`sample_keyframe_ease`): o gráfico
 * desenha exatamente o que anima o projeto. Nulo (sem a biblioteca nativa, nos
 * testes JVM) = a mesma conta em Kotlin ([Ease.transform]).
 */
internal fun engineEaseSamples(e: Ease, count: Int = CURVE_SAMPLES): FloatArray? = try {
    val out = FloatArray(count)
    if (com.aurea.aurea.engine.AureaEngine.nativeSampleEase(e.interp, e.x1, e.y1, e.x2, e.y2, e.power, out) == count) out else null
} catch (_: Throwable) { null }

/**
 * Um parâmetro da curva (Quantidade, Oscilações, Amortecimento): rótulo curto e
 * slider; um arrasto inteiro é UM passo de desfazer. [steps] > 0 = valores
 * inteiros; [state] = o valor falado pelo leitor de tela.
 */
@Composable
private fun androidx.compose.foundation.layout.RowScope.CurveParamSlider(
    label: String, value: Float, range: ClosedFloatingPointRange<Float>, steps: Int, tag: String,
    state: String?, store: EditorStore, gesture: String, onChange: (Float) -> Unit,
) {
    Text(label, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.padding(start = 4.dp).widthIn(max = 96.dp),
        style = AureaType.Base.merge(TextStyle(fontSize = PanelMetrics.Note, color = Color.White)))
    var changing by remember { mutableStateOf(false) }
    // 0.4.0: o dedo manda durante o arrasto; o valor do motor chega depois e
    // fazia o polegar voltar (parecia "travar").
    var dragValue by remember { mutableStateOf<Float?>(null) }
    val change by rememberUpdatedState(onChange)
    androidx.compose.material3.Slider(value = (dragValue ?: value).coerceIn(range.start, range.endInclusive), valueRange = range, steps = steps,
        onValueChange = { if (!changing) { changing = true; store.beginGesture(gesture) }; dragValue = it; change(it) },
        onValueChangeFinished = { dragValue = null; if (changing) { changing = false; store.endGesture() } },
        colors = androidx.compose.material3.SliderDefaults.colors(thumbColor = CurveGreen,
            activeTrackColor = CurveGreen, inactiveTrackColor = CurveRailFill),
        modifier = Modifier.weight(1f).testTag(tag).semantics {
            contentDescription = label
            if (state != null) stateDescription = state
        })
}

/** Pastilha da fileira de presets (mesmo desenho do botão Bounce), alvo de 40 dp. */
@Composable
private fun CurveChip(label: String, active: Boolean = false, tag: String, onTap: () -> Unit) {
    Box(Modifier.height(PanelMetrics.Touch).testTag(tag).tocavel { onTap() }, contentAlignment = Alignment.Center) {
        Text(
            label, maxLines = 1, overflow = TextOverflow.Ellipsis,
            modifier = Modifier.widthIn(max = 140.dp).clip(RoundedCornerShape(14.dp))
                .background(if (active) CurveGreen.copy(alpha = .18f) else CurveRailFill)
                .border(1.dp, if (active) CurveGreen else Color.White.copy(alpha = .18f), RoundedCornerShape(14.dp))
                .padding(horizontal = 12.dp, vertical = 7.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = PanelMetrics.Label, fontWeight = FontWeight.W600,
                color = if (active) CurveGreen else Color.White)),
        )
    }
}

/** Margem do gráfico da curva (a da referência): as pontas e as alças em 0 / 1 ficam tocáveis. */
private val CURVE_INSET = 15.dp
/** Raio do halo da alça no dedo. */
private val HANDLE_HIT = 28.dp
/** Encaixe da alça nas linhas 0 e 1 (x e y). */
private val CURVE_SNAP = 10.dp

/**
 * A grade da referência: 32 × 32 na área do gráfico, grossa (1 dp) a cada 8 e
 * fina (0,5 dp); branco 6 % na faixa 0..1 e 3 % fora dela; 0 e 1 tracejados a 25 %.
 */
private fun DrawScope.drawGrid(inset: Float, w: Float, h: Float, yTop: Float, yBottom: Float) {
    val thick = 1.dp.toPx()
    val thin = 0.5.dp.toPx()
    fun lines(alpha: Float) {
        val c = Color.White.copy(alpha = alpha)
        for (i in 0..32) {
            val sw = if (i % 8 == 0) thick else thin
            val x = inset + w * i / 32f
            val y = inset + h * i / 32f
            drawLine(c, Offset(x, inset), Offset(x, inset + h), sw)
            drawLine(c, Offset(inset, y), Offset(inset + w, y), sw)
        }
    }
    lines(0.03f)
    // Dentro da faixa 0..1 a grade fica mais forte (3 % + 3 % ≈ 6 %).
    clipRect(top = minOf(yTop, yBottom), bottom = maxOf(yTop, yBottom)) { lines(0.03f) }
    val lim = Color.White.copy(alpha = 0.25f)
    val dash = PathEffect.dashPathEffect(floatArrayOf(2.dp.toPx(), 2.dp.toPx()))
    for (py in floatArrayOf(yTop, yBottom)) drawLine(lim, Offset(inset, py), Offset(inset + w, py), thick, pathEffect = dash)
}

/** Instante LOCAL do cabeçote na camada [layer] (o tempo das marcas dela). */
private fun localPlayhead(store: EditorStore, layer: Long): Int? {
    val row = store.layers.firstOrNull { it.id == layer } ?: return null
    return store.playhead - row.startFrame + row.offsetFrames
}

/** Outra camada escolhida: o gráfico passa para a trilha animada dela (a mesma, se houver). */
private fun followGraphLayer(store: EditorStore) {
    val layer = store.primary ?: return
    val current = store.selectedKeyframe
    if (current?.first == layer) return
    val track = graphTrackFor(store.keyframes[layer].orEmpty(), current?.second)
    if (track.isEmpty()) { if (current != null) store.clearSelectedKeyframe(); return }
    val i = localPlayhead(store, layer)?.let { segmentIndexAt(track, it) } ?: 0
    store.selectKeyframe(layer, track[i.coerceAtLeast(0)])
}

/** Cabeçote parado noutro trecho da trilha mostrada: o painel mostra esse trecho. */
private fun followGraphPlayhead(store: EditorStore) {
    if (store.timelineKeyDragActive) return
    val (layer, sel) = store.selectedKeyframe ?: return
    if (layer != store.primary) return
    val track = store.keyframes[layer].orEmpty().track(sel)
    val target = com.aurea.aurea.engine.TrackKey(sel.property, sel.effectIndex, sel.paramIndex)
    val navigation = store.keyframeNavigation(layer, target)?.takeIf { it.hasSegment } ?: return
    val start = track.firstOrNull { it.time == navigation.segmentStart } ?: return
    if (start.time != sel.time) store.selectKeyframe(layer, start)
}
