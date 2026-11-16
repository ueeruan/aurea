package com.aurea.aurea.editor

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.PointerIcon
import androidx.compose.ui.input.pointer.pointerHoverIcon
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChanged
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.Ease
import com.aurea.aurea.editor.panels.Interp
import com.aurea.aurea.editor.panels.applyEase
import com.aurea.aurea.editor.panels.applyEaseToProperty
import com.aurea.aurea.editor.panels.easeOf
import com.aurea.aurea.editor.panels.engineEaseSamples
import com.aurea.aurea.editor.panels.propertySegmentStarts
import com.aurea.aurea.editor.panels.track
import com.aurea.aurea.editor.timeline.KeyRef
import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs

/**
 * O painel de CURVAS do Aurea PC (a janelinha [PcCurvePopupHost]): o gráfico
 * do trecho do keyframe escolhido, com as duas alças da bézier. Arrastar uma
 * alça mostra ao vivo e grava ao soltar (um desfazer; Shift: em todos os
 * trechos da propriedade); "Aplicar" grava a prévia de um preset. A biblioteca
 * embaixo mostra os presets do Aurea — um clique mostra no gráfico, dois
 * aplicam. Copiar/Colar curva no cabeçalho (e Ctrl+C / Ctrl+V depois de
 * clicar na janelinha). Os pontos da curva vêm do motor (`engineEaseSamples`),
 * então o desenho é exatamente o que vai animar.
 *
 * O último keyframe da trilha não abre trecho: o gráfico mostra o que CHEGA
 * nele (o do keyframe anterior), como o editor de gráficos do AE.
 */
@Composable
internal fun ColumnScope.PcCurvePanel(store: EditorStore, header: Modifier = Modifier, onClose: (() -> Unit)? = null) {
    // As alças não estão na KeyframeRow: a revisão do modelo relê a curva (desfazer, outro painel).
    val rev = store.curveRevision
    val sel = store.selectedKeyframe
    val seg = pcCurveSegment(store)
    val ref = seg?.let { it.first to KeyRef.of(it.second) }
    val base = remember(ref, rev, seg?.second?.interpolation) {
        seg?.let { (layer, key) -> runCatching { easeOf(store, layer, key) }.getOrNull() }
    }
    val current: Ease? = PcCurvePopup.edited?.takeIf { PcCurvePopup.editedFor == ref } ?: base
    SideEffect { PcCurvePopup.shown = current }
    val preview: (Ease?) -> Unit = { e -> PcCurvePopup.edited = e; PcCurvePopup.editedFor = ref }
    val apply: (Ease) -> Unit = { e ->
        seg?.let { (layer, key) -> pcApplyCurve(store, layer, key, e, wholeProperty = com.aurea.pc.PcKeys.shift) }
        PcCurvePopup.edited = null
    }

    // Cabeçalho: título, Copiar curva, Colar curva, fechar.
    Row(
        header.fillMaxWidth().height(36.dp).padding(start = 12.dp, end = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Text(stringResource(R.string.pc_curves), color = AureaColors.Text, fontSize = 13.sp, fontWeight = FontWeight.Medium,
            modifier = Modifier.weight(1f), maxLines = 1)
        CurveHeaderChip(CupertinoGlyph.DocOnDoc, stringResource(R.string.pc_curve_copy) , "Ctrl+C", enabled = current != null) {
            PcCurveClipboard.copy(store, current)
        }
        CurveHeaderChip(CupertinoGlyph.DocOnClipboard, stringResource(R.string.pc_curve_paste), "Ctrl+V",
            enabled = sel != null || store.keySelection?.isEmpty() == false) {
            PcCurveClipboard.paste(store, wholeProperty = com.aurea.pc.PcKeys.shift)
        }
        if (onClose != null) Box(Modifier.size(26.dp).clip(RoundedCornerShape(13.dp)).tocavel(shrink = 1f, onClick = onClose), contentAlignment = Alignment.Center) {
            CupertinoIcon(CupertinoGlyph.Xmark, 12.dp, AureaColors.Muted)
        }
    }
    Box(Modifier.fillMaxWidth().height(1.dp).background(AureaColors.Hairline))

    if (seg == null || current == null) {
        Box(Modifier.fillMaxWidth().weight(1f).padding(16.dp), contentAlignment = Alignment.Center) {
            Text(stringResource(if (sel == null) R.string.pc_curve_pick_key else R.string.pn_curve_need_two_keys),
                color = AureaColors.Muted, fontSize = 12.sp, textAlign = TextAlign.Center)
        }
        return
    }

    // Gráfico com as alças: arrastar mostra ao vivo e grava ao soltar.
    Box(Modifier.fillMaxWidth().weight(1.1f).padding(10.dp).clip(RoundedCornerShape(6.dp)).background(Color.Black.copy(alpha = 0.35f))) {
        CurveGraph(current, onCommit = apply)
    }
    // Aplicar a prévia (Shift: em todos os trechos da propriedade).
    val pending = PcCurvePopup.edited?.takeIf { PcCurvePopup.editedFor == ref } != null
    Row(Modifier.fillMaxWidth().padding(horizontal = 10.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(
            Modifier.weight(1f).height(36.dp).clip(RoundedCornerShape(8.dp))
                .background(if (pending) AureaColors.Brand else AureaColors.Brand.copy(alpha = 0.55f))
                .tocavel { apply(current) },
            contentAlignment = Alignment.Center,
        ) { Text(stringResource(R.string.pc_apply).uppercase(), color = AureaColors.OnAction, fontSize = 13.sp, fontWeight = FontWeight.Bold) }
    }
    Spacer(Modifier.height(10.dp))
    Box(Modifier.fillMaxWidth().height(1.dp).background(AureaColors.Hairline))
    // Biblioteca de presets: um clique mostra, dois aplicam.
    val previewNow = rememberUpdatedState(preview)
    val applyNow = rememberUpdatedState(apply)
    LazyVerticalGrid(
        columns = GridCells.Adaptive(72.dp),
        modifier = Modifier.fillMaxWidth().weight(1f).padding(8.dp),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        items(PRESETS) { (name, ease) ->
            val on = current.sameAs(ease)
            Column(
                Modifier.clip(RoundedCornerShape(6.dp)).background(if (on) AureaColors.Accent.copy(alpha = 0.18f) else Color.Transparent)
                    .pointerHoverIcon(PointerIcon.Hand)
                    // Chave só no preset: a seleção e as funções chegam pelo estado atualizado
                    // (antes a chave mudava a cada recomposição e o duplo clique se perdia).
                    .pointerInput(ease) {
                        detectTapGestures(
                            onTap = { previewNow.value(ease) },
                            onDoubleTap = { applyNow.value(ease) },
                        )
                    }
                    .padding(4.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Canvas(Modifier.fillMaxWidth().aspectRatio(1.25f)) { drawCurve(ease, AureaColors.Muted, 2f, inset = 6f) }
                Text(stringResource(name).uppercase(), color = if (on) AureaColors.Accent else AureaColors.Subtle, fontSize = 9.sp,
                    maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
        }
    }
}

/** Botão do cabeçalho (chip do Aurea com o símbolo e o nome). */
@Composable
private fun CurveHeaderChip(glyph: Char, label: String, keys: String, enabled: Boolean, onClick: () -> Unit) {
    val tint = if (enabled) AureaColors.Text else AureaColors.Subtle
    Row(
        Modifier.height(24.dp).clip(RoundedCornerShape(12.dp)).background(AureaColors.Chip)
            .then(if (enabled) Modifier.tocavel(shrink = 1f, onClick = onClick) else Modifier)
            .semantics { contentDescription = "$label ($keys)" }
            .padding(horizontal = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(glyph, 11.dp, tint)
        Spacer(Modifier.width(4.dp))
        Text(label, color = tint, fontSize = 11.sp, maxLines = 1)
    }
}

/** Os presets do Aurea (mesmas curvas do editor do celular). */
private val PRESETS: List<Pair<Int, Ease>> = listOf(
    R.string.panel_linear to Ease(Interp.LINEAR, 0f, 0f, 1f, 1f),
    R.string.pn_curve_std_smooth to Ease(Interp.BEZIER, 0.33f, 0f, 0.66f, 1f),
    R.string.pn_ease_in to Ease(Interp.BEZIER, 0.42f, 0f, 1f, 1f),
    R.string.pn_ease_out to Ease(Interp.BEZIER, 0f, 0f, 0.58f, 1f),
    R.string.pn_ease_in_out to Ease(Interp.BEZIER, 0.42f, 0f, 0.58f, 1f),
    R.string.pc_curve_sharp_in to Ease(Interp.BEZIER, 0.9f, 0f, 1f, 0.2f),
    R.string.pc_curve_sharp_out to Ease(Interp.BEZIER, 0f, 0.8f, 0.1f, 1f),
    R.string.pn_curve_std_overshoot to Ease(Interp.BEZIER, 0.34f, 1.56f, 0.64f, 1f),
    R.string.pn_curve_std_anticipate to Ease(Interp.BEZIER, 0.36f, 0f, 0.66f, -0.56f),
    R.string.pn_textpreset_elastic to Ease(Interp.ELASTIC, 0f, 0f, 1f, 1f),
    R.string.pn_textpreset_bounce to Ease(Interp.BOUNCE, .375f, .5f, 1f, -10f),
    R.string.pn_curve_steps4 to Ease(Interp.STEPS, 0f, 0f, 1f, 1f),
    R.string.pn_ease_hold to Ease(Interp.HOLD, 0f, 0f, 1f, 1f),
)

private fun Ease.sameAs(o: Ease) = interp == o.interp && abs(x1 - o.x1) < 1e-3f && abs(y1 - o.y1) < 1e-3f && abs(x2 - o.x2) < 1e-3f && abs(y2 - o.y2) < 1e-3f

/**
 * O gráfico: grade, a curva (pontos do motor) e, na bézier/reta, as duas alças
 * ligadas às pontas. O clique pega a alça mais perto; arrastar mostra ao vivo
 * e [onCommit] grava ao soltar (vira bézier, como no AE).
 *
 * O gesto vive num `pointerInput(Unit)` e lê a curva por estado atualizado:
 * antes a chave era a própria curva, que muda a cada movimento — o Compose
 * reiniciava o gesto no primeiro passo e a alça "não mexia".
 */
@Composable
private fun CurveGraph(ease: Ease, onCommit: (Ease) -> Unit) {
    var drag by remember { mutableStateOf<Ease?>(null) }
    var grabbed by remember { mutableIntStateOf(0) }
    val easeNow = rememberUpdatedState(ease)
    val commitNow = rememberUpdatedState(onCommit)
    val shown = drag ?: ease
    val handles = shown.hasHandles
    Canvas(
        Modifier.fillMaxSize()
            .then(if (handles) Modifier.pointerHoverIcon(PointerIcon.Hand) else Modifier)
            .pointerInput(Unit) {
                awaitEachGesture {
                    val down = awaitFirstDown(requireUnconsumed = false)
                    val start = easeNow.value
                    if (!start.hasHandles) return@awaitEachGesture
                    val box = PcCurveMath.graphBox(size.width.toFloat(), size.height.toFloat())
                    val which = PcCurveMath.nearestHandle(start, box, down.position.x, down.position.y)
                    down.consume()
                    grabbed = which
                    var cur = PcCurveMath.asBezier(start)
                    var moved = false
                    try {
                        while (true) {
                            val ev = awaitPointerEvent()
                            val ch = ev.changes.firstOrNull { it.id == down.id } ?: break
                            if (!ch.pressed) { ch.consume(); break }
                            if (ch.positionChanged()) {
                                val (x, y) = PcCurveMath.pointerToHandle(ch.position.x, ch.position.y, box)
                                cur = PcCurveMath.moveHandle(cur, which, x, y)
                                drag = cur
                                moved = true
                                ch.consume()
                            }
                        }
                        if (moved) commitNow.value(cur)
                    } finally {
                        grabbed = 0
                        drag = null
                    }
                }
            },
    ) {
        val box = PcCurveMath.graphBox(size.width, size.height)
        val l = box[0]; val t = box[1]; val w = box[2]; val h = box[3]
        // Grade 4×4 e as linhas do 0 e do 1.
        for (i in 0..4) {
            val x = l + w * i / 4f; val y = t + h * i / 4f
            drawLine(Color.White.copy(alpha = 0.06f), Offset(x, t - h * 0.25f), Offset(x, t + h * 1.25f), 1f)
            drawLine(Color.White.copy(alpha = if (i == 0 || i == 4) 0.25f else 0.06f), Offset(l, y), Offset(l + w, y), 1f)
        }
        drawCurve(shown, AureaColors.Accent, 3f, inset = 0f, box = box)
        if (handles) {
            val p0 = Offset(l, t + h); val p3 = Offset(l + w, t)
            val hp = PcCurveMath.handlePoints(shown, box)
            val p1 = Offset(hp[0], hp[1]); val p2 = Offset(hp[2], hp[3])
            drawLine(Color.White.copy(alpha = 0.7f), p0, p1, 1.5f)
            drawLine(Color.White.copy(alpha = 0.7f), p3, p2, 1.5f)
            listOf(p1 to 1, p2 to 2).forEach { (p, i) ->
                drawCircle(Color.White, if (grabbed == i) 9f else 7f, p)
                drawCircle(AureaColors.Accent, if (grabbed == i) 9f else 7f, p, style = Stroke(2f))
            }
        }
    }
}

/**
 * As contas das alças, puras (sem Compose nem motor): a área do gráfico, onde
 * cada alça fica, qual o clique pega e o valor novo sob o mouse.
 */
internal object PcCurveMath {
    /** Faixa do Y das alças (passar do ponto / antecipar). */
    const val Y_MIN = -1f
    const val Y_MAX = 2f

    /** [esquerda, topo, largura, altura] do quadrado 0..1, com margem para as alças fora dele. */
    fun graphBox(width: Float, height: Float): FloatArray {
        val padX = width * 0.08f
        val padY = height * 0.2f
        return floatArrayOf(padX, padY, width - 2 * padX, height - 2 * padY)
    }

    /** [x1, y1, x2, y2] das alças em pixels. */
    fun handlePoints(e: Ease, box: FloatArray): FloatArray {
        val l = box[0]; val t = box[1]; val w = box[2]; val h = box[3]
        return floatArrayOf(l + e.x1 * w, t + (1 - e.y1) * h, l + e.x2 * w, t + (1 - e.y2) * h)
    }

    /** 1 = alça de saída (x1, y1), 2 = de chegada (x2, y2): a mais perto do clique. */
    fun nearestHandle(e: Ease, box: FloatArray, px: Float, py: Float): Int {
        val p = handlePoints(e, box)
        val d1 = (px - p[0]) * (px - p[0]) + (py - p[1]) * (py - p[1])
        val d2 = (px - p[2]) * (px - p[2]) + (py - p[3]) * (py - p[3])
        return if (d1 <= d2) 1 else 2
    }

    /** O ponto do mouse em valores da curva: X preso em 0..1 (o tempo não volta), Y em [Y_MIN, Y_MAX]. */
    fun pointerToHandle(px: Float, py: Float, box: FloatArray): Pair<Float, Float> {
        val w = box[2].takeIf { it > 0f } ?: 1f
        val h = box[3].takeIf { it > 0f } ?: 1f
        val x = ((px - box[0]) / w).coerceIn(0f, 1f)
        val y = (1f - (py - box[1]) / h).coerceIn(Y_MIN, Y_MAX)
        return x to y
    }

    /** A curva como bézier (reta e as nomeadas viram as alças que já mostram). */
    fun asBezier(e: Ease): Ease = if (e.interp == Interp.BEZIER || e.interp == Interp.CUSTOM) e else Ease(Interp.BEZIER, e.x1, e.y1, e.x2, e.y2, e.power)

    /** A alça [which] em (x, y); a outra fica. */
    fun moveHandle(e: Ease, which: Int, x: Float, y: Float): Ease {
        val b = asBezier(e)
        return if (which == 1) b.copy(x1 = x, y1 = y) else b.copy(x2 = x, y2 = y)
    }
}

/** A curva pelos pontos do motor (o que anima de verdade). */
private fun DrawScope.drawCurve(ease: Ease, color: Color, stroke: Float, inset: Float, box: FloatArray? = null) {
    val samples = engineEaseSamples(ease, 96) ?: return
    val b = box ?: floatArrayOf(inset, size.height * 0.2f, size.width - 2 * inset, size.height * 0.6f)
    val l = b[0]; val t = b[1]; val w = b[2]; val h = b[3]
    val path = Path()
    samples.forEachIndexed { i, v ->
        val x = l + w * i / (samples.size - 1); val y = t + (1 - v) * h
        if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
    }
    drawPath(path, color, style = Stroke(stroke))
}

// =============================================================================
//  Trecho, aplicar, abrir e a área de transferência da curva
// =============================================================================

/**
 * O trecho que [key] mostra no editor: o que sai dele; no último keyframe da
 * trilha, o que chega nele. Nulo = trilha com menos de 2 keyframes (ou o
 * texto-fonte, que só segura).
 */
internal fun pcSegmentStart(keys: List<KeyframeRow>, key: KeyframeRow): KeyframeRow? {
    if (key.property == TrackProperty.TEXT_SOURCE) return null
    val ref = KeyRef.of(key)
    val track = keys.filter { ref.sameTrack(it) }.sortedBy { it.time }
    if (track.size < 2) return null
    val i = track.indexOfFirst { it.time == key.time }
    if (i < 0) return null
    return if (i < track.lastIndex) track[i] else track[i - 1]
}

/** O trecho do keyframe escolhido (a linha do motor, atual: a interpolação da seleção pode estar velha). */
internal fun pcCurveSegment(store: EditorStore): Pair<Long, KeyframeRow>? {
    val (layer, key) = store.selectedKeyframe ?: return null
    return pcSegmentStart(store.keyframes[layer].orEmpty(), key)?.let { layer to it }
}

/** Grava [e] no trecho de [start] (um desfazer); [wholeProperty] = todos os trechos da propriedade. */
internal fun pcApplyCurve(store: EditorStore, layer: Long, start: KeyframeRow, e: Ease, wholeProperty: Boolean) {
    if (wholeProperty) applyEaseToProperty(store, layer, start, e) else applyEase(store, layer, start, e)
}

/**
 * Abre o editor de curvas no keyframe escolhido; sem um, no da camada
 * principal no cabeçote (ou antes dele) de uma trilha animada. Falso = nada
 * para mostrar (a janelinha abre com o aviso de escolher um keyframe).
 */
internal fun pcOpenCurveNear(store: EditorStore): Boolean {
    if (store.selectedKeyframe != null) { PcCurvePopup.open = true; return true }
    val layer = store.primary
    val keys = layer?.let { store.keyframes[it] }.orEmpty().filter { it.timelineVisible && it.property != TrackProperty.TEXT_SOURCE }
    val tracks = keys.groupBy { KeyRef(it.property, it.effectIndex, it.paramIndex, 0) }.values
        .map { t -> t.sortedBy { it.time } }.filter { it.size >= 2 }
    if (layer == null || tracks.isEmpty()) { PcCurvePopup.open = true; return false }
    // A trilha em foco na timeline primeiro (a do painel aberto); senão a primeira animada.
    val focus = store.timelineFocus
    val track = tracks.firstOrNull { t -> focus?.any { f -> f.property == t[0].property && f.effectIndex == t[0].effectIndex && f.paramIndex == t[0].paramIndex } == true }
        ?: tracks.minBy { it[0].property }
    val local = store.detail?.takeIf { it.id == layer }?.localPlayhead ?: 0
    val key = track.lastOrNull { it.time <= local } ?: track.first()
    store.tapTimelineKey(layer, key, keys.filter { it.time == key.time && KeyRef.of(it).sameTrack(key) })
    PcCurvePopup.open = true
    return true
}

/**
 * A curva copiada: fica aqui e, em texto, na área de transferência do sistema
 * (passa de um projeto para outro e entre duas janelas do Aurea).
 */
internal object PcCurveClipboard {
    private const val TAG = "aurea-curve:1;"
    var ease by mutableStateOf<Ease?>(null)
        private set

    fun encode(e: Ease): String = TAG + listOf(e.interp, e.x1, e.y1, e.x2, e.y2, e.power).joinToString(";")

    fun decode(text: String?): Ease? {
        val s = text?.trim() ?: return null
        if (!s.startsWith(TAG)) return null
        val p = s.removePrefix(TAG).split(';')
        if (p.size < 5) return null
        val interp = p[0].toIntOrNull() ?: return null
        val f = p.subList(1, 5).map { it.toFloatOrNull()?.takeIf { v -> v.isFinite() } ?: return null }
        val power = p.getOrNull(5)?.toIntOrNull()?.coerceIn(1, 3) ?: 1
        return Ease(interp, f[0], f[1], f[2], f[3], power)
    }

    private fun systemText(): String? = runCatching {
        java.awt.Toolkit.getDefaultToolkit().systemClipboard.getData(java.awt.datatransfer.DataFlavor.stringFlavor) as? String
    }.getOrNull()

    /** A curva a colar: a do sistema (se for uma curva do Aurea), senão a copiada aqui. */
    fun current(): Ease? = decode(systemText()) ?: ease

    private fun text(id: Int) = com.aurea.pc.PcApp.context.getString(id)

    fun copy(store: EditorStore, e: Ease?): Boolean {
        if (e == null) {
            store.showToast(text(if (store.selectedKeyframe == null) R.string.pc_curve_pick_key else R.string.pn_curve_need_two_keys))
            return false
        }
        ease = e
        runCatching { java.awt.Toolkit.getDefaultToolkit().systemClipboard.setContents(java.awt.datatransfer.StringSelection(encode(e)), null) }
        store.showToast(text(R.string.curve_copied))
        return true
    }

    /**
     * Cola nos keyframes escolhidos (todos, se vários; nas outras camadas da
     * seleção também) ou no principal; [wholeProperty] (Shift) = em todos os
     * trechos das propriedades deles. Um passo de desfazer.
     */
    fun paste(store: EditorStore, wholeProperty: Boolean): Boolean {
        val e = current() ?: run { store.showToast(text(R.string.pc_curve_clipboard_empty)); return false }
        val groups = LinkedHashMap<Long, List<KeyframeRow>>()
        fun collect(layer: Long, refs: Collection<KeyRef>) {
            val keys = store.keyframes[layer].orEmpty()
            val rows = keys.filter { k -> refs.any { it.matches(k) } }
            val starts = if (wholeProperty) rows.flatMap { propertySegmentStarts(keys, keys.track(it)) }
            else rows.mapNotNull { pcSegmentStart(keys, it) }
            val distinct = starts.distinctBy { KeyRef.of(it) }
            if (distinct.isNotEmpty()) groups[layer] = (groups[layer].orEmpty() + distinct).distinctBy { KeyRef.of(it) }
        }
        val sel = store.keySelection
        if (sel != null && !sel.isEmpty()) {
            collect(sel.layer, sel.keys)
            sel.others.forEach { (layer, refs) -> collect(layer, refs) }
        } else {
            val (layer, key) = store.selectedKeyframe ?: run { store.showToast(text(R.string.pc_curve_pick_key)); return false }
            collect(layer, listOf(KeyRef.of(key)))
        }
        if (groups.isEmpty()) { store.showToast(text(R.string.pn_curve_need_two_keys)); return false }
        val many = groups.size > 1
        if (many) store.beginGesture("colar curva")
        groups.forEach { (layer, starts) ->
            store.setKeyframesEasing(layer, starts, e.interp, e.x1, e.y1, e.x2, e.y2, e.power, linkAxes = true)
        }
        if (many) store.endGesture()
        PcCurvePopup.edited = null
        store.showToast(text(R.string.pc_curve_pasted))
        return true
    }

    /** Ctrl+C: a curva que a janelinha mostra. */
    fun copyShown(store: EditorStore): Boolean = copy(store, PcCurvePopup.shown)
}
