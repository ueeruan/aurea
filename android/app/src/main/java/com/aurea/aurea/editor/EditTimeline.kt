package com.aurea.aurea.editor

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectDragGesturesAfterLongPress
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.calculatePan
import androidx.compose.foundation.gestures.calculateZoom
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.editor.timeline.WaveGrid
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

// =============================================================================
//  A LINHA DO TEMPO DA EDIÇÃO (0.2.5): o cabeçote fica PARADO no meio e o tempo
//  corre por baixo dele. Arrastar na horizontal anda no tempo; pinça aproxima;
//  arrastar na vertical rola as linhas. Embaixo da régua, a TRILHA PRINCIPAL
//  (miniaturas da mídia, "+" no fim) e as outras linhas por baixo dela
//  (camadas, texto e, por último, áudio). Vídeo com som mostra a ONDA do áudio
//  numa faixa embaixo das miniaturas; o corte de áudio é a onda inteira.
//
//  Num corte escolhido, as alças brancas aparam o começo e o fim (o motor
//  puxa os vizinhos da trilha magnética); segurar e arrastar reordena na
//  trilha principal ou move no tempo nas outras linhas. Tocar no vazio solta.
// =============================================================================

private val RulerH = 26.dp
private val MainH = 56.dp
private val RowH = 34.dp
private val RowGap = 6.dp
private val HandleW = 14.dp

/** Uma fileira da linha do tempo: os cortes dela e se é a trilha principal. */
internal class EditRow(val key: String, val clips: List<LayerRow>, val main: Boolean, val audio: Boolean)

/** Fileiras: a principal primeiro, depois as linhas de cima para baixo na pilha, e o áudio no fim. */
private fun editRows(layers: List<LayerRow>, anchor: Long): List<EditRow> {
    val anchorRow = layers.firstOrNull { it.id == anchor }
    fun keyOf(l: LayerRow) = if (l.trackId != 0) "t${l.trackId}" else "l${l.id}"
    val mainKey = anchorRow?.let(::keyOf)
    val byRow = layers.groupBy(::keyOf)
    val main = mainKey?.let { k -> byRow[k]?.let { EditRow(k, it.sortedBy { c -> c.startFrame }, main = true, audio = false) } }
    // Ordem de desenho: zIndex maior = na frente. A fileira mais à frente vem logo abaixo da principal.
    val others = byRow.filterKeys { it != mainKey }.map { (k, clips) ->
        EditRow(k, clips.sortedBy { it.startFrame }, main = false, audio = clips.all { it.kind == 3 })
    }.sortedWith(compareBy<EditRow> { it.audio }.thenByDescending { r -> r.clips.maxOf { it.zIndex } })
    return listOfNotNull(main) + others
}

@Composable
internal fun EditTimeline(
    store: EditorStore,
    modifier: Modifier,
    compact: Boolean,
    onAddMedia: () -> Unit,
    onAddAudio: () -> Unit,
    onTransition: (Long, Long) -> Unit = { _, _ -> },
) {
    val density = LocalDensity.current
    var pxPerSecond by remember { mutableFloatStateOf(with(density) { 72.dp.toPx() }) }
    var scrollY by remember { mutableFloatStateOf(0f) }
    // 0.3.6: a altura medida da linha do tempo (0 até o primeiro layout).
    var viewportH by remember { mutableIntStateOf(0) }
    val fps = store.project.fps.takeIf { it > 0f } ?: 30f
    val playhead = store.playhead
    val pxPerFrame = pxPerSecond / fps
    val rows = remember(store.layers, store.mainTrackAnchor) { editRows(store.layers, store.mainTrackAnchor) }
    val hasAudio = rows.any { it.audio }
    val timelineLabel = stringResource(R.string.edit_timeline)
    val currentPlayhead by rememberUpdatedState(playhead)
    val currentPxPerFrame by rememberUpdatedState(pxPerFrame)
    // 0.3.6: a rolagem vertical vai só até a última fileira encostar embaixo. Antes não
    // tinha teto e o arrasto de lado (com um pouco de vertical) levava as fileiras para
    // fora da tela: "o clipe some" ao deslizar logo depois de abrir o projeto.
    val layout = remember(rows, hasAudio, compact, density.density) { EditRowLayout.of(rows, hasAudio, compact, density) }
    val maxScroll = EditRowLayout.maxScroll(layout.contentBottom, viewportH.toFloat())
    val currentMaxScroll by rememberUpdatedState(maxScroll)
    val shownScroll = scrollY.coerceIn(0f, maxScroll)
    // 0.3.6: o corte escolhido rola para dentro da vista (com o painel aberto a linha do
    // tempo encolhe e a fileira dele ficava escondida embaixo, atrás do painel).
    val primary = store.primary
    LaunchedEffect(primary, layout, viewportH) {
        val id = primary ?: return@LaunchedEffect
        if (viewportH <= 0) return@LaunchedEffect
        val index = rows.indexOfFirst { r -> r.clips.any { it.id == id } }
        if (index < 0) return@LaunchedEffect
        val top = layout.rowTops[index] - layout.rowsTop
        val bottom = top + layout.rowHeights[index]
        val viewport = com.aurea.aurea.editor.timeline.TimelineReveal.viewport(viewportH.toFloat(), layout.rowsTop, 0f)
        val current = scrollY.coerceIn(0f, maxScroll)
        val target = com.aurea.aurea.editor.timeline.TimelineReveal.target(top, bottom, current, viewport).coerceIn(0f, maxScroll)
        if (target != current) scrollY = target
    }
    // Onda do som: uma janela por corte, pedida ao motor só quando a vista sai dela,
    // o zoom muda de degrau ou chega pedaço novo (a geração das miniaturas muda).
    val waves = remember { EditWaveCache() }
    val waveGeneration = store.thumbnailGeneration

    BoxWithConstraints(
        modifier.background(EditColors.Canvas).clipToBounds().testTag("edit.timeline")
            .onSizeChanged { viewportH = it.height }
            .semantics { contentDescription = timelineLabel }
            // Fundo: tocar solta a seleção; arrastar anda no tempo (e rola as
            // linhas na vertical); pinça muda a escala.
            .pointerInput(store) { detectTapGestures { store.clearSelection() } }
            .pointerInput(store) {
                // 0.3.6: o eixo é decidido UMA vez por gesto (depois da folga do toque):
                // de lado só anda no tempo, de pé só rola as fileiras. O tempo anda a
                // partir do quadro em que o gesto começou (o cabeçote do motor chega
                // atrasado; somar ao valor velho fazia o conteúdo pular).
                val slop = viewConfiguration.touchSlop
                awaitEachGesture {
                    awaitFirstDown(requireUnconsumed = false)
                    var axis = EditPanAxis.NONE
                    var total = Offset.Zero
                    var startFrame = 0
                    var pinching = false
                    while (true) {
                        val event = awaitPointerEvent()
                        // Um corte (alça de aparar, segurar e arrastar) ficou com o gesto.
                        if (event.changes.any { it.isConsumed }) break
                        val pressed = event.changes.count { it.pressed }
                        if (pressed == 0) break
                        if (pressed >= 2) pinching = true
                        if (pinching) {
                            val zoom = event.calculateZoom()
                            if (zoom != 1f) {
                                pxPerSecond = (pxPerSecond * zoom).coerceIn(with(density) { 12.dp.toPx() }, with(density) { 640.dp.toPx() })
                            }
                            event.changes.forEach { it.consume() }
                            continue
                        }
                        val pan = event.calculatePan()
                        total += pan
                        if (axis == EditPanAxis.NONE && total.getDistance() > slop) {
                            axis = EditPanAxis.of(total.x, total.y)
                            if (axis == EditPanAxis.TIME) {
                                if (store.playing) store.pause()
                                startFrame = currentPlayhead
                                total = Offset.Zero
                            }
                        }
                        when (axis) {
                            EditPanAxis.TIME -> {
                                val frame = (startFrame - total.x / currentPxPerFrame).roundToInt()
                                    .coerceIn(0, store.project.durationFrames.coerceAtLeast(0))
                                if (frame != currentPlayhead) store.seek(frame)
                            }
                            EditPanAxis.ROWS -> scrollY = (scrollY.coerceIn(0f, currentMaxScroll) - pan.y).coerceIn(0f, currentMaxScroll)
                            else -> {}
                        }
                        if (axis != EditPanAxis.NONE) event.changes.forEach { it.consume() }
                    }
                }
            },
    ) {
        val width = constraints.maxWidth.toFloat()
        val centerX = width / 2f
        fun xOf(frame: Int): Float = centerX + (frame - playhead) * pxPerFrame

        // Régua (fixa no topo).
        Canvas(Modifier.fillMaxWidth().height(RulerH)) {
            val step = rulerStep(pxPerSecond, with(density) { 64.dp.toPx() })
            val firstSecond = ((playhead / fps) - centerX / pxPerSecond).toInt().coerceAtLeast(0)
            val lastSecond = ((playhead / fps) + centerX / pxPerSecond).toInt() + 1
            var s = (firstSecond / step) * step
            while (s <= lastSecond) {
                val x = centerX + (s - playhead / fps) * pxPerSecond
                drawCircle(EditColors.Ruler, radius = 1.5.dp.toPx(), center = Offset(x + step * pxPerSecond / 2f, size.height * 0.62f))
                s += step
            }
        }
        RulerLabels(playhead, fps, pxPerSecond, centerX)

        // As fileiras, roladas na vertical.
        val mainHpx = with(density) { MainH.toPx() }
        val rowHpx = with(density) { RowH.toPx() }
        val gapPx = with(density) { RowGap.toPx() }
        val top = with(density) { (RulerH + 8.dp).toPx() } - shownScroll
        var y = top
        val anchorRow = rows.firstOrNull { it.main }
        if (anchorRow == null) {
            val emptyY = y
            EmptyMainTrack(Modifier.offset { IntOffset(0, emptyY.roundToInt()) }.fillMaxWidth().height(MainH), onAddMedia)
            y += mainHpx + gapPx
        }
        rows.forEach { row ->
            val h = if (row.main) mainHpx else rowHpx
            val rowTop = y
            row.clips.forEach { clip ->
                val left = xOf(clip.startFrame)
                val right = xOf(clip.endFrame)
                if (right < 0f || left > width) return@forEach
                // Só o trecho que cabe na tela é desenhado: o Compose limita a
                // largura de um filho à do pai, e um corte mais longo que a tela
                // saía cortado no meio. A borda fora da tela não mostra alça.
                val drawLeft = maxOf(left, 0f)
                val drawRight = minOf(right, width)
                EditClip(
                    store, clip, row.main, drawLeft, drawRight - drawLeft, rowTop, h,
                    pxPerFrame, playhead,
                    startVisible = left >= 0f, endVisible = right <= width, contentShift = drawLeft - left,
                    waves = waves, waveGeneration = waveGeneration,
                )
            }
            if (row.main) {
                // O quadradinho de TRANSIÇÃO em cada corte encostado da trilha principal.
                row.clips.zipWithNext().forEach { (a, b) ->
                    if (a.endFrame == b.startFrame) {
                        val cx = xOf(a.endFrame)
                        if (cx > -width && cx < width * 2) {
                            CutButton(
                                Modifier.offset { IntOffset((cx - with(density) { 11.dp.toPx() }).roundToInt(), (rowTop + (h - with(density) { 22.dp.toPx() }) / 2f).roundToInt()) },
                                a, b, onTransition,
                            )
                        }
                    }
                }
                // "+" no fim da trilha principal (fica na borda quando o fim sai da tela).
                val end = row.clips.maxOf { it.endFrame }
                val x = (xOf(end) + with(density) { 10.dp.toPx() }).coerceAtMost(width - with(density) { 46.dp.toPx() })
                AddButton(Modifier.offset { IntOffset(x.roundToInt(), (rowTop + (h - with(density) { 36.dp.toPx() }) / 2f).roundToInt()) }, onAddMedia)
            }
            y += h + gapPx
            if (row.main && !hasAudio && !compact) {
                val laneY = y
                AddAudioLane(Modifier.offset { IntOffset(centerX.roundToInt() + with(density) { 8.dp.roundToPx() }, laneY.roundToInt()) }, onAddAudio)
                y += rowHpx + gapPx
            }
        }

        // O cabeçote fixo no meio.
        Box(
            Modifier.offset { IntOffset((centerX - with(density) { 1.dp.toPx() }).roundToInt(), 0) }
                .width(2.dp).fillMaxHeight().background(Color.White),
        )
        Box(
            Modifier.offset { IntOffset((centerX - with(density) { 5.dp.toPx() }).roundToInt(), with(density) { (RulerH - 4.dp).roundToPx() }) }
                .size(10.dp).clip(RoundedCornerShape(5.dp)).background(Color.White),
        )
        // Ímã da trilha principal, no canto (o estado do motor, um toque troca).
        MagnetToggle(store, Modifier.align(Alignment.TopStart).padding(start = 6.dp, top = 2.dp))
    }
}

/** 0.3.6: o eixo de um arrasto de um dedo na linha do tempo da Edição, decidido uma vez. */
internal object EditPanAxis {
    const val NONE = 0
    /** De lado: anda no tempo. */
    const val TIME = 1
    /** De pé: rola as fileiras. */
    const val ROWS = 2

    /** Mais de lado que de pé (empate = tempo, o gesto principal). */
    fun of(dx: Float, dy: Float): Int = if (kotlin.math.abs(dx) >= kotlin.math.abs(dy)) TIME else ROWS
}

/**
 * 0.3.6: onde cada fileira fica (sem rolagem, px a partir do topo da linha do tempo)
 * e onde o conteúdo termina — a mesma conta do desenho, para o teto da rolagem e
 * para trazer o corte escolhido à vista.
 */
internal class EditRowLayout(val rowsTop: Float, val rowTops: FloatArray, val rowHeights: FloatArray, val contentBottom: Float) {
    companion object {
        /** Folga embaixo da última fileira. */
        private val BottomPad = 12.dp

        fun of(rows: List<EditRow>, hasAudio: Boolean, compact: Boolean, density: androidx.compose.ui.unit.Density): EditRowLayout =
            with(density) {
                compute(
                    rows.map { it.main }, hasAudio, compact,
                    rulerTop = (RulerH + 8.dp).toPx(), main = MainH.toPx(), row = RowH.toPx(), gap = RowGap.toPx(), pad = BottomPad.toPx(),
                )
            }

        /** A conta pura (testada na JVM): [mains] diz quais fileiras são a trilha principal. */
        fun compute(mains: List<Boolean>, hasAudio: Boolean, compact: Boolean, rulerTop: Float, main: Float, row: Float, gap: Float, pad: Float): EditRowLayout {
            var y = rulerTop
            if (mains.none { it }) y += main + gap
            val tops = FloatArray(mains.size)
            val heights = FloatArray(mains.size)
            mains.forEachIndexed { i, isMain ->
                val h = if (isMain) main else row
                tops[i] = y
                heights[i] = h
                y += h + gap
                if (isMain && !hasAudio && !compact) y += row + gap
            }
            return EditRowLayout(rulerTop, tops, heights, y - gap + pad)
        }

        /** O teto da rolagem: a última fileira encosta embaixo (nunca negativo). */
        fun maxScroll(contentBottom: Float, viewport: Float): Float =
            if (viewport <= 0f) 0f else kotlin.math.max(0f, contentBottom - viewport)
    }
}

/** Passo dos rótulos da régua, em segundos, para caber ~[minGapPx] entre eles. */
private fun rulerStep(pxPerSecond: Float, minGapPx: Float): Int =
    listOf(1, 2, 5, 10, 15, 30, 60, 120, 300).firstOrNull { it * pxPerSecond >= minGapPx } ?: 600

@Composable
private fun RulerLabels(playhead: Int, fps: Float, pxPerSecond: Float, centerX: Float) {
    val density = LocalDensity.current
    val step = rulerStep(pxPerSecond, with(density) { 64.dp.toPx() })
    val first = ((playhead / fps) - centerX / pxPerSecond).toInt().coerceAtLeast(0)
    val last = ((playhead / fps) + centerX / pxPerSecond).toInt() + 1
    var s = (first / step) * step
    while (s <= last) {
        val x = centerX + (s - playhead / fps) * pxPerSecond
        val label = "%02d:%02d".format(java.util.Locale.ROOT, s / 60, s % 60)
        Text(label, style = AureaType.of(10f, color = EditColors.Ruler),
            modifier = Modifier.offset { IntOffset((x - with(density) { 14.dp.toPx() }).roundToInt(), with(density) { 6.dp.roundToPx() }) })
        s += step
    }
}

/**
 * Um corte. Toque escolhe; escolhido, ganha a borda branca com as alças de
 * aparar; segurar e arrastar reordena (trilha principal) ou move no tempo.
 */
@Composable
private fun EditClip(
    store: EditorStore,
    clip: LayerRow,
    main: Boolean,
    x: Float,
    widthPx: Float,
    y: Float,
    heightPx: Float,
    pxPerFrame: Float,
    playhead: Int,
    startVisible: Boolean = true,
    endVisible: Boolean = true,
    contentShift: Float = 0f,
    waves: EditWaveCache? = null,
    waveGeneration: Int = 0,
) {
    val density = LocalDensity.current
    val selected = clip.id in store.selection
    var trimStart by remember(clip.id) { mutableFloatStateOf(0f) }
    var trimEnd by remember(clip.id) { mutableFloatStateOf(0f) }
    var drag by remember(clip.id) { mutableFloatStateOf(0f) }
    val currentClip by rememberUpdatedState(clip)
    val currentPxPerFrame by rememberUpdatedState(pxPerFrame)
    val color = LayerType.of(clip.kind).color
    val name = clip.name.ifEmpty { stringResource(R.string.edit_main_track) }
    val description = if (selected) stringResource(R.string.edit_clip_selected, name) else name
    val left = x + trimStart + drag
    val w = (widthPx - trimStart + trimEnd).coerceAtLeast(with(density) { 8.dp.toPx() })
    Box(
        Modifier
            .offset { IntOffset(left.roundToInt(), y.roundToInt()) }
            .size(with(density) { w.toDp() }, with(density) { heightPx.toDp() })
            .clip(RoundedCornerShape(6.dp))
            .background(if (main) Color(0xFF1E1E22) else color.copy(alpha = 0.85f))
            .then(if (selected) Modifier.border(2.dp, Color.White, RoundedCornerShape(6.dp)) else Modifier)
            .testTag("edit.clip.${clip.id}")
            .semantics { contentDescription = description; this.selected = selected; role = Role.Button }
            .pointerInput(clip.id) { detectTapGestures { store.select(currentClip.id) } }
            .pointerInput(clip.id, main) {
                detectDragGesturesAfterLongPress(
                    onDragStart = { if (store.playing) store.pause(); store.select(currentClip.id) },
                    onDragEnd = {
                        val frames = (drag / currentPxPerFrame).roundToInt()
                        val target = (currentClip.startFrame + frames).coerceAtLeast(0)
                        if (frames != 0) {
                            if (main) store.reorderClip(currentClip.id, target.toLong())
                            else store.editClipTime(6, target)
                        }
                        drag = 0f
                    },
                    onDragCancel = { drag = 0f },
                    onDrag = { change, amount -> change.consume(); drag += amount.x },
                )
            },
    ) {
        val audioClip = clip.kind == 3
        if (main && (clip.kind == 1 || clip.kind == 2 || clip.kind == 12)) {
            Filmstrip(store, clip, pxPerFrame, heightPx, trimStart - contentShift)
        } else if (!audioClip) {
            Text(
                clip.name, style = AureaType.of(11f, FontWeight.W600, color = Color.White), maxLines = 1, overflow = TextOverflow.Ellipsis,
                modifier = Modifier.align(Alignment.CenterStart).padding(horizontal = if (selected) 18.dp else 8.dp),
            )
        }
        if (waves != null && (clip.kind == 1 || audioClip)) {
            // Vídeo: faixa escura embaixo das miniaturas (ou do nome), barras subindo
            // do pé. Áudio: a onda ocupa o corte inteiro, espelhada no meio.
            val bandH = if (audioClip) heightPx else with(density) { (if (main) 16.dp else 10.dp).toPx() }
            ClipWaveform(
                store, clip, waves, waveGeneration, pxPerFrame, trimStart - contentShift, band = !audioClip,
                modifier = Modifier.align(Alignment.BottomStart).fillMaxWidth().height(with(density) { bandH.toDp() })
                    .testTag("edit.clip.${clip.id}.wave"),
            )
        }
        if (audioClip) {
            Text(
                clip.name, style = AureaType.of(10f, FontWeight.W600, color = Color.White), maxLines = 1, overflow = TextOverflow.Ellipsis,
                modifier = Modifier.align(Alignment.TopStart).padding(start = if (selected) 18.dp else 6.dp, top = 2.dp),
            )
        }
        if (main && selected) {
            // A duração do corte, no canto (como nos editores de vídeo).
            val fps = store.project.fps.takeIf { it > 0f } ?: 30f
            Text(
                "%.1fs".format(java.util.Locale.ROOT, (clip.endFrame - clip.startFrame) / fps),
                style = AureaType.of(10f, FontWeight.W600, color = Color.White),
                modifier = Modifier.align(Alignment.TopStart).padding(start = 18.dp, top = 3.dp)
                    .clip(RoundedCornerShape(4.dp)).background(Color.Black.copy(alpha = 0.55f)).padding(horizontal = 4.dp),
            )
        }
        if (selected) {
            if (startVisible) TrimHandle(Modifier.align(Alignment.CenterStart), stringResource(R.string.edit_trim_start)) { dx, done ->
                if (!done) trimStart += dx
                else {
                    val frames = (trimStart / currentPxPerFrame).roundToInt()
                    if (frames != 0) store.editClipTime(0, currentClip.startFrame + frames)
                    trimStart = 0f
                }
            }
            if (endVisible) TrimHandle(Modifier.align(Alignment.CenterEnd), stringResource(R.string.edit_trim_end)) { dx, done ->
                if (!done) trimEnd += dx
                else {
                    val frames = (trimEnd / currentPxPerFrame).roundToInt()
                    if (frames != 0) store.editClipTime(1, currentClip.endFrame + frames)
                    trimEnd = 0f
                }
            }
        }
    }
}

/** A alça branca de aparar: arrastar mostra o novo limite; soltar manda ao motor. */
@Composable
private fun TrimHandle(modifier: Modifier, label: String, onDrag: (Float, Boolean) -> Unit) {
    val current by rememberUpdatedState(onDrag)
    Box(
        modifier.width(HandleW).fillMaxHeight().background(Color.White)
            .semantics { contentDescription = label }
            .pointerInput(Unit) {
                detectHorizontalDragGestures(
                    onDragEnd = { current(0f, true) },
                    onDragCancel = { current(0f, true) },
                    onHorizontalDrag = { change, dx -> change.consume(); current(dx, false) },
                )
            },
        contentAlignment = Alignment.Center,
    ) {
        Box(Modifier.width(2.dp).height(14.dp).clip(RoundedCornerShape(1.dp)).background(Color.Black.copy(alpha = 0.7f)))
    }
}

/** As miniaturas da mídia ao longo do corte (um ladrilho por largura de altura × 16:9). */
@Composable
private fun Filmstrip(store: EditorStore, clip: LayerRow, pxPerFrame: Float, heightPx: Float, trimShift: Float) {
    val fps = store.project.fps.takeIf { it > 0f } ?: 30f
    val bucket = (fps * 0.25f).coerceAtLeast(1f)
    Canvas(Modifier.fillMaxSize()) {
        val tileW = heightPx * 16f / 9f
        val tiles = (size.width / tileW).toInt() + 1
        for (i in 0 until tiles) {
            val px = i * tileW
            val frameOffset = ((px - trimShift) / pxPerFrame)
            val frame = ((clip.startFrame + frameOffset) / bucket).roundToInt() * bucket
            val bmp = store.thumbnails.get(clip.id, frame.toInt().coerceAtLeast(clip.startFrame), heightPx.roundToInt().coerceIn(16, 256))
            if (bmp != null) {
                val image = bmp.asImageBitmap()
                val scale = size.height / image.height
                drawImage(
                    image,
                    srcOffset = IntOffset.Zero, srcSize = IntSize(image.width, image.height),
                    dstOffset = IntOffset(px.roundToInt(), 0),
                    dstSize = IntSize((image.width * scale).roundToInt(), size.height.roundToInt()),
                )
            } else {
                drawRect(Color(0xFF2A2A30), topLeft = Offset(px, 0f), size = Size(tileW - 1f, size.height))
            }
        }
    }
}

/**
 * Onda do som de um corte (a mesma do motor que a Composição desenha, ver
 * `Engine::query_waveform`), só no trecho visível: um balde a cada ~1,5 dp na
 * grade fixa do tempo ([WaveGrid]). Corte sem som não desenha nada.
 */
@Composable
private fun ClipWaveform(
    store: EditorStore,
    clip: LayerRow,
    waves: EditWaveCache,
    generation: Int,
    pxPerFrame: Float,
    trimShift: Float,
    band: Boolean,
    modifier: Modifier,
) {
    val density = LocalDensity.current
    val bucketPx = with(density) { 1.5.dp.toPx() }
    Canvas(modifier) {
        if (pxPerFrame <= 0f || size.width < 1f || size.height < 1f) return@Canvas
        val fpb = WaveGrid.framesPerBucket(bucketPx, pxPerFrame)
        // Frames da timeline nas bordas desta caixa (x = 0 e x = largura).
        val fromFrame = clip.startFrame + (-trimShift / pxPerFrame).toDouble()
        val toFrame = fromFrame + size.width / pxPerFrame
        val first = WaveGrid.bucketAt(max(fromFrame, clip.startFrame.toDouble()), fpb)
        val last = WaveGrid.bucketAt(min(toFrame, clip.endFrame.toDouble()) - 1e-6, fpb)
        if (last < first) return@Canvas
        val wave = waves.get(store, clip, fpb, generation, first, last) ?: return@Canvas
        if (band) drawRect(Color.Black.copy(alpha = 0.45f))
        val stroke = max(1f, (fpb * pxPerFrame).toFloat() * 0.72f)
        val top = size.height * 0.12f
        val color = Color.White.copy(alpha = if (band) 0.8f else 0.75f)
        var b = first
        while (b <= last) {
            val v = wave.at(b) / 255f
            val x = (((b + 0.5) * fpb - clip.startFrame) * pxPerFrame).toFloat() + trimShift
            b++
            if (v <= 0f) continue
            if (band) {
                val h = v * (size.height - top)
                drawLine(color, Offset(x, size.height - h), Offset(x, size.height), strokeWidth = stroke)
            } else {
                val half = v * (size.height - top) / 2f
                val mid = (size.height + top) / 2f
                drawLine(color, Offset(x, mid - half), Offset(x, mid + half), strokeWidth = stroke)
            }
        }
    }
}

/**
 * Janelas da onda por corte. O motor só é consultado quando a vista sai da
 * janela guardada, o degrau de zoom muda, o corte muda de lugar ou chega
 * pedaço novo da onda: tocando, o quadro só lê números daqui.
 */
internal class EditWaveCache {
    class Wave(val first: Long, val count: Int, private val data: ByteArray) {
        fun at(k: Long): Int = if (k < first || k >= first + count) 0 else data[(k - first).toInt()].toInt() and 0xFF
    }

    private class Entry(
        val fpb: Double, val generation: Int, val start: Int, val end: Int, val offset: Int,
        val w0: Long, val w1: Long, val wave: Wave?,
    )

    private val entries = HashMap<Long, Entry>()
    private val buf: java.nio.ByteBuffer = java.nio.ByteBuffer.allocateDirect(MAX)
    private val win = LongArray(2)

    fun get(store: EditorStore, clip: LayerRow, fpb: Double, generation: Int, first: Long, last: Long): Wave? {
        if (last - first + 1 > MAX) return null
        entries[clip.id]?.let { e ->
            if (e.fpb == fpb && e.generation == generation && e.start == clip.startFrame && e.end == clip.endFrame &&
                e.offset == clip.offsetFrames && first >= e.w0 && last < e.w1) return e.wave
        }
        WaveGrid.window(first, last, MAX, win)
        val n = (win[1] - win[0]).toInt().coerceIn(0, MAX)
        buf.clear()
        val got = if (n > 0) store.queryWaveform(clip.id, win[0] * fpb, fpb, n, buf).coerceIn(0, n) else 0
        val wave = if (got > 0) Wave(win[0], got, ByteArray(got).also { buf.position(0); buf.get(it, 0, got) }) else null
        if (entries.size > 64) entries.clear()
        entries[clip.id] = Entry(fpb, generation, clip.startFrame, clip.endFrame, clip.offsetFrames, win[0], win[0] + n, wave)
        return wave
    }

    private companion object { const val MAX = 4096 }
}

@Composable
private fun AddButton(modifier: Modifier, onClick: () -> Unit) {
    val label = stringResource(R.string.edit_add_media)
    Box(
        modifier.size(36.dp).clip(RoundedCornerShape(8.dp)).background(Color.White).testTag("edit.add")
            .semantics { contentDescription = label; role = Role.Button }
            .tocavel(shrink = 0.9f, haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { CupertinoIcon(CupertinoGlyph.Plus, 20.dp, Color.Black) }
}

@Composable
private fun EmptyMainTrack(modifier: Modifier, onAddMedia: () -> Unit) {
    Row(modifier.padding(horizontal = 16.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.edit_empty_hint), style = AureaType.of(13f, color = AureaColors.Muted), modifier = Modifier.weight(1f))
        AddButton(Modifier, onAddMedia)
    }
}

@Composable
private fun AddAudioLane(modifier: Modifier, onClick: () -> Unit) {
    val label = stringResource(R.string.edit_add_audio_lane)
    Row(
        modifier.height(RowH).clip(RoundedCornerShape(6.dp)).background(Color(0xFF16161A))
            .testTag("edit.addAudio").semantics { contentDescription = label; role = Role.Button }
            .tocavel(shrink = 0.96f, onClick = onClick).padding(horizontal = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(CupertinoGlyph.Plus, 14.dp, AureaColors.Muted)
        Text(label, style = AureaType.of(12f, color = AureaColors.Muted), modifier = Modifier.padding(start = 6.dp))
    }
}

@Composable
private fun MagnetToggle(store: EditorStore, modifier: Modifier) {
    val label = stringResource(R.string.edit_magnet)
    val on = store.editMode
    Box(
        modifier.size(22.dp).clip(RoundedCornerShape(6.dp)).background(if (on) AureaColors.AccentDim else Color(0xFF1B1B1F))
            .testTag("edit.magnet").semantics { contentDescription = label; selected = on; role = Role.Switch }
            .tocavel(shrink = 0.9f) { store.toggleEditMode() },
        contentAlignment = Alignment.Center,
    ) { CupertinoIcon(CupertinoGlyph.Link, 13.dp, if (on) AureaColors.Accent else AureaColors.Muted) }
}

/** O quadradinho branco num corte da trilha principal: abre as transições daquele corte. */
@Composable
private fun CutButton(modifier: Modifier, a: LayerRow, b: LayerRow, onTransition: (Long, Long) -> Unit) {
    val label = stringResource(R.string.edit_transition_between, a.name, b.name)
    Box(
        modifier.size(22.dp).clip(RoundedCornerShape(5.dp)).background(Color.White).testTag("edit.cut.${a.id}.${b.id}")
            .semantics { contentDescription = label; role = Role.Button }
            .tocavel(shrink = 0.9f, haptic = true) { onTransition(a.id, b.id) },
        contentAlignment = Alignment.Center,
    ) { CupertinoIcon(CupertinoGlyph.ArrowLeftRight, 12.dp, Color.Black) }
}
