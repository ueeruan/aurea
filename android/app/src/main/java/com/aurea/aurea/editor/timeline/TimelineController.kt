package com.aurea.aurea.editor.timeline

import androidx.compose.animation.core.AnimationState
import androidx.compose.animation.core.animateDecay
import androidx.compose.animation.splineBasedDecay
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.calculateCentroid
import androidx.compose.foundation.gestures.calculateZoom
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.hapticfeedback.HapticFeedback
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerEvent
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.input.pointer.PointerInputScope
import androidx.compose.ui.input.pointer.util.VelocityTracker
import androidx.compose.ui.input.pointer.util.addPointerInputChange
import androidx.compose.ui.unit.Density
import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.state.EditorStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import java.util.Arrays
import kotlin.math.abs
import kotlin.math.floor
import kotlin.math.max

/**
 * Dono ÚNICO dos gestos da timeline (spec §11.3): um `pointerInput` na raiz
 * decide pelo hit-test calculado (a mesma geometria do pintor) o que o pouso
 * atingiu — pílula > losango > alça > corpo > vazio — e cada gesto fala com o
 * motor só pelo [EditorStore].
 *
 * Contratos que a spec pediu para acertar no port:
 * - a vista É o playhead do motor; o gesto que anda no tempo abre um scrub
 *   (`scrubStart/scrubTo/scrubEnd`, sempre em par) e a vista fracionária só
 *   existe enquanto ele dura (§8.3-1, bug 10.13);
 * - todo arrasto que muda o projeto é UM passo de desfazer, aberto na 1ª
 *   mutação real e fechado ao soltar ou cancelar;
 * - um só contrato de losango: toque = escolhe e abre a curva; arrastar = move
 *   (bug 10.1).
 */
internal class TimelineController(
    val store: EditorStore,
    val state: TimelineState,
    private val scope: CoroutineScope,
) {
    var metrics = TimelineMetrics(1f)
    var onEmptyTap: () -> Unit = {}
    var onKeyframeTap: (Long, KeyframeRow) -> Unit = { _, _ -> }
    var onTrackTap: (Long, Int, Int) -> Unit = { _, _, _ -> }
    val expandedLayer = mutableStateOf<Long?>(null)
    var haptics: HapticFeedback? = null

    /** Linhas derivadas do que o store leu; refeitas só as que mudaram (fase 8D). */
    private val rowCache = RowCache()
    private var expandedBase: List<RowModel>? = null
    private var expandedRevision = -1
    private var expandedId: Long? = null
    private var expandedResult: List<RowModel> = emptyList()
    val rows = derivedStateOf {
        val base = rowCache.build(store.layers, store.keyframes)
        val id = expandedLayer.value?.takeUnless { state.compact }
        val revision = store.curveRevision
        if (id == null) base else if (expandedBase === base && expandedId == id && expandedRevision == revision) expandedResult else {
            expandedBase = base; expandedId = id; expandedRevision = revision
            expandedRows(base, id, store.keyframes, store.timelineEffects(id)).also { expandedResult = it }
        }
    }
    // Seleção como LongArray ordenado: `Set<Long>.contains` encaixotaria o id a cada linha pintada.
    private val selectedIds = derivedStateOf { store.selection.toLongArray().also { it.sort() } }
    private val primaryId = derivedStateOf { store.primary ?: NO_ID }

    fun isSelected(id: Long) = Arrays.binarySearch(selectedIds.value, id) >= 0
    fun selectionSize() = selectedIds.value.size
    private fun multi() = selectionSize() >= 2

    // =========================================================================
    // Linhas visíveis (no compacto, só a camada escolhida — A.01 `singleLayerId`)
    // =========================================================================
    fun rowCount(list: List<RowModel>): Int =
        if (!state.compact) list.size else if (compactRow(list) != null) 1 else 0

    fun rowAt(list: List<RowModel>, i: Int): RowModel? =
        if (!state.compact) list.getOrNull(i) else compactRow(list)

    fun rowHeight(row: RowModel): Float = timelineRowHeight(row, metrics.row, metrics.density)
    fun rowTop(index: Int): Float = if (state.compact) 0f else timelineRowTop(rows.value, index, metrics.row, metrics.density)
    fun rowIndexAt(y: Float): Int = if (state.compact) {
        if (y < 0f) -1 else if (y < metrics.row) 0 else 1
    } else timelineRowIndex(rows.value, y, metrics.row, metrics.density)

    private fun compactRow(list: List<RowModel>): RowModel? {
        val p = primaryId.value
        for (i in list.indices) if (list[i].id == p) return list[i]
        return null
    }

    private fun rowById(id: Long): RowModel? {
        val list = rows.value
        for (i in list.indices) if (list[i].id == id) return list[i]
        return null
    }

    private fun maxScroll(n: Int): Float =
        max(0f, rowTop(n) + metrics.bottomPad - (state.height - metrics.rowsTop))

    fun clampedScroll(n: Int): Float = if (state.compact) 0f else state.scrollY.coerceIn(0f, maxScroll(n))

    private fun clampScroll(v: Float): Float = v.coerceIn(0f, maxScroll(rowCount(rows.value)))

    // =========================================================================
    // Vista (frame sob o cabeçote fixo)
    // =========================================================================
    private val fps: Float get() = TimeAxis.safeFps(store.project.fps)
    fun pxPerFrame(): Float = TimeAxis.pxPerFrame(state.pps, metrics.density, fps)
    fun view(): Double {
        val held = state.heldView
        return if (held.isNaN()) store.playhead.toDouble() else held
    }

    private fun centerX() = state.width / 2f
    private fun frameAt(x: Float) = TimeAxis.frameAt(x, view(), pxPerFrame(), centerX())
    private fun xOf(frame: Int) = TimeAxis.xOf(frame.toDouble(), view(), pxPerFrame(), centerX())
    private fun playheadFrame() = view().toFrame()

    private var scrubOpen = false

    /** Leva a vista a `v` pelo scrub do motor (o 1º passo abre o scrub e pausa). */
    private fun holdView(v: Double) {
        val c = TimeAxis.clampView(v, store.project.durationFrames)
        state.heldView = c
        val f = c.toFrame()
        if (!scrubOpen) {
            scrubOpen = true
            if (store.playing) store.pause()
            store.scrubStart(f)
        } else {
            store.scrubTo(f)
        }
    }

    /** Solta a vista: fecha o scrub e ela volta a ser o playhead do motor. */
    private fun releaseView() {
        if (scrubOpen) {
            scrubOpen = false
            store.scrubEnd()
        }
        state.heldView = Double.NaN
    }

    // =========================================================================
    // Desfazer: 1 gesto = 1 passo, aberto na 1ª mutação real
    // =========================================================================
    private var undoOpen = false

    private fun openUndo(label: String) {
        if (!undoOpen) {
            undoOpen = true
            store.beginGesture(label)
        }
    }

    private fun closeUndo() {
        if (undoOpen) {
            undoOpen = false
            store.endGesture()
        }
    }

    // =========================================================================
    // Háptico (spec §5.10)
    // =========================================================================
    private fun tick() = haptics?.performHapticFeedback(HapticFeedbackType.SegmentTick)
    private fun light() = haptics?.performHapticFeedback(HapticFeedbackType.VirtualKey)
    private fun heavy() = haptics?.performHapticFeedback(HapticFeedbackType.LongPress)

    private fun pauseIfPlaying() {
        if (store.playing) store.pause()
    }

    private fun setGuide(frame: Int) {
        if (frame == state.guideFrame) return
        if (frame != Snap.NONE) tick()
        state.guideFrame = frame
    }

    // =========================================================================
    // Hit-test
    // =========================================================================
    private class Hit {
        var kind = HitKind.NONE
        var row: RowModel? = null
        var rowIndex = -1
        var keyIndex = -1
    }

    private val hitOut = IntArray(1)

    private fun hitAt(p: Offset): Hit {
        val hit = Hit()
        val m = metrics
        if (p.y < m.rowsTop) {
            hit.kind = HitKind.RULER
            return hit
        }
        val list = rows.value
        val n = rowCount(list)
        val scroll = clampedScroll(n)
        val i = rowIndexAt(p.y - m.rowsTop + scroll)
        if (i !in 0 until n) return hit
        val r = rowAt(list, i) ?: return hit
        val top = m.rowsTop + rowTop(i) - scroll
        val x0 = xOf(r.start)
        val x1 = max(xOf(r.end), x0 + m.barMinWidth)
        val handles = r.track == null && !state.compact && selectionSize() == 1 && isSelected(r.id) && !r.locked
        hit.kind = RowHit.hit(
            m, p.x, if (r.track == null) p.y - top else m.diamondCyNormal, state.width.toFloat(), x0, x1, handles, state.compact,
            keysEnabled = !multi(), instants = r.instants,
            view = view(), pxPerFrame = pxPerFrame(), centerX = centerX(), out = hitOut,
        )
        if (r.track != null && hit.kind != HitKind.KEYFRAME) hit.kind = HitKind.BODY
        hit.row = r
        hit.rowIndex = i
        hit.keyIndex = hitOut[0]
        return hit
    }

    // =========================================================================
    // Laço de gestos
    // =========================================================================
    suspend fun PointerInputScope.handleGestures() {
        awaitEachGesture {
            val down = awaitFirstDown(requireUnconsumed = false)
            down.consume()
            // Um dedo que pousa para a inércia (horizontal e vertical) — e esse toque só para, não escolhe.
            val stoppedFling = stopFlings()
            try {
                gesture(down, stoppedFling)
            } finally {
                endInteraction()
            }
        }
    }

    private enum class Phase { TAP, DRAG, PINCH, LONG_PRESS, CANCEL }

    private suspend fun AwaitPointerEventScope.gesture(down: PointerInputChange, stoppedFling: Boolean) {
        val hit = hitAt(down.position)
        val tracker = VelocityTracker()
        tracker.addPointerInputChange(down)
        val slop = viewConfiguration.touchSlop
        var phase = Phase.CANCEL
        var slopAt = down.position
        val finished = withTimeoutOrNull(LONG_PRESS_MS) {
            while (true) {
                val ev = awaitPointerEvent()
                if (pressedCount(ev) >= 2) {
                    phase = Phase.PINCH
                    break
                }
                val ch = changeOf(ev, down.id)
                if (ch == null) {
                    phase = Phase.CANCEL
                    break
                }
                if (!ch.pressed) {
                    ch.consume()
                    phase = Phase.TAP
                    break
                }
                tracker.addPointerInputChange(ch)
                if ((ch.position - down.position).getDistance() > slop) {
                    ch.consume()
                    slopAt = ch.position
                    phase = Phase.DRAG
                    break
                }
            }
        }
        if (finished == null) phase = Phase.LONG_PRESS
        when (phase) {
            Phase.TAP -> if (!stoppedFling) onTap(hit, down.position)
            Phase.DRAG -> drag(hit, down, slopAt, tracker)
            Phase.PINCH -> pinch()
            Phase.LONG_PRESS -> longPress(hit, down, tracker)
            Phase.CANCEL -> {}
        }
    }

    /** Fecha tudo o que o gesto abriu (também no cancelamento pelo sistema). */
    private fun endInteraction() {
        stopAutoScroll()
        closeUndo()
        state.guideFrame = Snap.NONE
        state.reorderSource = -1
        state.reorderTarget = -1
        state.dragKeyLayer = 0L
        state.dragKeyFrame = Snap.NONE
        if (flingJob == null) releaseView()
    }

    fun dispose() {
        stopFlings()
        endInteraction()
    }

    /** Mais um quadro de desenho (fora da fase de desenho: escrever estado lido nela ali mesmo é frágil). */
    private var redrawPosted = false
    fun requestRedraw() {
        if (redrawPosted) return
        redrawPosted = true
        scope.launch {
            redrawPosted = false
            state.redrawTick++
        }
    }

    // --- Toque ------------------------------------------------------------------
    /**
     * Toque na régua: só leva o cabeçote ao instante do dedo. NÃO cria marca —
     * a régua divide a faixa com o relógio e o topo do cabeçote, e cada toque
     * para buscar um ponto deixava uma marca "do nada". Tocar EM CIMA de uma
     * marca (até 12dp) abre o editor dela; marca nova nasce pela âncora do
     * preview ou pelo menu.
     */
    private fun rulerTap(x: Float) {
        val f = frameAt(x).toFrame()
        tick()
        val tolerance = (12f * metrics.density / pxPerFrame().coerceAtLeast(0.0001f)).toInt()
        val marker = store.markerNear(f, tolerance)
        store.seek(marker ?: f)
        if (marker != null) store.openMarkerEditor(marker)
    }

    private fun onTap(hit: Hit, p: Offset) {
        val r = hit.row
        when (hit.kind) {
            HitKind.RULER -> rulerTap(p.x)
            HitKind.NONE -> {
                store.clearSelectedKeyframe()
                onEmptyTap()
            }
            HitKind.HEADER_EYE -> if (r != null) {
                light()
                store.setLayerVisible(r.id, !r.visible)
            }
            // A.01: ‹ = selectNeighbor(+1), › = selectNeighbor(−1).
            HitKind.ARROW_PREV -> {
                tick()
                store.selectNeighbor(1)
            }
            HitKind.ARROW_NEXT -> {
                tick()
                store.selectNeighbor(-1)
            }
            HitKind.KEYFRAME -> if (r != null) keyframeTap(r, hit.keyIndex)
            HitKind.HEADER -> if (r != null) {
                if (!state.compact) {
                    tick()
                    expandedLayer.value = if (expandedLayer.value == r.id) null else r.id
                } else selectTap(r)
            }
            HitKind.BODY, HitKind.TRIM_START, HitKind.TRIM_END -> if (r != null) {
                if (r.track != null) onTrackTap(r.id, r.track.property, r.track.effect) else selectTap(r)
            }
        }
    }

    private fun selectTap(r: RowModel) {
        tick()
        // A.01: no compacto, tocar na barra sai do painel (a casca decide pelo toque no vazio).
        if (state.compact) {
            onEmptyTap()
            return
        }
        if (multi()) {
            store.select(r.id, additive = true)
        } else {
            pauseIfPlaying()
            store.select(r.id)
        }
    }

    /** Losango: escolhe a camada, leva o cabeçote ao instante e avisa a casca (curva). */
    private fun keyframeTap(r: RowModel, index: Int) {
        if (index !in r.instants.indices) return
        tick()
        pauseIfPlaying()
        if (!(selectionSize() == 1 && isSelected(r.id))) store.select(r.id)
        val key = r.keysAt[index].first()
        store.seek(r.instants[index])
        store.selectKeyframe(r.id, key)
        onKeyframeTap(r.id, key)
    }

    // --- Toque longo ---------------------------------------------------------------
    private suspend fun AwaitPointerEventScope.longPress(hit: Hit, down: PointerInputChange, tracker: VelocityTracker) {
        val r = hit.row
        val kind = hit.kind
        if ((r?.track != null && kind != HitKind.KEYFRAME) || r == null || kind == HitKind.NONE || kind == HitKind.RULER || kind == HitKind.HEADER_EYE) {
            // Sem ação de toque longo aqui: o gesto segue como arrasto (scrub/rolagem).
            val slop = viewConfiguration.touchSlop
            while (true) {
                val ev = awaitPointerEvent()
                if (pressedCount(ev) >= 2) return pinch()
                val ch = changeOf(ev, down.id) ?: return
                if (!ch.pressed) return
                tracker.addPointerInputChange(ch)
                if ((ch.position - down.position).getDistance() > slop) {
                    ch.consume()
                    return drag(hit, down, ch.position, tracker)
                }
            }
        }
        heavy()
        while (true) {
            val ev = awaitPointerEvent()
            val ch = changeOf(ev, down.id)
            if (ch == null || !ch.pressed) {
                ch?.consume()
                longPressStill(hit, r)
                return
            }
            consumeAll(ev)
            val d = ch.position - down.position
            if (d.getDistance() < metrics.axisSlop) continue
            // O eixo se decide UMA vez, no 1º movimento (A.01): tremor não vira deslocamento.
            val timeAxis = abs(d.x) > abs(d.y)
            when (kind) {
                HitKind.KEYFRAME -> if (timeAxis) keyframeDrag(r, hit.keyIndex, down) else consumeUntilUp()
                HitKind.HEADER -> if (!timeAxis && !state.compact) reorderDrag(r, hit.rowIndex, down) else consumeUntilUp()
                else -> if (timeAxis || state.compact) longPressMove(r, down) else reorderDrag(r, hit.rowIndex, down)
            }
            return
        }
    }

    private fun longPressStill(hit: Hit, r: RowModel) {
        when (hit.kind) {
            // A.01: segurar o quadradinho trava/destrava.
            HitKind.HEADER -> {
                tick()
                store.setLayerLocked(r.id, !r.locked)
            }
            HitKind.KEYFRAME -> keyframeTap(r, hit.keyIndex)
            else -> {
                if (state.compact) return
                tick()
                when {
                    selectionSize() == 0 -> store.select(r.id)
                    selectionSize() == 1 && isSelected(r.id) -> {}   // segurar a única escolhida não a solta
                    else -> store.select(r.id, additive = true)      // soma/tira do lote
                }
            }
        }
    }

    /**
     * Toque longo + arrasto no tempo. Bug 10.4: movia um clipe NÃO escolhido sem
     * escolhê-lo (sem contorno durante o arrasto). Agora ele é escolhido antes
     * (somado ao lote, se há lote) e o arrasto move o que está escolhido.
     */
    private suspend fun AwaitPointerEventScope.longPressMove(r: RowModel, down: PointerInputChange) {
        if (!isSelected(r.id)) {
            if (multi()) store.select(r.id, additive = true) else store.select(r.id)
        }
        moveDrag(rowById(r.id) ?: r, down)
    }

    // --- Arrasto ---------------------------------------------------------------------
    private suspend fun AwaitPointerEventScope.drag(hit: Hit, down: PointerInputChange, slopAt: Offset, tracker: VelocityTracker) {
        val d = slopAt - down.position
        val horizontal = abs(d.x) >= abs(d.y)
        val r = hit.row
        when {
            r != null && horizontal && hit.kind == HitKind.KEYFRAME -> keyframeDrag(r, hit.keyIndex, down)
            r != null && horizontal && hit.kind == HitKind.TRIM_START -> trimDrag(r, true, down)
            r != null && horizontal && hit.kind == HitKind.TRIM_END -> trimDrag(r, false, down)
            // Clipe escolhido anda no tempo; o não escolhido não se arrasta (o arrasto é scrub).
            // No compacto a barra ocupa a timeline: arrastar nela é scrub, mover é por toque longo (A.01).
            r != null && r.track == null && horizontal && !state.compact && isSelected(r.id) && hit.kind == HitKind.BODY -> moveDrag(r, down)
            horizontal -> scrub(down.id, slopAt, tracker)
            !state.compact -> scroll(down.id, slopAt, tracker)
            else -> consumeUntilUp()
        }
    }

    /** Scrub: o tempo anda sob o cabeçote fixo; arrastar para a DIREITA traz o passado. */
    private suspend fun AwaitPointerEventScope.scrub(id: PointerId, start: Offset, tracker: VelocityTracker) {
        val view0 = view()
        val ppf = pxPerFrame()
        holdView(view0)
        while (true) {
            val ev = awaitPointerEvent()
            if (pressedCount(ev) >= 2) return pinch()
            val ch = changeOf(ev, id)
            // Soltou: o tempo fica onde o dedo parou (sem inércia — o dono
            // pediu; o quadro escolhido é o que estava sob o cabeçote).
            if (ch == null || !ch.pressed) return
            ch.consume()
            holdView(view0 - (ch.position.x - start.x) / ppf)
        }
    }

    private suspend fun AwaitPointerEventScope.scroll(id: PointerId, start: Offset, tracker: VelocityTracker) {
        val s0 = clampedScroll(rowCount(rows.value))
        while (true) {
            val ev = awaitPointerEvent()
            if (pressedCount(ev) >= 2) return pinch()
            val ch = changeOf(ev, id)
            if (ch == null || !ch.pressed) {
                if (ch != null) startScrollFling(tracker.calculateVelocity().y)
                return
            }
            tracker.addPointerInputChange(ch)
            ch.consume()
            state.scrollY = clampScroll(s0 - (ch.position.y - start.y))
        }
    }

    /**
     * Pinça ancorada no instante sob os dedos (e pan de dois dedos junto). A
     * vista anda pelo scrub do motor (coalescido), não por seek (bug 10.8).
     * Tocando, o zoom é em volta do cabeçote e nada é pedido ao motor.
     */
    private suspend fun AwaitPointerEventScope.pinch() {
        tick()
        val pps0 = state.pps
        val playing = store.playing
        var zoom = 1f
        var anchor = Double.NaN
        while (true) {
            val ev = awaitPointerEvent()
            if (pressedCount(ev) < 2) break
            zoom *= ev.calculateZoom()
            state.pps = Zoom.clamp(pps0 * zoom)
            zoom = state.pps / pps0
            if (!playing) {
                val focus = ev.calculateCentroid(useCurrent = true)
                if (anchor.isNaN()) anchor = frameAt(focus.x)
                holdView(Zoom.anchoredView(anchor, focus.x, centerX(), pxPerFrame()))
            }
            consumeAll(ev)
        }
        // O dedo que sobrou não vira scrub até todos levantarem.
        consumeUntilUp()
    }

    // --- Mover clipe -------------------------------------------------------------------
    private suspend fun AwaitPointerEventScope.moveDrag(r: RowModel, down: PointerInputChange) {
        val ids: LongArray = if (isSelected(r.id)) selectedIds.value.copyOf() else longArrayOf(r.id)
        val group = ArrayList<Long>(ids.size)
        var minStart = Int.MAX_VALUE
        var locked = false
        for (id in ids) {
            val row = rowById(id) ?: continue
            group.add(id)
            minStart = minOf(minStart, row.start)
            locked = locked || row.locked
        }
        if (locked) {
            light()
            store.showToast(if (group.size > 1) "Há camada bloqueada na seleção: desbloqueie para mover" else "Camada bloqueada: desbloqueie para editar")
            return consumeUntilUp()
        }
        if (group.isEmpty()) return consumeUntilUp()
        pauseIfPlaying()
        val length = r.end - r.start
        // O lote para inteiro no zero: nenhuma distância entre as camadas encolhe.
        val floorStart = r.start - minStart
        // O clipe fica SOB o dedo no ponto em que foi pego.
        val grab = r.start - frameAt(down.position.x)
        val targets = snapTargets(ids, own = null, ownEdges = false, ownKeys = false)
        val out = IntArray(2)
        // Fase 8D: posições ABSOLUTAS a partir do começo do gesto. O delta era
        // medido contra a linha relida do motor, que pode ainda não ter o passo
        // anterior (o comando é aplicado no quadro do motor): passo repetido ou
        // perdido. Absoluto é idempotente e o gesto não precisa reler nada.
        val groupIds = group.toLongArray()
        val baseStarts = IntArray(groupIds.size) { rowById(groupIds[it])?.start ?: 0 }
        val baseEnds = IntArray(groupIds.size) { rowById(groupIds[it])?.end ?: 0 }
        val starts = IntArray(groupIds.size)
        val ends = IntArray(groupIds.size)
        var sent = 0
        dragLoop(down.id, down.position, horizontal = true) { p ->
            val desired = (frameAt(p.x) + grab).toFrame()
            Snap.span(targets, desired, length, playheadFrame(), (metrics.snapClip / pxPerFrame()).toDouble(), out)
            val target = max(out[0], floorStart)
            val delta = target - r.start
            if (delta != sent) {
                openUndo("mover")
                for (i in groupIds.indices) {
                    starts[i] = baseStarts[i] + delta
                    ends[i] = baseEnds[i] + delta
                }
                store.setLayerRanges(groupIds, starts, ends)
                sent = delta
            }
            setGuide(if (target == out[0]) out[1] else Snap.NONE)
        }
    }

    // --- Aparar ------------------------------------------------------------------------
    private suspend fun AwaitPointerEventScope.trimDrag(r: RowModel, start: Boolean, down: PointerInputChange) {
        light()
        pauseIfPlaying()
        val id = r.id
        val grab = (if (start) r.start else r.end) - frameAt(down.position.x)
        // Os keyframes da própria camada ficam parados no tempo ao aparar: bons alvos.
        val targets = snapTargets(longArrayOf(id), own = r, ownEdges = false, ownKeys = true)
        // Modo Edição: aparar o começo não move a camada — conta o que já foi.
        val magnetic = start && store.editMode
        var applied = 0
        dragLoop(down.id, down.position, horizontal = true) { p ->
            val desired = frameAt(p.x) + grab
            val snapped = Snap.nearest(targets, desired, playheadFrame(), (metrics.snapClip / pxPerFrame()).toDouble())
            val target = max(0, if (snapped != Snap.NONE) snapped else desired.toFrame())
            val cur = rowById(id) ?: return@dragLoop
            if (magnetic) {
                val step = (target - r.start) - applied
                if (step != 0) {
                    openUndo("aparar")
                    store.trimStartBy(id, step)
                    applied += step
                }
            } else if (start && target != cur.start) {
                openUndo("aparar")
                store.trimStart(id, target)
            } else if (!start && target != cur.end) {
                openUndo("aparar")
                store.trimEnd(id, target)
            }
            setGuide(snapped)
        }
    }

    // --- Arrastar losango -----------------------------------------------------------------
    /** Move o INSTANTE inteiro (todas as trilhas com marca ali), em frames, sem encostar no vizinho. */
    private suspend fun AwaitPointerEventScope.keyframeDrag(r: RowModel, index: Int, down: PointerInputChange) {
        if (index !in r.instants.indices) return consumeUntilUp()
        if (r.locked) {
            light()
            store.showToast("Camada bloqueada: desbloqueie para mover o keyframe")
            return consumeUntilUp()
        }
        light()
        pauseIfPlaying()
        if (!(selectionSize() == 1 && isSelected(r.id))) store.select(r.id)
        val keys = r.keysAt[index]
        val limits = IntArray(2)
        Keyframes.dragLimits(r.instants, index, r.start, r.end, limits)
        val targets = snapTargets(longArrayOf(r.id), own = r, ownEdges = true, ownKeys = false)
        var current = r.instants[index]
        val grab = current - frameAt(down.position.x)
        store.selectKeyframe(r.id, keys.first())
        state.dragKeyLayer = r.id
        state.dragKeyFrame = current
        dragLoop(down.id, down.position, horizontal = true) { p ->
            val desired = frameAt(p.x) + grab
            val snapped = Snap.nearest(targets, desired, playheadFrame(), (metrics.snapKey / pxPerFrame()).toDouble())
            val t = (if (snapped != Snap.NONE) snapped else desired.toFrame()).coerceIn(limits[0], limits[1])
            if (t != current) {
                openUndo("mover keyframe")
                val from = r.toLocal(current)
                val to = r.toLocal(t)
                for (k in keys) store.moveKeyframe(r.id, k.copy(time = from), to)
                current = t
                // O keyframe escolhido acompanha a marca.
                store.selectKeyframe(r.id, keys.first().copy(time = to))
                state.dragKeyFrame = t
            }
            setGuide(if (snapped != Snap.NONE && snapped == t) snapped else Snap.NONE)
        }
    }

    // --- Reordenar ---------------------------------------------------------------------------
    /** A linha não sai do lugar durante o gesto (véu + traço); aplica UMA vez ao soltar. */
    private suspend fun AwaitPointerEventScope.reorderDrag(r: RowModel, index: Int, down: PointerInputChange) {
        if (r.locked) {
            light()
            store.showToast("Camada bloqueada: desbloqueie para editar")
            return consumeUntilUp()
        }
        heavy()
        state.reorderSource = index
        state.reorderTarget = index
        val released = dragLoop(down.id, down.position, horizontal = false) { p ->
            val n = rowCount(rows.value)
            val t = rowIndexAt(p.y - metrics.rowsTop + clampedScroll(n)).coerceIn(0, maxOf(0, n - 1))
            if (t != state.reorderTarget) {
                tick()
                state.reorderTarget = t
            }
        }
        val target = state.reorderTarget
        if (released && target >= 0 && target != index) {
            openUndo("reordenar")
            store.reorderLayer(r.id, store.layers.indexOfFirst { it.id == rows.value.getOrNull(target)?.id }.coerceAtLeast(0))
            closeUndo()
            light()
        }
    }

    /**
     * Laço comum dos arrastos que editam: aplica com o dedo atual e liga a
     * auto-rolagem na borda (que reaplica com o dedo parado). Devolve se terminou
     * soltando o dedo (cancelamento lança e cai no `finally` do gesto).
     */
    private suspend fun AwaitPointerEventScope.dragLoop(
        id: PointerId,
        origin: Offset,
        horizontal: Boolean,
        apply: (Offset) -> Unit,
    ): Boolean {
        autoApply = apply
        autoHorizontal = horizontal
        autoOrigin = origin
        while (true) {
            val ev = awaitPointerEvent()
            val ch = changeOf(ev, id)
            if (ch == null || !ch.pressed) {
                consumeAll(ev)
                stopAutoScroll()
                return true
            }
            consumeAll(ev)
            lastPointer = ch.position
            apply(ch.position)
            updateAutoScroll(ch.position)
        }
    }

    // =========================================================================
    // Auto-rolagem (borda 38 dp, 120 dp/s constante, intenção 4 dp)
    // =========================================================================
    private var autoJob: Job? = null
    private var autoDirX = 0
    private var autoDirY = 0
    private var autoApply: ((Offset) -> Unit)? = null
    private var autoHorizontal = true
    private var autoOrigin = Offset.Zero
    private var lastPointer = Offset.Zero

    private fun updateAutoScroll(p: Offset) {
        val m = metrics
        autoDirX = if (autoHorizontal) {
            AutoScroll.direction(p.x, autoOrigin.x, m.headerColumn + m.autoEdge, state.width - m.autoEdge, m.autoIntent)
        } else {
            0
        }
        autoDirY = if (!autoHorizontal && !state.compact) {
            AutoScroll.direction(p.y, autoOrigin.y, m.rowsTop + m.autoEdge, state.height - m.autoEdge, m.autoIntent)
        } else {
            0
        }
        if ((autoDirX != 0 || autoDirY != 0) && autoJob == null) {
            autoJob = scope.launch {
                var last = withFrameNanos { it }
                while (autoDirX != 0 || autoDirY != 0) {
                    val now = withFrameNanos { it }
                    val step = metrics.autoSpeed * ((now - last) / 1e9f).coerceAtMost(MAX_FRAME_S)
                    last = now
                    // A auto-rolagem horizontal anda no tempo pelo scrub (o cabeçote segue a vista).
                    if (autoDirX != 0) holdView(view() + autoDirX * step / pxPerFrame())
                    if (autoDirY != 0) state.scrollY = clampScroll(clampedScroll(rowCount(rows.value)) + autoDirY * step)
                    autoApply?.invoke(lastPointer)
                }
                // Saiu da borda: a próxima entrada nela liga um laço novo.
                autoJob = null
            }
        }
    }

    private fun stopAutoScroll() {
        autoDirX = 0
        autoDirY = 0
        autoJob?.cancel()
        autoJob = null
        autoApply = null
    }

    // =========================================================================
    // Inércia
    // =========================================================================
    private var flingJob: Job? = null
    private var scrollJob: Job? = null

    /** Rolagem vertical com o fling do Android (Clamping). */
    private fun startScrollFling(vy: Float) {
        if (abs(vy) < metrics.flingMin) return
        val decay = splineBasedDecay<Float>(Density(metrics.density))
        scrollJob = scope.launch {
            var last = 0f
            AnimationState(0f, -vy).animateDecay(decay) {
                val before = state.scrollY
                val after = clampScroll(before + (value - last))
                last = value
                state.scrollY = after
                if (after == before) cancelAnimation()
            }
        }
    }

    /** Para as inércias; devolve se alguma ainda corria. */
    private fun stopFlings(): Boolean {
        var stopped = false
        val j = flingJob
        if (j != null) {
            flingJob = null
            j.cancel()
            releaseView()
            stopped = true
        }
        val s = scrollJob
        if (s != null) {
            stopped = stopped || s.isActive
            s.cancel()
            scrollJob = null
        }
        return stopped
    }

    // =========================================================================
    // Ímã: alvos reunidos uma vez por gesto
    // =========================================================================
    private fun snapTargets(excluded: LongArray, own: RowModel?, ownEdges: Boolean, ownKeys: Boolean): IntArray {
        val sorted = excluded.copyOf().also { it.sort() }
        var buf = IntArray(64)
        var n = 0
        fun add(v: Int) {
            if (n == buf.size) buf = buf.copyOf(n * 2)
            buf[n++] = v
        }
        add(0)
        // Marcas e batidas da régua: o ímã principal da edição no ritmo.
        val mk = store.markers
        for (i in 0 until mk.size) add(mk.frames[i])
        // No compacto as outras camadas não aparecem: grudar nelas pareceria aleatório.
        if (!state.compact) {
            val list = rows.value
            for (i in list.indices) {
                val row = list[i]
                if (Arrays.binarySearch(sorted, row.id) >= 0) continue
                add(row.start)
                add(row.end)
                for (t in row.instants) add(t)
            }
        }
        if (own != null) {
            if (ownEdges) {
                add(own.start)
                add(own.end)
            }
            if (ownKeys) for (t in own.instants) add(t)
        }
        return Snap.sortedDistinct(buf, n)
    }

    // =========================================================================
    // Revelar a camada escolhida e auto-zoom da A.01
    // =========================================================================
    /** Chave do revelar: muda quando a principal muda ou troca de linha. */
    fun revealKey(): Long {
        val p = primaryId.value
        val list = rows.value
        var idx = -1
        for (i in list.indices) if (list[i].id == p) idx = i
        return p * 31 + idx
    }

    fun reveal() {
        if (state.compact) return
        val p = primaryId.value
        if (p == NO_ID) return
        val list = rows.value
        var idx = -1
        for (i in list.indices) if (list[i].id == p) idx = i
        if (idx < 0) return
        val viewport = state.height - metrics.rowsTop
        if (viewport <= 0f) return
        val top = rowTop(idx)
        val bottom = top + rowHeight(list[idx])
        val s = clampedScroll(list.size)
        val target = when {
            top < s -> top
            bottom > s + viewport -> bottom - viewport
            else -> s
        }
        if (target != s) state.scrollY = target
    }

    /** A.01: projeto de 20 s ou mais nasce enquadrado na largura (uma vez; depois a pinça manda). */
    fun autoFit() {
        val path = store.project.path ?: return
        if (path == state.fittedPath || state.width <= 0) return
        val seconds = store.project.durationFrames / fps
        if (seconds < Zoom.AUTO_FIT_MIN_SECONDS) return
        state.fittedPath = path
        state.pps = Zoom.autoFit(state.width / metrics.density - AUTO_FIT_MARGIN_DP, seconds)
    }

    // =========================================================================
    // Utilidades de ponteiro (sem iteradores: o laço roda a cada evento)
    // =========================================================================
    private fun pressedCount(ev: PointerEvent): Int {
        var n = 0
        val list = ev.changes
        for (i in list.indices) if (list[i].pressed) n++
        return n
    }

    private fun changeOf(ev: PointerEvent, id: PointerId): PointerInputChange? {
        val list = ev.changes
        for (i in list.indices) if (list[i].id == id) return list[i]
        return null
    }

    private fun consumeAll(ev: PointerEvent) {
        val list = ev.changes
        for (i in list.indices) list[i].consume()
    }

    private suspend fun AwaitPointerEventScope.consumeUntilUp() {
        while (true) {
            val ev = awaitPointerEvent()
            consumeAll(ev)
            if (pressedCount(ev) == 0) return
        }
    }

    companion object {
        const val NO_ID = Long.MIN_VALUE
        /** Toque longo de 500 ms (a spec fixa; o padrão do Android é 400). */
        const val LONG_PRESS_MS = 500L
        private const val MAX_FRAME_S = 0.05f
        private const val AUTO_FIT_MARGIN_DP = 32f
    }
}
