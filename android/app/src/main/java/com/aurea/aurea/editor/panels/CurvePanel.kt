package com.aurea.aurea.editor.panels

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
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
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
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
import androidx.compose.ui.semantics.contentDescription
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
}

/**
 * Um easing como o motor o avalia (`apply_easing`): nomeado ou bézier com dois
 * pontos de controle. Para mostrar alças, os nomeados viram a bézier equivalente
 * (EaseIn t² = bézier (⅓,0)(⅔,⅓) exata).
 */
@Immutable
internal data class Ease(val interp: Int, val x1: Float, val y1: Float, val x2: Float, val y2: Float) {
    val isBezier get() = interp == Interp.BEZIER || interp == Interp.CUSTOM

    /** Tem alças arrastáveis (bézier ou reta, que vira bézier ao ser puxada). */
    val hasHandles get() = isBezier || interp == Interp.LINEAR || interp == Interp.EASE_IN || interp == Interp.EASE_OUT || interp == Interp.EASE_IN_OUT
    val supportsInversion get() = interp in Interp.LINEAR..Interp.CUSTOM

    fun transform(t: Float): Float = when (interp) {
        Interp.HOLD -> if (t < 1f) 0f else 1f
        Interp.LINEAR -> t
        Interp.EASE_IN -> t * t
        Interp.EASE_OUT -> 1f - (1f - t) * (1f - t)
        Interp.EASE_IN_OUT -> if (t < 0.5f) 2f * t * t else 1f - 2f * (1f - t) * (1f - t)
        Interp.BOUNCE -> {
            val u = t.coerceIn(0f, 1f)
            if (u < .5f) 4f*u*u else {
                val (start, span, height) = if (u < .75f) Triple(.5f,.25f,.25f) else if (u < .9f) Triple(.75f,.15f,.0625f) else Triple(.9f,.1f,.015625f)
                val p = (u-start)/span
                1f-4f*height*p*(1f-p)
            }
        }
        Interp.ELASTIC -> if (t <= 0f) 0f else if (t >= 1f) 1f else ((1-kotlin.math.exp(-6.0*t)*kotlin.math.cos(6*Math.PI*t))/(1-kotlin.math.exp(-6.0))).toFloat()
        Interp.STEPS -> kotlin.math.floor(t.coerceIn(0f,1f)*4f)/4f
        else -> cubicBezier(x1, y1, x2, y2, t)
    }

    /** As alças mostradas: as da bézier; a reta tem as alças nas pontas. */
    fun handles(): FloatArray = when (interp) {
        Interp.LINEAR -> floatArrayOf(0f, 0f, 1f, 1f)
        Interp.EASE_IN -> floatArrayOf(1f / 3f, 0f, 2f / 3f, 1f / 3f)
        Interp.EASE_OUT -> floatArrayOf(1f / 3f, 2f / 3f, 2f / 3f, 1f)
        Interp.EASE_IN_OUT -> floatArrayOf(0.5f, 0f, 0.5f, 1f)
        else -> floatArrayOf(x1, y1, x2, y2)
    }

    /** Alças espelhadas; nulo significa simétrica ou família sem inversão disponível. */
    fun inverted(): Ease? = when (interp) {
        Interp.EASE_IN -> copy(interp = Interp.EASE_OUT)
        Interp.EASE_OUT -> copy(interp = Interp.EASE_IN)
        Interp.BEZIER, Interp.CUSTOM -> {
            val m = Ease(Interp.BEZIER, 1f - x2, 1f - y2, 1f - x1, 1f - y1)
            if (abs(m.x1 - x1) < 0.01f && abs(m.y1 - y1) < 0.01f && abs(m.x2 - x2) < 0.01f && abs(m.y2 - y2) < 0.01f) null else m
        }
        else -> null
    }

    fun same(o: Ease): Boolean {
        if (interp != o.interp) return false
        if (!isBezier) return true
        return abs(x1 - o.x1) < 0.01f && abs(y1 - o.y1) < 0.01f && abs(x2 - o.x2) < 0.01f && abs(y2 - o.y2) < 0.01f
    }
}

/** Um preset de uma família: nome [A] e o easing do motor. */
/** [name] nulo = preset salvo (o nome dele está em [entry]). */
private class CurvePreset(@StringRes val name: Int?, val ease: Ease, val entry: com.aurea.aurea.presets.PresetEntry? = null)

private class CurveFamily(@StringRes val name: Int, val glyph: Char, val presets: List<CurvePreset>)

private fun bez(x1: Float, y1: Float, x2: Float, y2: Float) = Ease(Interp.BEZIER, x1, y1, x2, y2)

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
        listOf(CurvePreset(R.string.pn_textpreset_bounce, Ease(Interp.BOUNCE,0f,0f,1f,1f)),
               CurvePreset(R.string.pn_textpreset_elastic, Ease(Interp.ELASTIC,0f,0f,1f,1f)))),
    CurveFamily(R.string.pn_curve_steps4, CupertinoGlyph.ChartBarAltFill,
        listOf(CurvePreset(R.string.pn_curve_steps4, Ease(Interp.STEPS,0f,0f,1f,1f)),
               CurvePreset(R.string.pn_ease_hold, Ease(Interp.HOLD,0f,0f,1f,1f)))) ,
)

@StringRes
private fun nameOf(e: Ease): Int {
    for (f in Families) for (p in f.presets) if (e.same(p.ease) && p.name != null) return p.name
    return when (e.interp) {
        Interp.EASE_IN -> R.string.pn_ease_in
        Interp.EASE_OUT -> R.string.pn_ease_out
        Interp.EASE_IN_OUT -> R.string.pn_ease_in_out
        else -> R.string.pn_ease_bezier_custom
    }
}

private fun familyOf(e: Ease): Int = when (e.interp) {
    Interp.BOUNCE, Interp.ELASTIC -> 1
    Interp.HOLD, Interp.STEPS -> 2
    else -> 0
}

/** Área de transferência da curva (Copiar curva / Colar curva). */
private object CurveClipboard {
    var ease: Ease? = null
}

/** Reads the authoritative project curve, including after undo and reopen. */
internal fun easeOf(store: EditorStore, layer: Long, k: KeyframeRow): Ease {
    val h = store.queryKeyframeEasing(layer, k)
    return Ease(k.interpolation, h?.get(0) ?: 0.33f, h?.get(1) ?: 0f,
        h?.get(2) ?: 0.67f, h?.get(3) ?: 1f)
}

/**
 * Escreve o easing no trecho que SAI de [start], em todas as trilhas irmãs que
 * têm marca no mesmo instante (X e Y da posição andam juntos, como a A.01).
 */
internal fun applyEase(store: EditorStore, layer: Long, start: KeyframeRow, e: Ease) {
    val keys = store.keyframes[layer] ?: return
    keys.filter { it.time == start.time && it.sameGroup(start) }.forEach { k ->
        store.setKeyframeEasing(layer, k, e.interp, e.x1, e.y1, e.x2, e.y2)
    }
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
private val CurveGreen = Color(0xFF00EFA4)
private val CurvePanelFill = Color(0xFF373B55)
private val CurveRailFill = Color(0xFF2B3046)

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
    val segment by remember(store) {
        derivedStateOf {
            val (layer, sel) = store.selectedKeyframe ?: return@derivedStateOf null
            val keys = store.keyframes[layer] ?: return@derivedStateOf null
            val track = keys.track(sel)
            var i = track.indexOfFirst { it.time == sel.time }
            if (i < 0 || track.size < 2) return@derivedStateOf null
            if (i == track.lastIndex) i-- // a última marca não abre trecho: mostra o que chega nela
            Triple(layer, track[i], track[i + 1])
        }
    }
    val seg = segment
    if (seg == null) {
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
    var overshoot by rememberSaveable { mutableStateOf(false) }
    var graphMode by rememberSaveable(layer, start.property, start.effectIndex, start.paramIndex) { mutableIntStateOf(0) }
    var menu by remember { mutableStateOf(false) }
    var savePrompt by remember { mutableStateOf(false) }

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
    Row(Modifier.fillMaxSize().background(CurvePanelFill)) {
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
                CupertinoIcon(CupertinoGlyph.Ellipsis, 24.dp, if (overshoot) CurveGreen else Color.White)
            }
            Spacer(Modifier.height(8.dp))
        }
        Column(Modifier.weight(1f).fillMaxHeight()) {
            Box(Modifier.weight(1f).fillMaxWidth()) {
                if (graphMode != 0) {
                    TrackGraph(store, layer, (store.keyframes[layer] ?: emptyList()).track(start), graphMode == 2)
                } else CurveGraph(
                    ease = ease,
                    overshoot = overshoot,
                    progress = progress,
                    onBegin = { store.beginGesture("curva") },
                    onChange = { applyEase(store, layer, start, it) },
                    onEnd = { store.endGesture() },
                )
            }
            Row(Modifier.fillMaxWidth().height(48.dp), horizontalArrangement = Arrangement.Center, verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(48.dp).tocavel { jump(-1) }, contentAlignment = Alignment.Center) {
                    CupertinoIcon(CupertinoGlyph.ChevronLeft, 16.dp, Color.White)
                }
                Spacer(Modifier.width(4.dp))
                val track = (store.keyframes[layer] ?: emptyList()).track(start)
                val n = track.indexOfFirst { it.time == start.time }
                Text(
                    if (graphMode == 0) "Cubic Bezier Easing" else stringResource(if (graphMode == 1) R.string.particular_curve_value else R.string.panel_velocidade),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f, fill = false),
                    style = AureaType.Base.merge(TextStyle(fontSize = 10.sp, fontWeight = FontWeight.W400, color = Color.White.copy(alpha = .6f), textAlign = TextAlign.Center)),
                )
                Spacer(Modifier.width(4.dp))
                Box(Modifier.size(48.dp).tocavel { jump(1) }, contentAlignment = Alignment.Center) {
                    CupertinoIcon(CupertinoGlyph.ChevronRight, 16.dp, Color.White)
                }
            }
        }
        // Presets de curva (nativos + os salvos): a aba ★ das famílias.
        val saved = remember(store.presets.user) { store.presets.entries(com.aurea.aurea.presets.PresetKind.Curve).mapNotNull { entry ->
            store.curveOfPreset(entry)?.let { v -> CurvePreset(null, Ease(v[0].toInt(), v[1], v[2], v[3], v[4]), entry) }
        } }
        if (graphMode == 0) CurveFamilies(
            current = ease,
            saved = saved,
            onPick = { p ->
                store.beginGesture("curva")
                applyEase(store, layer, start, p.ease)
                store.endGesture()
                p.entry?.let { store.presets.markUsed(it) }
            },
        )
    }

    if (savePrompt) {
        AureaNamePrompt(
            title = stringResource(R.string.panel_salvar_curva_como_preset),
            initial = stringResource(nameOf(ease)),
            onConfirm = { name ->
                // A interpolação vai como está (nomeada, reta, manter ou bézier com as alças).
                val h = ease.handles()
                store.savePreset(com.aurea.aurea.presets.PresetKind.Curve, name, store.curvePresetJson(name, ease.interp, h[0], h[1], h[2], h[3]))
            },
            onDismiss = { savePrompt = false },
        )
    }

    if (menu) {
        AureaActionSheet(
            title = stringResource(R.string.panel_curva),
            actions = listOf(
                SheetAction(stringResource(R.string.panel_curva)) { graphMode = 0 },
                SheetAction(stringResource(R.string.particular_curve_value)) { graphMode = 1 },
                SheetAction(stringResource(R.string.panel_velocidade)) { graphMode = 2 },
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
                SheetAction(stringResource(R.string.panel_aplicar_todos_segmentos)) {
                    val track = (store.keyframes[layer] ?: emptyList()).track(start)
                    store.beginGesture("curva em todos")
                    track.dropLast(1).forEach { applyEase(store, layer, it, ease) }
                    store.endGesture()
                },
                SheetAction(if (overshoot) stringResource(R.string.panel_overshoot_9678) else stringResource(R.string.panel_overshoot)) { overshoot = !overshoot },
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
    overshoot: Boolean,
    progress: () -> Float?,
    onBegin: () -> Unit,
    onChange: (Ease) -> Unit,
    onEnd: () -> Unit,
) {
    val h = ease.handles()
    var activeRange by remember { mutableStateOf<Pair<Float, Float>?>(null) }
    val yMin = activeRange?.first ?: min(if (overshoot) -0.5f else -0.12f, min(h[1], h[3]) - 0.12f)
    val yMax = activeRange?.second ?: max(if (overshoot || ease.interp == Interp.ELASTIC) 1.5f else 1.12f, max(h[1], h[3]) + 0.12f)
    val current by rememberUpdatedState(ease)
    val over by rememberUpdatedState(overshoot)
    val range by rememberUpdatedState(Pair(yMin, yMax))
    // O gesto vive entre composições: trocar de trecho (‹ ›) troca o keyframe
    // que recebe a curva, e o gesto tem de escrever no NOVO.
    val begin by rememberUpdatedState(onBegin)
    val change by rememberUpdatedState(onChange)
    val end by rememberUpdatedState(onEnd)
    Box(
        Modifier
            .fillMaxSize()
            .pointerInput(Unit) {
                awaitEachGesture {
                    val down = awaitFirstDown()
                    val e0 = current
                    if (!e0.hasHandles || size.width <= 0 || size.height <= 0) return@awaitEachGesture
                    val (lo, hi) = range
                    val inset = 24.dp.toPx()
                    val width = max(1f, size.width - 2 * inset)
                    fun plot(x: Float, y: Float) = Offset(inset + x * width, size.height - (y - lo) / (hi - lo) * size.height)
                    val hh = e0.handles()
                    val p1 = plot(hh[0], hh[1])
                    val p2 = plot(hh[2], hh[3])
                    val d1 = (down.position - p1).getDistanceSquared()
                    val d2 = (down.position - p2).getDistanceSquared()
                    val radius = 24.dp.toPx()
                    if (min(d1, d2) > radius * radius) return@awaitEachGesture
                    down.consume()
                    val first = d1 <= d2
                    val grabOffset = (if (first) p1 else p2) - down.position
                    var began = false
                    var e = Ease(Interp.BEZIER, hh[0], hh[1], hh[2], hh[3])
                    try {
                      while (true) {
                        val ev = awaitPointerEvent()
                        val ch = ev.changes.firstOrNull { it.id == down.id } ?: break
                        if (!ch.pressed) break
                        ch.consume()
                        if (!began && (ch.position - down.position).getDistance() < viewConfiguration.touchSlop) continue
                        val position = ch.position + grabOffset
                        val x = ((position.x - inset) / width).coerceIn(0f, 1f)
                        var y = lo + (size.height - position.y) / size.height * (hi - lo)
                        if (!over) y = y.coerceIn(0f, 1f)
                        if (!x.isFinite() || !y.isFinite()) continue
                        if (!began) {
                            began = true
                            activeRange = Pair(lo, hi)
                            begin()
                        }
                        e = if (first) e.copy(x1 = x, y1 = y) else e.copy(x2 = x, y2 = y)
                        change(e)
                      }
                    } finally {
                        activeRange = null
                        if (began) end()
                    }
                }
            },
    ) {
        // A curva, a grade e as alças só mudam com o easing: redesenham raramente.
        Canvas(Modifier.fillMaxSize()) {
            val inset = 24.dp.toPx()
            fun pt(x: Float, y: Float) = Offset(inset + x * max(1f, size.width - 2 * inset), size.height - (y - yMin) / (yMax - yMin) * size.height)
            drawGrid(pt(0f, 0f).y, pt(0f, 1f).y)
            val base = pt(0f, 0f).y
            val curve = Path()
            val area = Path().apply { moveTo(0f, base) }
            for (i in 0..72) {
                val t = i / 72f
                val q = pt(t, ease.transform(t))
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
                val h1 = pt(hh[0], hh[1])
                val h2 = pt(hh[2], hh[3])
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
                drawLine(Color.White, p0, h1, 2.5.dp.toPx())
                drawLine(Color.White, p1, h2, 2.5.dp.toPx())
                drawCircle(Color.White, 11.dp.toPx(), h1)
                drawCircle(Color.White, 11.dp.toPx(), h2)
            }
            drawCircle(CurveGreen, 4.5.dp.toPx(), p0)
            drawCircle(CurveGreen, 4.5.dp.toPx(), p1)
        }
        // Guia do cabeçote em camada própria, sem cobrir os pontos da curva.
        Canvas(Modifier.fillMaxSize()) {
            val f = progress() ?: return@Canvas
            val inset = 24.dp.toPx()
            val p = Offset(inset + f * max(1f, size.width - 2 * inset), size.height - (ease.transform(f) - yMin) / (yMax - yMin) * size.height)
            drawLine(Color.White.copy(alpha = 0.4f), Offset(p.x, 0f), Offset(p.x, size.height),
                1.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(2.dp.toPx(), 4.dp.toPx())))
        }
    }
}

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

/**
 * OS PRESETS À DIREITA [A] (`_FamiliasDaCurva`, 132 dp): grade de miniaturas
 * 38 × 38 da família aberta + abas verticais das famílias (34 dp, `#151C24`).
 * A aba nasce na família do trecho.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun CurveFamilies(current: Ease, saved: List<CurvePreset>, onPick: (CurvePreset) -> Unit) {
    // As famílias fixas + a aba ★ dos presets de curva (nativos e salvos).
    val families = Families + CurveFamily(R.string.panel_presets, CupertinoGlyph.Star, saved)
    var tab by rememberSaveable { mutableIntStateOf(familyOf(current)) }
    val family = families[tab.coerceIn(0, families.lastIndex)]
    Row(Modifier.width(96.dp).fillMaxHeight()) {
        Box(Modifier.weight(1f).fillMaxHeight().verticalScroll(rememberScrollState()).padding(horizontal = 2.dp, vertical = 6.dp), contentAlignment = Alignment.Center) {
            Column(verticalArrangement = Arrangement.spacedBy(2.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                family.presets.forEach { p ->
                    val on = current.same(p.ease)
                    val presetName = stringResource(p.name ?: nameOf(p.ease))
                    Box(
                        Modifier
                            .size(48.dp)
                            .semantics { contentDescription = presetName }
                            .clip(RoundedCornerShape(8.dp))
                            .background(CurveRailFill)
                            .border(if (on) 1.8.dp else 0.dp, if (on) CurveGreen else Color.Transparent, RoundedCornerShape(8.dp))
                            .tocavel { onPick(p) },
                    ) {
                        PresetThumb(p.ease, on, Modifier.fillMaxSize())
                    }
                }
            }
        }
        Column(
            Modifier.width(48.dp).fillMaxHeight().background(CurveRailFill).verticalScroll(rememberScrollState()),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            families.forEachIndexed { i, f ->
                val familyName = stringResource(f.name)
                Box(Modifier.size(48.dp, 48.dp).semantics { contentDescription = familyName }.tocavel(shrink = 1f) { tab = i }, contentAlignment = Alignment.Center) {
                    PresetThumb(f.presets.firstOrNull()?.ease ?: Ease(Interp.BEZIER, .5f, 0f, .5f, 1f), i == tab, Modifier.size(40.dp))
                }
            }
        }
    }
}

/** A miniatura de um preset (`_PresetThumbPainter`): a curva e as duas pontas. */
@Composable
private fun PresetThumb(e: Ease, selected: Boolean, modifier: Modifier) {
    Canvas(modifier) {
        val pad = 6.dp.toPx()
        val w = size.width - pad * 2
        val hh = size.height - pad * 2
        val color = if (selected) CurveGreen else Color.White.copy(alpha = 0.7f)
        val p = Path()
        for (i in 0..40) {
            val t = i / 40f
            val v = e.transform(t).coerceIn(-0.3f, 1.3f)
            val o = Offset(pad + t * w, pad + hh - v * hh)
            if (i == 0) p.moveTo(o.x, o.y) else p.lineTo(o.x, o.y)
        }
        drawPath(p, color, style = Stroke(2.dp.toPx(), cap = StrokeCap.Round))
        val dot = if (selected) CurveGreen else Color.White
        drawCircle(dot, 2.5.dp.toPx(), Offset(pad, pad + hh))
        drawCircle(dot, 2.5.dp.toPx(), Offset(pad + w, pad))
    }
}
