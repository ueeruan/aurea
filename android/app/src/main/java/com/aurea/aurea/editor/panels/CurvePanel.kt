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
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
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
    val hasHandles get() = isBezier || interp == Interp.LINEAR

    fun transform(t: Float): Float = when (interp) {
        Interp.HOLD -> if (t < 1f) 0f else 1f
        Interp.LINEAR -> t
        Interp.EASE_IN -> t * t
        Interp.EASE_OUT -> 1f - (1f - t) * (1f - t)
        Interp.EASE_IN_OUT -> if (t < 0.5f) 2f * t * t else 1f - 2f * (1f - t) * (1f - t)
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

    /** INVERTER: o fim vira o começo (alças espelhadas). Nulo = igual nos dois sentidos. */
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

/** A mesma conta do motor (`cubic_bezier`): acha o t de x por bissecção e devolve y. */
internal fun cubicBezier(x1: Float, y1: Float, x2: Float, y2: Float, x: Float): Float {
    fun b(p: Float, q: Float, m: Float) = 3f * p * (1 - m) * (1 - m) * m + 3f * q * (1 - m) * m * m + m * m * m
    var lo = 0f
    var hi = 1f
    var mid = x
    repeat(24) {
        mid = (lo + hi) / 2f
        val e = b(x1, x2, mid)
        if (abs(e - x) < 1e-4f) return b(y1, y2, mid)
        if (e < x) lo = mid else hi = mid
    }
    return b(y1, y2, mid)
}

/** Um preset de uma família: nome [A] e o easing do motor. */
private class CurvePreset(val name: String, val ease: Ease, val entry: com.aurea.aurea.presets.PresetEntry? = null)

private class CurveFamily(val name: String, val glyph: Char, val presets: List<CurvePreset>)

private fun bez(x1: Float, y1: Float, x2: Float, y2: Float) = Ease(Interp.BEZIER, x1, y1, x2, y2)

/**
 * AS FAMÍLIAS DA CURVA [A] (`_familiasDaCurva`). As bézier da A.01 são as
 * mesmas alças (0,42 / 0,58); "Manter" é o `Hold` do motor. Quique, elástico,
 * degraus e osciladores não existem no motor: as famílias que só mostravam
 * "em breve" saíram na Fase 8I (§195, sem botão falso).
 */
private val Families = listOf(
    CurveFamily(
        "Bézier", CupertinoGlyph.Scribble,
        listOf(
            CurvePreset("Linear", Ease(Interp.LINEAR, 0f, 0f, 1f, 1f)),
            CurvePreset("Suave na entrada", bez(0.42f, 0f, 1f, 1f)),
            CurvePreset("Suave na saída", bez(0f, 0f, 0.58f, 1f)),
            CurvePreset("Suave nas duas pontas", bez(0.42f, 0f, 0.58f, 1f)),
        ),
    ),
    CurveFamily(
        "Manter", CupertinoGlyph.ChartBarAltFill,
        listOf(CurvePreset("Manter", Ease(Interp.HOLD, 0f, 0f, 1f, 1f))),
    ),
)

private fun nameOf(e: Ease): String {
    for (f in Families) for (p in f.presets) if (e.same(p.ease)) return p.name
    return when (e.interp) {
        Interp.EASE_IN -> "Suave na entrada"
        Interp.EASE_OUT -> "Suave na saída"
        Interp.EASE_IN_OUT -> "Suave nas duas pontas"
        else -> "Bézier (personalizada)"
    }
}

private fun familyOf(e: Ease): Int = if (e.interp == Interp.HOLD) 1 else 0

/**
 * AS ALÇAS QUE O MOTOR NÃO PUBLICA. `KeyframeRow` traz só a interpolação, não os
 * quatro números da bézier; a curva desenhada precisa deles. Guardamos aqui o
 * que ESTA sessão escreveu, por trilha e instante (é o valor que o motor tem,
 * porque fomos nós que mandamos). Keyframe bézier vindo de fora mostra o padrão
 * do motor (0,33 / 0,67). Lacuna do motor anotada no relatório.
 */
private object HandleMemory {
    val map = mutableStateMapOf<String, Ease>()
    fun key(layer: Long, k: KeyframeRow) = "$layer/${k.property}/${k.effectIndex}/${k.paramIndex}/${k.time}"
}

/** Área de transferência da curva (Copiar curva / Colar curva). */
private object CurveClipboard {
    var ease: Ease? = null
}

/** O easing de um keyframe como o motor o tem (com as alças lembradas). */
internal fun easeOf(layer: Long, k: KeyframeRow): Ease {
    if (k.interpolation == Interp.BEZIER || k.interpolation == Interp.CUSTOM) {
        return HandleMemory.map[HandleMemory.key(layer, k)] ?: Ease(k.interpolation, 0.33f, 0f, 0.67f, 1f)
    }
    return Ease(k.interpolation, 0f, 0f, 1f, 1f)
}

/**
 * Escreve o easing no trecho que SAI de [start], em todas as trilhas irmãs que
 * têm marca no mesmo instante (X e Y da posição andam juntos, como a A.01).
 */
internal fun applyEase(store: EditorStore, layer: Long, start: KeyframeRow, e: Ease) {
    val keys = store.keyframes[layer] ?: return
    keys.filter { it.time == start.time && it.sameGroup(start) }.forEach { k ->
        store.setKeyframeEasing(layer, k, e.interp, e.x1, e.y1, e.x2, e.y2)
        if (e.isBezier) HandleMemory.map[HandleMemory.key(layer, k)] = e else HandleMemory.map.remove(HandleMemory.key(layer, k))
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
@Composable
internal fun CurvePanel(env: PanelEnv) {
    val store = env.store
    val back = {
        val r = env.returnTo()
        if (r != null && r != EditorPanel.Curve) env.onOpenPanel(r) else env.onClose()
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
    val ease = easeOf(layer, start)
    var overshoot by rememberSaveable { mutableStateOf(false) }
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

    Row(Modifier.fillMaxSize()) {
        // Trilho esquerdo: voltar · inverter · menu.
        Column(Modifier.width(44.dp).fillMaxHeight(), horizontalAlignment = Alignment.CenterHorizontally) {
            Spacer(Modifier.height(6.dp))
            Box(Modifier.size(44.dp).tocavel { back() }, contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.ChevronBack, 24.dp, Color.White)
            }
            Spacer(Modifier.weight(1f))
            Box(
                Modifier.size(44.dp).tocavel {
                    val inv = ease.inverted()
                    if (inv == null) store.showToast("Esta curva é igual nos dois sentidos")
                    else {
                        store.beginGesture("inverter curva")
                        applyEase(store, layer, start, inv)
                        store.endGesture()
                    }
                },
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.ArrowRightArrowLeft, 20.dp, Color.White)
            }
            Spacer(Modifier.height(4.dp))
            RailMoreButton(active = overshoot) { menu = true }
            Spacer(Modifier.height(8.dp))
        }
        Column(Modifier.weight(1f).fillMaxHeight()) {
            Box(Modifier.weight(1f).fillMaxWidth().alpha(if (inside) 1f else 0.45f)) {
                CurveGraph(
                    ease = ease,
                    overshoot = overshoot,
                    progress = progress,
                    onBegin = { store.beginGesture("curva") },
                    onChange = { applyEase(store, layer, start, it) },
                    onEnd = { store.endGesture() },
                )
            }
            Row(Modifier.fillMaxWidth().height(32.dp), horizontalArrangement = Arrangement.Center, verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(32.dp).tocavel { jump(-1) }, contentAlignment = Alignment.Center) {
                    CupertinoIcon(CupertinoGlyph.ChevronLeft, 16.dp, Color.White)
                }
                Spacer(Modifier.width(4.dp))
                val track = (store.keyframes[layer] ?: emptyList()).track(start)
                val n = track.indexOfFirst { it.time == start.time }
                Text(
                    if (inside) nameOf(ease) else "Trecho ${n + 1} → ${n + 2}",
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f, fill = false),
                    style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = Color.White, textAlign = TextAlign.Center)),
                )
                Spacer(Modifier.width(4.dp))
                Box(Modifier.size(32.dp).tocavel { jump(1) }, contentAlignment = Alignment.Center) {
                    CupertinoIcon(CupertinoGlyph.ChevronRight, 16.dp, Color.White)
                }
            }
        }
        // Presets de curva (nativos + os salvos): a aba ★ das famílias.
        val saved = remember(store.presets.user) { store.presets.entries(com.aurea.aurea.presets.PresetKind.Curve).mapNotNull { entry ->
            store.curveOfPreset(entry)?.let { v -> CurvePreset(entry.name, Ease(v[0].toInt(), v[1], v[2], v[3], v[4]), entry) }
        } }
        CurveFamilies(
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
            initial = nameOf(ease),
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
    val yMin = if (overshoot || h[1] < 0f || h[3] < 0f) -0.5f else -0.12f
    val yMax = if (overshoot || h[1] > 1f || h[3] > 1f) 1.5f else 1.12f
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
                    if (!e0.hasHandles) return@awaitEachGesture
                    val (lo, hi) = range
                    fun plot(x: Float, y: Float) = Offset(x * size.width, size.height - (y - lo) / (hi - lo) * size.height)
                    val hh = e0.handles()
                    val p1 = plot(hh[0], hh[1])
                    val p2 = plot(hh[2], hh[3])
                    val first = (down.position - p1).getDistanceSquared() <= (down.position - p2).getDistanceSquared()
                    var began = false
                    var e = Ease(Interp.BEZIER, hh[0], hh[1], hh[2], hh[3])
                    while (true) {
                        val ev = awaitPointerEvent()
                        val ch = ev.changes.firstOrNull { it.id == down.id } ?: break
                        if (!ch.pressed) break
                        ch.consume()
                        val x = (ch.position.x / size.width).coerceIn(0f, 1f)
                        var y = lo + (size.height - ch.position.y) / size.height * (hi - lo)
                        if (!over) y = y.coerceIn(0f, 1f)
                        if (!x.isFinite() || !y.isFinite()) continue
                        if (!began) {
                            began = true
                            begin()
                        }
                        e = if (first) e.copy(x1 = x, y1 = y) else e.copy(x2 = x, y2 = y)
                        change(e)
                    }
                    if (began) end()
                }
            },
    ) {
        // A curva, a grade e as alças só mudam com o easing: redesenham raramente.
        Canvas(Modifier.fillMaxSize()) {
            fun pt(x: Float, y: Float) = Offset(x * size.width, size.height - (y - yMin) / (yMax - yMin) * size.height)
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
            drawPath(area, AureaColors.Accent.copy(alpha = 0.10f))
            drawPath(curve, AureaColors.Accent, style = Stroke(3.5.dp.toPx(), cap = StrokeCap.Round))
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
            drawCircle(AureaColors.Accent, 4.5.dp.toPx(), p0)
            drawCircle(AureaColors.Accent, 4.5.dp.toPx(), p1)
        }
        // O ponto que corre com o cabeçote: camada própria, lida NO DESENHO — a
        // reprodução só repinta estes três traços, sem recompor nem refazer a curva.
        Canvas(Modifier.fillMaxSize()) {
            val f = progress() ?: return@Canvas
            val p = Offset(f * size.width, size.height - (ease.transform(f) - yMin) / (yMax - yMin) * size.height)
            drawLine(AureaColors.Danger.copy(alpha = 0.35f), Offset(p.x, 0f), Offset(p.x, size.height), 1.dp.toPx())
            drawCircle(AureaColors.Danger, 8.dp.toPx(), p)
            drawCircle(Color.White, 8.dp.toPx(), p, style = Stroke(2.dp.toPx()))
        }
    }
}

private fun DrawScope.drawGrid(y0: Float, y1: Float) {
    val grid = AureaColors.CurveGrid
    val w = 1.dp.toPx()
    val dashLen = 2.5.dp.toPx()
    val gap = 7.dp.toPx()
    for (i in 1 until 8) {
        val x = size.width * i / 8f
        var y = 0f
        while (y < size.height) { drawLine(grid, Offset(x, y), Offset(x, y + dashLen), w); y += gap }
        val gy = size.height * i / 8f
        var gx = 0f
        while (gx < size.width) { drawLine(grid, Offset(gx, gy), Offset(gx + dashLen, gy), w); gx += gap }
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
    val families = Families + CurveFamily(stringResource(R.string.panel_presets), CupertinoGlyph.Star, saved)
    var tab by rememberSaveable { mutableIntStateOf(familyOf(current)) }
    val family = families[tab.coerceIn(0, families.lastIndex)]
    Row(Modifier.width(132.dp).fillMaxHeight()) {
        Box(Modifier.weight(1f).fillMaxHeight().verticalScroll(rememberScrollState()).padding(horizontal = 2.dp, vertical = 6.dp), contentAlignment = Alignment.Center) {
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.CenterHorizontally), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                family.presets.forEach { p ->
                    val on = current.same(p.ease)
                    Box(
                        Modifier
                            .size(38.dp)
                            .clip(RoundedCornerShape(8.dp))
                            .background(AureaColors.RailModeFill)
                            .border(if (on) 1.8.dp else 1.dp, if (on) AureaColors.Accent else AureaColors.CurvePresetBorder, RoundedCornerShape(8.dp))
                            .tocavel { onPick(p) },
                    ) {
                        PresetThumb(p.ease, on, Modifier.fillMaxSize())
                    }
                }
            }
        }
        Column(
            Modifier.width(34.dp).fillMaxHeight().background(AureaColors.Surface),
            verticalArrangement = Arrangement.Center,
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            families.forEachIndexed { i, f ->
                Box(Modifier.size(34.dp, 34.dp).semantics { contentDescription = f.name }.tocavel(shrink = 1f) { tab = i }, contentAlignment = Alignment.Center) {
                    CupertinoIcon(f.glyph, 17.dp, if (i == tab) AureaColors.Accent else AureaColors.Muted)
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
        val color = if (selected) AureaColors.Accent else Color.White.copy(alpha = 0.7f)
        val p = Path()
        for (i in 0..40) {
            val t = i / 40f
            val v = e.transform(t).coerceIn(-0.3f, 1.3f)
            val o = Offset(pad + t * w, pad + hh - v * hh)
            if (i == 0) p.moveTo(o.x, o.y) else p.lineTo(o.x, o.y)
        }
        drawPath(p, color, style = Stroke(2.dp.toPx(), cap = StrokeCap.Round))
        val dot = if (selected) AureaColors.Accent else Color.White
        drawCircle(dot, 2.5.dp.toPx(), Offset(pad, pad + hh))
        drawCircle(dot, 2.5.dp.toPx(), Offset(pad + w, pad))
    }
}
