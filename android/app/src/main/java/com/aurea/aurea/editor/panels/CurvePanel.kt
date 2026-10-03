package com.aurea.aurea.editor.panels

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

/** `elastic_ease` (core/Math.hpp): ciclos inteiros (1..8), amortecimento k = 2 + 16·B. */
internal fun elasticCurve(t: Float, cycles01: Float, damping01: Float, reverse: Boolean): Float {
    if (t <= 0f) return 0f
    if (t >= 1f) return 1f
    val n = kotlin.math.round(cycles01 * 8f).toInt().coerceIn(1, 8).toFloat()
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

    val elasticCycles get() = if (interp == Interp.ELASTIC && marked) kotlin.math.round(x1 * 8).toInt().coerceIn(1, 8) else ELASTIC_DEFAULT_CYCLES
    val elasticDamping get() = if (interp == Interp.ELASTIC && marked) y1.coerceIn(0f, 1f) else ELASTIC_DEFAULT_DAMPING
    fun elastic(cycles: Int = elasticCycles, damping: Float = elasticDamping) =
        Ease(Interp.ELASTIC, cycles.coerceIn(1, 8) / 8f, damping.coerceIn(0f, 1f), if (interp == Interp.ELASTIC && marked) x2 else 1f, EASE_PARAM_MARKER)

    val bounceCount get() = if (y2 == -10f) kotlin.math.round(x1 * 8).toInt().coerceIn(1, 8) else 3
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
        Interp.EASE_IN_OUT -> floatArrayOf(0.5f, 0f, 0.5f, 1f)
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
 * Write the outgoing segment; the store synchronizes grouped 3D axes.
 * Effects and planar tracks keep their independent curves.
 */
internal fun applyEase(store: EditorStore, layer: Long, start: KeyframeRow, e: Ease) {
    val keys = store.keyframes[layer] ?: return
    keys.filter { it.time == start.time && it.sameTrack(start) }.forEach { k ->
        store.setKeyframeEasing(layer, k, e.interp, e.x1, e.y1, e.x2, e.y2, e.power)
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
    store.setKeyframesEasing(layer, starts, e.interp, e.x1, e.y1, e.x2, e.y2, e.power)
    return true
}

/**
 * O EDITOR DE CURVA [A] ("Easing curve", 11): trilho esquerdo (‹ · ⇄ · ⋯), gráfico
 * com grade tracejada, curva `destaque` e as duas alças brancas, o nome do preset
 * entre ‹ › (trocam de TRECHO, como na A.01), e à direita os presets da família em
 * miniatura com as abas das famílias.
 *
 * O keyframe vem de `store.selectedKeyframe` (losango tocado na timeline, ou o
 * trecho sob o cabeçote escolhido pelo trilho do painel de origem).
 */
private val CurveGreen get() = AureaColors.Accent
private val CurvePanelFill get() = AureaColors.EditorPanel
private val CurveRailFill get() = AureaColors.EditorPanelHigh

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
    // O gráfico segue o que a pessoa olha, não o que estava aberto quando o
    // painel abriu: outra camada escolhida (toque, setas ‹ ›, arrasto vertical
    // da fileira compacta) traz a trilha animada dela; o cabeçote parado noutro
    // trecho (scrub, rolar a timeline) escolhe esse trecho.
    LaunchedEffect(store) { snapshotFlow { store.primary }.collect { followGraphLayer(store) } }
    LaunchedEffect(store) {
        // O primeiro valor é o de quando o painel abriu: o trecho tocado vale.
        snapshotFlow { store.playhead to store.playing }.drop(1)
            .collect { (_, playing) -> if (!playing) followGraphPlayhead(store) }
    }
    val segment by remember(store) {
        derivedStateOf {
            val (layer, sel) = store.selectedKeyframe ?: return@derivedStateOf null
            val keys = store.keyframes[layer] ?: return@derivedStateOf null
            val track = keys.track(sel)
            var i = track.indexOfFirst { it.time == sel.time }
            if (track.size < 2) return@derivedStateOf null
            // The engine publishes on its next frame; keep the gesture mounted
            // while the selected key's new time is still in the command queue.
            if (i < 0) i = track.indexOfLast { it.time <= sel.time }.coerceAtLeast(0)
            if (i == track.lastIndex) i-- // a última marca não abre trecho: mostra o que chega nela
            Triple(layer, track[i], track[i + 1])
        }
    }
    val seg = segment
    if (seg == null) {
        // Propriedade com menos de 2 keyframes: não há curva — avisa e volta,
        // venha de onde vier (losango tocado, botão de curva de um painel).
        if (store.selectedKeyframe != null) {
            val needTwo = stringResource(R.string.pn_curve_need_two_keys)
            LaunchedEffect(Unit) {
                store.showToast(needTwo)
                back()
            }
        }
        Column(Modifier.fillMaxSize(), verticalArrangement = Arrangement.Center, horizontalAlignment = Alignment.CenterHorizontally) {
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
    var menu by remember { mutableStateOf(false) }
    var savePrompt by remember { mutableStateOf(false) }
    var presetMenu by remember { mutableStateOf(false) }
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
    val inside by remember(store, start, end) {
        derivedStateOf { store.detail?.localPlayhead?.let { it >= start.time && it < end.time } ?: false }
    }

    fun jump(dir: Int) {
        val track = (store.keyframes[layer] ?: emptyList()).track(start)
        val i = track.indexOfFirst { it.time == start.time }
        val target = (i + dir).coerceIn(0, max(0, track.size - 2))
        if (target == i || target >= track.lastIndex) return
        val a = track[target]
        val b = track[target + 1]
        store.selectKeyframe(layer, a)
        // Trocar de trecho é escolha da pessoa: aqui mover o cabeçote é legítimo
        // (pausado, para não disputar com o relógio).
        store.pause()
        store.detail?.let { d -> store.seek(d.timelineFrame(a.time + (b.time - a.time) / 2)) }
    }

    val symmetricMsg = stringResource(R.string.pn_curve_symmetric)
    val needTwoMsg = stringResource(R.string.pn_curve_need_two_keys)
    Column(Modifier.fillMaxSize().background(CurvePanelFill)) {
    Row(Modifier.fillMaxWidth().height(48.dp).background(CurveRailFill)) {
        listOf(R.string.panel_easing_curve, R.string.particular_curve_value, R.string.panel_velocidade).forEachIndexed { index, label ->
            Box(Modifier.weight(1f).fillMaxHeight().tocavel { store.curveGraphMode = index }, contentAlignment = Alignment.Center) {
                Text(stringResource(label), maxLines = 1, overflow = TextOverflow.Ellipsis,
                    style = AureaType.Base.merge(TextStyle(fontSize = 12.sp,
                        color = if (graphMode == index) AureaColors.Accent else AureaColors.Muted)))
            }
        }
    }
    Row(Modifier.fillMaxWidth().weight(1f)) {
        // Trilho esquerdo: voltar · inverter · menu.
        Column(Modifier.width(48.dp).fillMaxHeight().background(CurveRailFill), horizontalAlignment = Alignment.CenterHorizontally) {
            Spacer(Modifier.height(6.dp))
            Box(Modifier.size(48.dp).tocavel { back() }, contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.ChevronBack, 24.dp, Color.White)
            }
            Spacer(Modifier.weight(1f))
            Box(
                Modifier.size(48.dp).tocavel(enabled = ease.supportsInversion) {
                    val inv = ease.inverted()
                    if (inv == null) store.showToast(symmetricMsg)
                    else {
                        store.beginGesture("inverter curva")
                        applyEase(store, layer, start, inv)
                        store.endGesture()
                    }
                },
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.ArrowRightArrowLeft, 20.dp, Color.White.copy(alpha = if (ease.supportsInversion) 1f else .35f))
            }
            Spacer(Modifier.height(4.dp))
            Box(Modifier.size(48.dp).tocavel { menu = true }, contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.Ellipsis, 24.dp, Color.White)
            }
            Spacer(Modifier.height(8.dp))
        }
        Column(Modifier.weight(1f).fillMaxHeight()) {
            Box(Modifier.weight(1f).fillMaxWidth()) {
                if (graphMode != 0) {
                    TrackGraph(store, layer, (store.keyframes[layer] ?: emptyList()).track(start), graphMode == 2)
                } else CurveGraph(
                    ease = ease,
                    samples = samples,
                    progress = progress,
                    onBegin = { store.beginGesture("curva") },
                    onChange = { applyEase(store, layer, start, it) },
                    onEnd = { store.endGesture() },
                )
            }
            // Parâmetros das curvas que passam do ponto / balançam (um desfazer por arrasto).
            if (graphMode == 0 && ease.interp == Interp.OVERSHOOT) {
                Row(Modifier.fillMaxWidth().height(48.dp).padding(horizontal = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                    CurveParamSlider(stringResource(R.string.curve_param_amount), ease.overshootAmount, 0f..1f, 0, "curve.overshoot.amount", null, store,
                        "overshoot") { applyEase(store, layer, start, ease.overshoot(amount = it)) }
                }
            }
            if (graphMode == 0 && ease.interp == Interp.ELASTIC) {
                Row(Modifier.fillMaxWidth().height(48.dp).padding(horizontal = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                    CurveParamSlider(stringResource(R.string.curve_param_oscillations), ease.elasticCycles.toFloat(), 1f..8f, 6, "curve.elastic.cycles",
                        stringResource(R.string.curve_param_oscillations_value, ease.elasticCycles), store, "elastic") {
                        val n = kotlin.math.round(it).toInt().coerceIn(1, 8)
                        if (n != ease.elasticCycles) applyEase(store, layer, start, ease.elastic(cycles = n))
                    }
                    CurveParamSlider(stringResource(R.string.curve_param_damping), ease.elasticDamping, 0f..1f, 0, "curve.elastic.damping", null, store,
                        "elastic") { applyEase(store, layer, start, ease.elastic(damping = it)) }
                }
            }
            if (graphMode == 0 && ease.interp == Interp.BOUNCE) {
                Row(Modifier.fillMaxWidth().height(48.dp).padding(horizontal = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                    CurveChip(stringResource(R.string.curve_bounce_count) + " ×${ease.bounceCount}", tag = "curve.bounce.count") {
                        setCurve("bounce", ease.bounce(count = ease.bounceCount % 8 + 1))
                    }
                    Text(stringResource(R.string.fx_amplitude), modifier = Modifier.padding(start = 8.dp),
                        style = AureaType.Base.merge(TextStyle(fontSize = 10.sp, color = Color.White)))
                    var changing by remember { mutableStateOf(false) }
                    val bounceLabel = stringResource(R.string.pn_textpreset_bounce)
                    androidx.compose.material3.Slider(value = ease.bounceStrength, valueRange = .1f.. .9f,
                        onValueChange = { if (!changing) { changing = true; store.beginGesture("bounce") }; applyEase(store, layer, start, ease.bounce(strength = it)) },
                        onValueChangeFinished = { if (changing) { changing = false; store.endGesture() } },
                        colors = androidx.compose.material3.SliderDefaults.colors(thumbColor = CurveGreen,
                            activeTrackColor = CurveGreen, inactiveTrackColor = CurveRailFill),
                        modifier = Modifier.weight(1f).testTag("curve.bounce.strength").semantics { contentDescription = bounceLabel })
                }
            }
            // Presets prontos (menu), salvar a curva e as curvas salvas numa
            // fileira fina que rola: o painel não cresce.
            if (graphMode == 0) {
                Row(
                    Modifier.fillMaxWidth().height(40.dp).horizontalScroll(rememberScrollState()).padding(horizontal = 8.dp),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    CurveChip(stringResource(R.string.panel_presets) + " ▾", tag = "curve.presets") { presetMenu = true }
                    CurveChip("+ " + stringResource(R.string.pn_curve_save), tag = "curve.save") { savePrompt = true }
                    saved.forEach { (entry, e) ->
                        CurveChip(entry.name, active = ease.same(e), tag = "curve.saved") {
                            setCurve("preset de curva", e)
                            store.presets.markUsed(entry)
                        }
                    }
                }
            }
            Row(Modifier.fillMaxWidth().height(48.dp), horizontalArrangement = Arrangement.Center, verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(48.dp).tocavel { jump(-1) }, contentAlignment = Alignment.Center) {
                    CupertinoIcon(CupertinoGlyph.ChevronLeft, 16.dp, Color.White)
                }
                Spacer(Modifier.width(4.dp))
                val track = (store.keyframes[layer] ?: emptyList()).track(start)
                val n = track.indexOfFirst { it.time == start.time }
                // A curva do trecho em números; tocar troca a força (×1 → ×2 → ×3).
                val powered = graphMode == 0 && ease.hasHandles
                Text(
                    if (graphMode == 0) {
                        if (!ease.hasHandles) stringResource(nameOf(ease))
                        else cubicBezierLabel(ease)
                    } else stringResource(if (graphMode == 1) R.string.particular_curve_value else R.string.panel_velocidade),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f, fill = false)
                        .testTag("curve.power")
                        .tocavel(enabled = powered) {
                            store.beginGesture("força da curva")
                            applyEase(store, layer, start, nextPower(ease))
                            store.endGesture()
                        }
                        .padding(vertical = 14.dp),
                    style = AureaType.Base.merge(TextStyle(fontSize = 10.sp, fontWeight = FontWeight.W400, color = Color.White.copy(alpha = .6f), textAlign = TextAlign.Center)),
                )
                Spacer(Modifier.width(4.dp))
                Box(Modifier.size(48.dp).tocavel { jump(1) }, contentAlignment = Alignment.Center) {
                    CupertinoIcon(CupertinoGlyph.ChevronRight, 16.dp, Color.White)
                }
            }
        }
        // Os tipos de curva ao lado do gráfico (como no pedido do beta): cada
        // botão mostra a curva que aplica, desenhada pelo motor.
        if (graphMode == 0) CurveTypeRail(ease) { type ->
            val next = quickTypeEase(type, ease)
            if (!next.same(ease) || next.interp != ease.interp) setCurve("tipo de curva", next)
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

    if (presetMenu) {
        AureaActionSheet(
            title = stringResource(R.string.panel_presets),
            actions = StandardCurves.map { (name, e) ->
                SheetAction(stringResource(name)) { setCurve("preset de curva", e) }
            },
            onDismiss = { presetMenu = false },
        )
    }

    if (menu) {
        AureaActionSheet(
            title = stringResource(R.string.panel_curva),
            actions = listOf(
                SheetAction(stringResource(R.string.panel_curva)) { store.curveGraphMode = 0 },
                SheetAction(stringResource(R.string.particular_curve_value)) { store.curveGraphMode = 1 },
                SheetAction(stringResource(R.string.panel_velocidade)) { store.curveGraphMode = 2 },
                SheetAction(stringResource(if (expanded) R.string.editor_sair_tela_cheia else R.string.panel_expandir)) { if (expanded) collapse() else fullscreen = true },
                SheetAction(stringResource(R.string.panel_copiar_curva)) { CurveClipboard.ease = ease },
                SheetAction(stringResource(R.string.panel_salvar_curva_como_preset)) { savePrompt = true },
                SheetAction(stringResource(R.string.panel_colar_curva), enabled = CurveClipboard.ease != null) {
                    CurveClipboard.ease?.let {
                        store.beginGesture("colar curva")
                        applyEase(store, layer, start, it)
                        store.endGesture()
                    }
                },
                // Todos os trechos da propriedade (e dos eixos do grupo): um desfazer.
                SheetAction(stringResource(R.string.panel_curve_apply_property)) {
                    if (!applyEaseToProperty(store, layer, start, ease)) store.showToast(needTwoMsg)
                },
            ),
            onDismiss = { menu = false },
        )
    }
}

/**
 * O GRÁFICO [A] (`_AmCurvePainter`): grade pontilhada 8 × 8 `#34405A`, limites 0 e
 * 1 tracejados, área sob a curva a 10 %, curva `destaque` 3,5, alças brancas
 * (linha 2,5 e bola de 11) com as quedas tracejadas até a base, pontas `destaque`
 * e o ponto rosa que corre com o cabeçote.
 *
 * A alça é escolhida UMA vez, no toque (a mais perto); o resto do arrasto
 * obedece — escolher a cada passo fazia "a curva saltar" para a outra alça.
 */
@Composable
private fun CurveGraph(
    ease: Ease,
    samples: FloatArray?,
    progress: () -> Float?,
    onBegin: () -> Unit,
    onChange: (Ease) -> Unit,
    onEnd: () -> Unit,
) {
    var activeRange by remember { mutableStateOf<Pair<Float, Float>?>(null) }
    /** A alça no dedo (0 saída, 1 chegada, −1 nenhuma): ganha o halo. */
    var activeHandle by remember { mutableIntStateOf(-1) }
    // Faixa vertical ajustada à curva e às alças; parada enquanto o dedo arrasta.
    // A faixa cresce para mostrar o overshoot e as oscilações inteiras.
    val fitted = remember(ease, samples) { easeRange(ease, samples) }
    val yMin = activeRange?.first ?: fitted.first
    val yMax = activeRange?.second ?: fitted.second
    val current by rememberUpdatedState(ease)
    val range by rememberUpdatedState(Pair(yMin, yMax))
    // O gesto vive entre composições: trocar de trecho (‹ ›) troca o keyframe
    // que recebe a curva, e o gesto tem de escrever no NOVO.
    val begin by rememberUpdatedState(onBegin)
    val change by rememberUpdatedState(onChange)
    val end by rememberUpdatedState(onEnd)
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
                    val width = max(1f, size.width - 2 * inset)
                    fun plot(x: Float, y: Float) = Offset(inset + x * width, size.height - (y - lo) / (hi - lo) * size.height)
                    val hh = e0.handles()
                    val p1 = plot(hh[0], hh[1])
                    val p2 = plot(hh[2], hh[3])
                    val k0 = plot(0f, 0f)
                    val k1 = plot(1f, 1f)
                    // Qualquer toque no gráfico pega a alça mais perto de onde ela
                    // está DESENHADA (afastadas se coincidem) — como no app antigo.
                    val shown = separatedHandles(p1.x, p1.y, p2.x, p2.y, k0.x, k0.y, k1.x, k1.y, HANDLE_SEPARATION.toPx())
                    val which = grabHandle(down.position.x, down.position.y, shown)
                    down.consume()
                    val first = which == 0
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
                        // Um toque sem arrasto não mexe em nada.
                        if (!began) {
                            if ((ch.position - down.position).getDistance() < viewConfiguration.touchSlop) continue
                            began = true
                            begin()
                        }
                        // A alça vai para o dedo (x 0..1, y −2..3), encaixando em 0 e 1.
                        val p = handleAt(ch.position.x, ch.position.y, inset, size.width.toFloat(), size.height.toFloat(), lo, hi, snap)
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
        // A curva, a grade e as alças só mudam com o easing: redesenham raramente.
        Canvas(Modifier.fillMaxSize()) {
            val inset = CURVE_INSET.toPx()
            fun pt(x: Float, y: Float) = Offset(inset + x * max(1f, size.width - 2 * inset), size.height - (y - yMin) / (yMax - yMin) * size.height)
            drawGrid(pt(0f, 0f).y, pt(0f, 1f).y)
            val base = pt(0f, 0f).y
            val curve = Path()
            val area = Path().apply { moveTo(0f, base) }
            for (i in 0..256) {
                val t = i / 256f
                val q = pt(t, samples?.getOrNull(i) ?: ease.transform(t))
                if (i == 0) curve.moveTo(q.x, q.y) else curve.lineTo(q.x, q.y)
                area.lineTo(q.x, q.y)
            }
            area.lineTo(size.width, base)
            area.close()
            drawPath(area, CurveGreen.copy(alpha = 0.025f))
            drawPath(curve, CurveGreen, style = Stroke(3.5.dp.toPx(), cap = StrokeCap.Round))
            val p0 = pt(0f, 0f)
            val p1 = pt(1f, 1f)
            if (ease.hasHandles) {
                val hh = ease.handles()
                val real1 = pt(hh[0], hh[1])
                val real2 = pt(hh[2], hh[3])
                val shown = separatedHandles(real1.x, real1.y, real2.x, real2.y, p0.x, p0.y, p1.x, p1.y, HANDLE_SEPARATION.toPx())
                val h1 = Offset(shown[0], shown[1])
                val h2 = Offset(shown[2], shown[3])
                val dash = Color.White.copy(alpha = 0.30f)
                fun drop(hp: Offset) {
                    var y = min(hp.y, base)
                    val yEnd = max(hp.y, base)
                    while (y < yEnd) {
                        drawLine(dash, Offset(hp.x, y), Offset(hp.x, min(y + 3.dp.toPx(), yEnd)), 1.dp.toPx())
                        y += 6.dp.toPx()
                    }
                }
                drop(h1)
                drop(h2)
                // Cada alça ligada à SUA marca: a de saída (cheia) à primeira,
                // a de chegada (anel) à segunda — dá para saber qual é qual.
                drawLine(Color.White, p0, h1, 2.5.dp.toPx())
                drawLine(Color.White.copy(alpha = .7f), p1, h2, 2.5.dp.toPx())
                if (activeHandle >= 0) drawCircle(CurveGreen.copy(alpha = .22f), HANDLE_HIT.toPx() * .85f, if (activeHandle == 0) h1 else h2)
                drawCircle(Color.White, 11.dp.toPx(), h1)
                drawCircle(CurvePanelFill, 9.dp.toPx(), h2)
                drawCircle(Color.White, 9.dp.toPx(), h2, style = Stroke(3.dp.toPx()))
            }
            drawCircle(CurveGreen, 5.dp.toPx(), p0)
            drawCircle(CurveGreen, 5.dp.toPx(), p1)
        }
        // Guia do cabeçote em camada própria, sem cobrir os pontos da curva: a
        // linha e o ponto onde o cabeçote está NA curva.
        Canvas(Modifier.fillMaxSize()) {
            val f = progress() ?: return@Canvas
            val inset = CURVE_INSET.toPx()
            val v = samples?.let { easeSampleAt(it, f) } ?: ease.transform(f)
            val p = Offset(inset + f * max(1f, size.width - 2 * inset), size.height - (v - yMin) / (yMax - yMin) * size.height)
            drawLine(Color.White.copy(alpha = 0.4f), Offset(p.x, 0f), Offset(p.x, size.height),
                1.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(2.dp.toPx(), 4.dp.toPx())))
            drawCircle(Color.White, 4.dp.toPx(), p)
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

/** Nome, rótulo de acessibilidade e alvo de teste de cada tipo rápido. */
@StringRes
private fun quickTypeName(t: CurveQuickType): Int = when (t) {
    CurveQuickType.Linear -> R.string.panel_linear
    CurveQuickType.Ease -> R.string.curve_type_ease
    CurveQuickType.Overshoot -> R.string.curve_type_overshoot
    CurveQuickType.Elastic -> R.string.pn_textpreset_elastic
    CurveQuickType.Bounce -> R.string.pn_textpreset_bounce
}

private fun quickTypeTag(t: CurveQuickType): String = when (t) {
    // O Quique mantém o alvo antigo do botão (testes instrumentados).
    CurveQuickType.Bounce -> "curve.preset.bounce"
    else -> "curve.type." + t.name.lowercase()
}

/**
 * Os tipos de curva ao lado do gráfico: Linear, Suave, Passar do ponto,
 * Elástico e Quique. Cada botão desenha a curva padrão do tipo (amostrada pelo
 * motor); o tipo do trecho fica destacado. Rola se o painel for baixo.
 */
@Composable
private fun CurveTypeRail(ease: Ease, onPick: (CurveQuickType) -> Unit) {
    val current = quickTypeOf(ease)
    val railLabel = stringResource(R.string.curve_types_label)
    Column(
        Modifier.width(52.dp).fillMaxHeight().background(CurveRailFill)
            .verticalScroll(rememberScrollState()).padding(vertical = 4.dp)
            .semantics { contentDescription = railLabel },
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        CurveQuickType.entries.forEach { type ->
            val active = type == current
            val name = stringResource(quickTypeName(type))
            val preview = remember(type) { quickTypeEase(type, Ease(Interp.HOLD, 0f, 0f, 1f, 1f)) }
            val points = remember(preview) { engineEaseSamples(preview, 49) ?: FloatArray(49) { preview.transform(it / 48f) } }
            Box(
                Modifier.size(48.dp).testTag(quickTypeTag(type))
                    .semantics { contentDescription = name; selected = active }
                    .tocavel { onPick(type) },
                contentAlignment = Alignment.Center,
            ) {
                Canvas(
                    Modifier.size(40.dp).clip(RoundedCornerShape(10.dp))
                        .background(if (active) CurveGreen.copy(alpha = .18f) else Color.Transparent)
                        .border(1.dp, if (active) CurveGreen else Color.White.copy(alpha = .14f), RoundedCornerShape(10.dp)),
                ) {
                    val (lo, hi) = easeRange(preview, points)
                    val pad = 6.dp.toPx()
                    val w = size.width - 2 * pad
                    val h = size.height - 2 * pad
                    val path = Path()
                    points.forEachIndexed { i, v ->
                        val x = pad + w * i / (points.size - 1)
                        val y = pad + h - (v - lo) / (hi - lo) * h
                        if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
                    }
                    drawPath(path, if (active) CurveGreen else Color.White, style = Stroke(1.8.dp.toPx(), cap = StrokeCap.Round))
                }
            }
        }
    }
}

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
        style = AureaType.Base.merge(TextStyle(fontSize = 10.sp, color = Color.White)))
    var changing by remember { mutableStateOf(false) }
    val change by rememberUpdatedState(onChange)
    androidx.compose.material3.Slider(value = value.coerceIn(range.start, range.endInclusive), valueRange = range, steps = steps,
        onValueChange = { if (!changing) { changing = true; store.beginGesture(gesture) }; change(it) },
        onValueChangeFinished = { if (changing) { changing = false; store.endGesture() } },
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
    Box(Modifier.height(40.dp).testTag(tag).tocavel { onTap() }, contentAlignment = Alignment.Center) {
        Text(
            label, maxLines = 1, overflow = TextOverflow.Ellipsis,
            modifier = Modifier.widthIn(max = 140.dp).clip(RoundedCornerShape(14.dp))
                .background(if (active) CurveGreen.copy(alpha = .18f) else CurveRailFill)
                .border(1.dp, if (active) CurveGreen else Color.White.copy(alpha = .18f), RoundedCornerShape(14.dp))
                .padding(horizontal = 12.dp, vertical = 6.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600,
                color = if (active) CurveGreen else Color.White)),
        )
    }
}

/** Margem lateral do gráfico da curva: as pontas e as alças em x = 0 / 1 ficam tocáveis. */
private val CURVE_INSET = 28.dp
/** Raio do halo da alça no dedo. */
private val HANDLE_HIT = 28.dp
/** Distância mínima entre as alças DESENHADAS (as bolas nunca se sobrepõem). */
private val HANDLE_SEPARATION = 30.dp
/** Encaixe da alça nas linhas 0 e 1 (x e y). */
private val CURVE_SNAP = 10.dp

private fun DrawScope.drawGrid(y0: Float, y1: Float) {
    val w = 0.6.dp.toPx()
    val grid = Color.White.copy(alpha = 0.045f)
    for (i in 1 until 32) {
        val x = size.width * i / 32f
        val y = size.height * i / 32f
        drawLine(grid, Offset(x, 0f), Offset(x, size.height), w)
        drawLine(grid, Offset(0f, y), Offset(size.width, y), w)
    }
    val lim = Color.White.copy(alpha = 0.24f)
    for (py in floatArrayOf(y0, y1)) {
        var x = 0f
        while (x < size.width) { drawLine(lim, Offset(x, py), Offset(x + 4.5.dp.toPx(), py), w); x += 9.dp.toPx() }
    }
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
    val local = localPlayhead(store, layer) ?: return
    val want = segmentIndexAt(track, local)
    if (want >= 0 && want != segmentIndexOf(track, sel.time)) store.selectKeyframe(layer, track[want])
}
