package com.aurea.aurea.editor

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.gestures.calculateCentroid
import androidx.compose.foundation.gestures.calculatePan
import androidx.compose.foundation.gestures.calculateZoom
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.graphics.graphicsLayer
import com.aurea.aurea.ui.platform.platformCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.positionChanged
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.layout
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.Constraints
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
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.floor

// =============================================================================
//  A timeline mobile mantém a faixa principal seguida de overlays e áudio.
//  Faixas vazias e ferramentas avançadas ficam no menu. O conteúdo desliza sob
//  o cabeçote central, limitado ao início e ao fim da sequência.
//
//  Adaptado ao toque: arrastar no vazio rola a linha do tempo, a régua move o
//  cabeçote e a pinça aproxima. Num dedo só, quem decide o que o arrasto faz é a
//  ferramenta escolhida na faixa de cima; o clipe escolhido responde na hora, e
//  o que não está escolhido pede um toque (ou segurar), para rolar nunca virar
//  edição sem querer.
//
//  Tudo é desenhado num Canvas só, com o cabeçote e a rolagem lidos na fase de
//  desenho: tocar um vídeo não recompõe a tela. As regras de cada edição moram
//  no motor (EngineTracks.inl); aqui só se escolhe a operação e o quadro.
// =============================================================================

private val RulerH = 20.dp
private val RowsTop = 50.dp
private val LaneH = 50.dp
private val CompactLaneH = 40.dp
private val LaneGap = 8.dp
private val EdgeW = 13.dp

private object EditPalette {
    val Lane get() = AureaColors.EditorCanvas
    val Head = Color(0xFF08090B)
    val RulerBg get() = AureaColors.EditorCanvas
    val Tick get() = AureaColors.TickWeak
    val Separator get() = AureaColors.EditorBarLine
    val Video = Color(0xFF1F6F8B)
    val VideoTop = Color(0xFF17566C)
    val Overlay = Color(0xFF7A2FA8)
    val OverlayTop = Color(0xFF5E2482)
    val Adjustment = Color(0xFFD016C4)
    val AdjustmentTop = Color(0xFFA30F99)
    val Audio = Color(0xFF2F6A23)
    val AudioWave = Color(0xFF9FE08A)
    val LinkedAudio = Color(0xFF17465A)
    val LinkedWave = Color(0xFF67C4F2)
    val Playhead get() = AureaColors.Accent
    val Selected get() = AureaColors.Accent
    val Range = Color(0x334EA8F2)
    val Snap = Color(0xFFFFD54A)
    val Cut = Color(0xFFE8EDF5)
}

/** Faixa como a interface mostra: `label` é V1, A2… */
internal class EditTrackInfo(val id: Int, val audio: Boolean, val locked: Boolean, val muted: Boolean, val solo: Boolean, val target: Boolean, val label: String)

/** O que `Engine::query_edit_timeline` devolve, em fileiras prontas para desenhar. */
internal class EditTimelineData(val tracks: List<EditTrackInfo>, val linkedAudio: Map<Long, Int>, val markIn: Long, val markOut: Long, val mainTrackId: Int = 0) {
    /** A apresentação mobile traz a principal primeiro; a leitura antiga mantém sua ordem. */
    val lanes: List<EditTrackInfo> = tracks.filter { it.id == mainTrackId } +
        tracks.filter { !it.audio && it.id != mainTrackId }.reversed() + tracks.filter { it.audio }

    companion object {
        fun parse(raw: LongArray): EditTimelineData? {
            if (raw.size < 4) return null
            val trackCount = raw[0].toInt()
            val linkedCount = raw[1].toInt()
            if (trackCount < 0 || linkedCount < 0 || raw.size < 4 + trackCount * 2 + linkedCount * 2) return null
            var video = 0
            var audio = 0
            val tracks = (0 until trackCount).map { i ->
                val flags = raw[5 + i * 2].toInt()
                val isAudio = flags and 1 != 0
                EditTrackInfo(
                    raw[4 + i * 2].toInt(), isAudio, flags and 2 != 0, flags and 4 != 0, flags and 8 != 0, flags and 16 != 0,
                    if (isAudio) "A${++audio}" else "V${++video}",
                )
            }
            val linked = HashMap<Long, Int>(linkedCount)
            for (i in 0 until linkedCount) linked[raw[4 + trackCount * 2 + i * 2]] = raw[5 + trackCount * 2 + i * 2].toInt()
            return EditTimelineData(tracks, linked, raw[2], raw[3])
        }
    }
}

/** A ferramenta da faixa de cima; as letras do Premiere viram nomes no toque. */
internal enum class EditTool(val label: Int, val tag: String, val icon: EditIcon) {
    Select(R.string.edit_tool_select, "select", EditIcon.Select),
    Ripple(R.string.edit_tool_ripple, "ripple", EditIcon.Ripple),
    Roll(R.string.edit_tool_roll, "roll", EditIcon.Roll),
    Razor(R.string.edit_tool_razor, "razor", EditIcon.Razor),
    Slip(R.string.edit_tool_slip, "slip", EditIcon.Slip),
    Slide(R.string.edit_tool_slide, "slide", EditIcon.Slide),
}

/** Números de `TrackEditOp` (Engine.hpp). */
internal object TrackOp {
    const val MOVE = 0
    const val MOVE_INSERT = 1
    const val TRIM_IN = 2
    const val TRIM_OUT = 3
    const val RIPPLE_TRIM_IN = 4
    const val RIPPLE_TRIM_OUT = 5
    const val ROLL = 6
    const val SLIP = 7
    const val SLIDE = 8
    const val RAZOR = 9
    const val RAZOR_ALL = 10
    const val RIPPLE_DELETE = 11
    const val LIFT = 12
    const val MOVE_LINKED_AUDIO = 13
}

/** Onde cada faixa fica (px, sem rolagem) e onde o conteúdo termina. A conta é testada na JVM. */
internal class EditLaneLayout(val tops: FloatArray, val heights: FloatArray, val bottom: Float) {
    fun laneAt(y: Float): Int {
        for (i in tops.indices) if (y >= tops[i] && y < tops[i] + heights[i]) return i
        return -1
    }

    companion object {
        fun compute(count: Int, rulerH: Float, laneH: Float, gap: Float): EditLaneLayout {
            val tops = FloatArray(count) { rulerH + it * (laneH + gap) }
            return EditLaneLayout(tops, FloatArray(count) { laneH }, rulerH + count * (laneH + gap))
        }

        /** O teto da rolagem: a última faixa encosta embaixo (nunca negativo). */
        fun maxScroll(bottom: Float, viewport: Float): Float = if (viewport <= 0f) 0f else max(0f, bottom - viewport)

        fun clampHorizontal(offset: Float, duration: Int, viewport: Float, pxPerFrame: Float): Float {
            if (!offset.isFinite() || !viewport.isFinite() || !pxPerFrame.isFinite() || viewport <= 0f || pxPerFrame <= 0f) return 0f
            val lastOffset = max(0f, duration.toFloat() - viewport / pxPerFrame)
            return offset.coerceIn(0f, lastOffset)
        }

        fun clampCentered(offset: Float, duration: Int): Float =
            if (offset.isFinite()) offset.coerceIn(0f, duration.coerceAtLeast(0).toFloat()) else 0f
    }
}

/** Encaixe: o alvo mais perto de `frame` dentro de `reach` quadros, ou o próprio `frame`. */
internal fun snapFrame(frame: Int, targets: IntArray, reach: Float): Int {
    var best = frame
    var distance = reach
    for (t in targets) {
        val d = abs(t - frame).toFloat()
        if (d < distance) { distance = d; best = t }
    }
    return best
}

private class LaneClip(val row: LayerRow, val virtual: Boolean)

private const val DRAG_TRIM_IN = 1
private const val DRAG_TRIM_OUT = 2
private const val DRAG_MOVE = 3
private const val DRAG_SLIP = 4
private const val DRAG_SLIDE = 5
private const val DRAG_AUDIO_LANE = 6

private class ClipDrag(val kind: Int, val clip: LayerRow, val neighbour: LayerRow?, val targets: IntArray) {
    var frames by mutableIntStateOf(0)
    var lane by mutableIntStateOf(0)
    var snapped by mutableIntStateOf(-1)
}

@Stable
private class EditView(pxPerFrame: Float) {
    var pxPerFrame by mutableFloatStateOf(pxPerFrame)
    var scrollFrames by mutableFloatStateOf(0f)
    var scrollY by mutableFloatStateOf(0f)
    var drag by mutableStateOf<ClipDrag?>(null)
    var scrubbing by mutableStateOf(false)
    var revealedTrack by mutableIntStateOf(0)
}

@Composable
internal fun EditTimeline(
    store: EditorStore,
    modifier: Modifier,
    compact: Boolean,
    onAddMedia: () -> Unit,
    onTransition: (Long, Long) -> Unit = { _, _ -> },
    onAddAudio: (() -> Unit)? = null,
    onAddText: (() -> Unit)? = null,
) {
    val density = LocalDensity.current
    val fps = store.project.fps.takeIf { it > 0f } ?: 30f
    val view = remember(store) { EditView(with(density) { 72.dp.toPx() } / fps) }
    var tool by remember { mutableStateOf(EditTool.Select) }
    var snap by remember { mutableStateOf(true) }
    var more by remember { mutableStateOf(false) }
    val timelineLabel = stringResource(R.string.edit_timeline)
    val rawData = store.editTimeline
    val data = remember(rawData, store.layers, store.mainTrackAnchor, view.revealedTrack) {
        rawData?.let { raw ->
            val used = store.layers.mapTo(HashSet()) { it.trackId }
            used.addAll(raw.linkedAudio.values)
            val main = store.layers.firstOrNull { it.id == store.mainTrackAnchor }?.trackId?.takeIf { it != 0 }
                ?: raw.tracks.firstOrNull { !it.audio }?.id ?: 0
            val visible = raw.tracks.filter { it.id in used || it.id == main || it.id == view.revealedTrack }
            EditTimelineData(visible, raw.linkedAudio, raw.markIn, raw.markOut, main)
        }
    }

    BoxWithConstraints(modifier.background(EditPalette.Lane).clipToBounds().testTag("edit.timeline").semantics { contentDescription = timelineLabel }) {
        if (data != null) EditLanes(store, data, view, tool, snap, compact, fps,
            constraints.maxWidth.toFloat(), constraints.maxHeight.toFloat(), onTransition, onAddMedia, onAddAudio, onAddText)
        val moreLabel = stringResource(R.string.edit_more)
        Box(Modifier.align(Alignment.TopEnd).size(44.dp).background(EditPalette.Lane)
            .testTag("edit.tools.more").semantics { contentDescription = moreLabel; role = Role.Button }
            .tocavel { more = true }, contentAlignment = Alignment.Center) {
            CupertinoIcon(CupertinoGlyph.Ellipsis, 18.dp, AureaColors.Text)
        }
    }
    if (more) EditMoreSheet(store, tool, { tool = it }, snap, { snap = it }, { view.revealedTrack = it }) { more = false }
}

/** O que não é ferramenta de arrasto, em três grupos. */
@Composable
private fun EditMoreSheet(store: EditorStore, tool: EditTool, onTool: (EditTool) -> Unit,
                          snap: Boolean, onSnap: (Boolean) -> Unit, onRevealTrack: (Int) -> Unit, onDismiss: () -> Unit) {
    com.aurea.aurea.ui.ds.AureaModalSheet(onDismiss = onDismiss) {
        Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(start = 16.dp, end = 16.dp, bottom = 24.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
            EditTool.entries.chunked(3).forEach { tools ->
                Row(Modifier.fillMaxWidth()) {
                    tools.forEach { item ->
                        MoreTile(item.icon, stringResource(item.label), "edit.tool.${item.tag}", item == tool) { onTool(item); onDismiss() }
                    }
                }
            }
            Row(Modifier.fillMaxWidth()) {
                MoreTile(EditIcon.Snap, stringResource(R.string.edit_snap), "edit.snap", snap) { onSnap(!snap) }
            }
            MoreSection(stringResource(R.string.edit_section_marks)) {
                MoreTile(EditIcon.MarkIn, stringResource(R.string.edit_mark_in), "edit.markIn") { store.toggleEditMark(0); onDismiss() }
                MoreTile(EditIcon.MarkOut, stringResource(R.string.edit_mark_out), "edit.markOut") { store.toggleEditMark(1); onDismiss() }
                MoreTile(EditIcon.Lift, stringResource(R.string.edit_lift), "edit.lift") { store.editMarkedRange(false); onDismiss() }
                MoreTile(EditIcon.Extract, stringResource(R.string.edit_extract), "edit.extract") { store.editMarkedRange(true); onDismiss() }
            }
            MoreSection(stringResource(R.string.edit_section_tracks)) {
                MoreTile(EditIcon.RazorAll, stringResource(R.string.edit_razor_all), "edit.razorAll") { store.razorAllAtPlayhead(); onDismiss() }
                MoreTile(EditIcon.AddVideo, stringResource(R.string.edit_add_video_track), "edit.addVideoTrack") {
                    val track = store.addEditTrack(false)
                    if (track > 0) onRevealTrack(track)
                    onDismiss()
                }
                MoreTile(EditIcon.AddAudio, stringResource(R.string.edit_add_audio_track), "edit.addAudio") {
                    val track = store.addEditTrack(true)
                    if (track > 0) onRevealTrack(track)
                    onDismiss()
                }
                MoreTile(EditIcon.ClearTracks, stringResource(R.string.edit_remove_empty_tracks), "edit.clearTracks") { store.removeEmptyEditTracks(); onDismiss() }
            }
            MoreSection(stringResource(R.string.edit_section_placement)) {
                MoreTile(EditIcon.PlaceEnd, stringResource(R.string.edit_place_end), "edit.placement.0", store.editPlacement == 0) { store.editPlacement = 0; onDismiss() }
                MoreTile(EditIcon.PlaceInsert, stringResource(R.string.edit_place_insert), "edit.placement.1", store.editPlacement == 1) { store.editPlacement = 1; onDismiss() }
                MoreTile(EditIcon.PlaceOverwrite, stringResource(R.string.edit_place_overwrite), "edit.placement.2", store.editPlacement == 2) { store.editPlacement = 2; onDismiss() }
                Box(Modifier.weight(1f))
            }
            store.editTimeline?.tracks?.forEach { lane ->
                TrackHead(store, lane) { onRevealTrack(lane.id) }
            }
        }
    }
}

@Composable
private fun MoreSection(title: String, content: @Composable androidx.compose.foundation.layout.RowScope.() -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(title, style = AureaType.of(13f, FontWeight.W600, color = AureaColors.Muted))
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), content = content)
    }
}

@Composable
private fun androidx.compose.foundation.layout.RowScope.MoreTile(icon: EditIcon, label: String, tag: String, on: Boolean = false, onClick: () -> Unit) {
    Column(
        Modifier.weight(1f).height(68.dp).clip(RoundedCornerShape(11.dp)).background(if (on) AureaColors.AccentDim else AureaColors.SurfaceHigh)
            .testTag(tag).semantics(mergeDescendants = true) { role = Role.Button; selected = on }
            .tocavel(shrink = 0.95f, onClick = onClick).padding(horizontal = 4.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        EditToolIcon(icon, if (on) AureaColors.Accent else AureaColors.Text, 24.dp)
        Text(label, style = AureaType.of(11f, FontWeight.W500, color = if (on) AureaColors.Accent else AureaColors.Text), maxLines = 1, modifier = Modifier.padding(top = 4.dp))
    }
}

@Composable
private fun EditLanes(
    store: EditorStore,
    data: EditTimelineData,
    view: EditView,
    tool: EditTool,
    snap: Boolean,
    compact: Boolean,
    fps: Float,
    width: Float,
    height: Float,
    onTransition: (Long, Long) -> Unit,
    onAddMedia: () -> Unit,
    onAddAudio: (() -> Unit)?,
    onAddText: (() -> Unit)?,
) {
    val density = LocalDensity.current
    val haptics = LocalHapticFeedback.current
    val headW = width / 2f
    val rulerH = with(density) { RulerH.toPx() }
    val laneH = with(density) { ((if (compact) CompactLaneH else LaneH).value +
        12f * (fontScale.coerceIn(1f, 2f) - 1f)).dp.toPx() }
    val gap = with(density) { LaneGap.toPx() }
    val edge = with(density) { EdgeW.toPx() }
    val rowsTop = with(density) { RowsTop.toPx() }
    val layout = remember(data.lanes.size, rowsTop, laneH, gap) { EditLaneLayout.compute(data.lanes.size, rowsTop, laneH, gap) }
    val laneClips = remember(store.layers, data) {
        val byTrack = store.layers.groupBy { it.trackId }
        data.lanes.map { lane ->
            val real = byTrack[lane.id].orEmpty().map { LaneClip(it, false) }
            val linked = if (lane.audio) store.layers.filter { data.linkedAudio[it.id] == lane.id }.map { LaneClip(it, true) } else emptyList()
            (real + linked).sortedBy { it.row.startFrame }
        }
    }
    val waves = remember { EditWaveCache() }
    val currentLanes by rememberUpdatedState(laneClips)
    val currentData by rememberUpdatedState(data)
    val currentTool by rememberUpdatedState(tool)
    val currentSnap by rememberUpdatedState(snap)
    val currentLayout by rememberUpdatedState(layout)
    val currentHeight by rememberUpdatedState(height)
    val currentDuration by rememberUpdatedState(store.project.durationFrames)
    val currentTransition by rememberUpdatedState(onTransition)

    fun xOf(frame: Float) = headW + (frame - view.scrollFrames) * view.pxPerFrame
    fun frameAt(x: Float) = view.scrollFrames + (x - headW) / view.pxPerFrame
    fun clampScrollY() { view.scrollY = view.scrollY.coerceIn(0f, EditLaneLayout.maxScroll(currentLayout.bottom, currentHeight)) }
    fun clampScrollX() {
        view.scrollFrames = EditLaneLayout.clampCentered(view.scrollFrames, currentDuration)
    }

    LaunchedEffect(width, store.project.durationFrames, view.pxPerFrame) { clampScrollX() }
    LaunchedEffect(layout, height) { clampScrollY() }

    // Playback and external seeks keep the same frame beneath the fixed playhead.
    LaunchedEffect(view, width) {
        snapshotFlow { store.playhead }.collect { frame ->
            if (view.drag != null || view.scrubbing) return@collect
            view.scrollFrames = frame.toFloat()
            clampScrollX()
        }
    }
    // O clipe escolhido rola para dentro da vista (com painel aberto a área encolhe).
    val primary = store.primary
    LaunchedEffect(primary, layout, height) {
        val id = primary ?: return@LaunchedEffect
        val index = laneClips.indexOfFirst { lane -> lane.any { it.row.id == id && !it.virtual } }
        if (index < 0 || height <= 0f) return@LaunchedEffect
        val top = layout.tops[index] - view.scrollY
        if (top < rulerH) view.scrollY -= rulerH - top
        else if (top + layout.heights[index] > height) view.scrollY += top + layout.heights[index] - height
        clampScrollY()
    }
    LaunchedEffect(view.revealedTrack, layout, height) {
        val index = data.lanes.indexOfFirst { it.id == view.revealedTrack }
        if (index < 0 || height <= 0f) return@LaunchedEffect
        val top = layout.tops[index] - view.scrollY
        if (top < rulerH) view.scrollY -= rulerH - top
        else if (top + layout.heights[index] > height) view.scrollY += top + layout.heights[index] - height
        clampScrollY()
    }

    Canvas(
        Modifier.fillMaxSize().pointerInput(store, width, headW) {
            val slop = viewConfiguration.touchSlop
            val longPressMs = viewConfiguration.longPressTimeoutMillis

            fun hitClip(p: Offset): Triple<Int, LaneClip, Int>? {
                if (p.y < rulerH) return null
                val lane = currentLayout.laneAt(p.y + view.scrollY)
                if (lane < 0) return null
                val frame = frameAt(p.x)
                val edgeFrames = edge / view.pxPerFrame
                var near: Triple<Int, LaneClip, Int>? = null
                for (clip in currentLanes[lane]) {
                    val s = clip.row.startFrame.toFloat()
                    val e = clip.row.endFrame.toFloat()
                    if (frame < s - edgeFrames || frame > e + edgeFrames) continue
                    val inside = frame >= s && frame <= e
                    val zone = when {
                        // Clipe estreito: o miolo continua alcançável.
                        (e - s) * view.pxPerFrame > edge * 3 && frame <= s + edgeFrames -> DRAG_TRIM_IN
                        (e - s) * view.pxPerFrame > edge * 3 && frame >= e - edgeFrames -> DRAG_TRIM_OUT
                        else -> DRAG_MOVE
                    }
                    if (inside) return Triple(lane, clip, zone)
                    near = Triple(lane, clip, if (frame < s) DRAG_TRIM_IN else DRAG_TRIM_OUT)
                }
                return near
            }

            fun neighbourLeft(lane: Int, clip: LayerRow): LayerRow? =
                currentLanes[lane].firstOrNull { !it.virtual && it.row.endFrame == clip.startFrame }?.row

            fun snapTargets(self: LayerRow): IntArray {
                val out = ArrayList<Int>()
                out.add(store.playhead)
                if (currentData.markIn >= 0) out.add(currentData.markIn.toInt())
                if (currentData.markOut >= 0) out.add(currentData.markOut.toInt())
                currentLanes.forEach { lane -> lane.forEach { c -> if (c.row.id != self.id) { out.add(c.row.startFrame); out.add(c.row.endFrame) } } }
                return out.toIntArray()
            }

            fun beginDrag(hit: Triple<Int, LaneClip, Int>, longPress: Boolean): ClipDrag? {
                val (lane, clip, zone) = hit
                val selected = clip.row.id in store.selection
                val active = selected || longPress || currentTool != EditTool.Select
                if (!active) return null
                val kind = when {
                    clip.virtual -> if (zone == DRAG_MOVE) DRAG_AUDIO_LANE else return null
                    currentTool == EditTool.Slip -> DRAG_SLIP
                    currentTool == EditTool.Slide -> DRAG_SLIDE
                    currentTool == EditTool.Razor -> return null
                    zone == DRAG_MOVE && currentTool == EditTool.Roll -> return null
                    else -> zone
                }
                val neighbour = if (currentTool == EditTool.Roll && kind == DRAG_TRIM_IN) neighbourLeft(lane, clip.row) else null
                if (currentTool == EditTool.Roll && kind == DRAG_TRIM_IN && neighbour == null) return null
                if (store.playing) store.pause()
                if (!selected) store.select(clip.row.id)
                return ClipDrag(kind, clip.row, neighbour, if (currentSnap) snapTargets(clip.row) else IntArray(0)).also {
                    it.lane = currentData.lanes[lane].id
                }
            }

            fun updateDrag(drag: ClipDrag, start: Offset, now: Offset) {
                val raw = ((now.x - start.x) / view.pxPerFrame).roundToInt()
                val reach = with(density) { 9.dp.toPx() } / view.pxPerFrame
                var frames = raw
                var snappedAt = -1
                if (drag.targets.isNotEmpty()) when (drag.kind) {
                    DRAG_TRIM_IN -> { val s = snapFrame(drag.clip.startFrame + raw, drag.targets, reach); if (s != drag.clip.startFrame + raw) { frames = s - drag.clip.startFrame; snappedAt = s } }
                    DRAG_TRIM_OUT -> { val s = snapFrame(drag.clip.endFrame + raw, drag.targets, reach); if (s != drag.clip.endFrame + raw) { frames = s - drag.clip.endFrame; snappedAt = s } }
                    DRAG_MOVE, DRAG_SLIDE -> {
                        val a = snapFrame(drag.clip.startFrame + raw, drag.targets, reach)
                        val b = snapFrame(drag.clip.endFrame + raw, drag.targets, reach)
                        if (a != drag.clip.startFrame + raw) { frames = a - drag.clip.startFrame; snappedAt = a }
                        else if (b != drag.clip.endFrame + raw) { frames = b - drag.clip.endFrame; snappedAt = b }
                    }
                }
                if (snappedAt != drag.snapped && snappedAt >= 0) haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                drag.frames = frames
                drag.snapped = snappedAt
                if (drag.kind == DRAG_MOVE || drag.kind == DRAG_AUDIO_LANE) {
                    val lane = currentLayout.laneAt(now.y + view.scrollY)
                    if (lane >= 0 && now.y >= rulerH) {
                        val info = currentData.lanes[lane]
                        if (info.audio == (drag.kind == DRAG_AUDIO_LANE || drag.clip.kind == 3)) drag.lane = info.id
                    }
                }
            }

            fun commitDrag(drag: ClipDrag) {
                val clip = drag.clip
                val ripple = currentTool == EditTool.Ripple
                when (drag.kind) {
                    DRAG_MOVE -> if (drag.frames != 0 || drag.lane != clip.trackId)
                        store.editTrackClip(if (ripple) TrackOp.MOVE_INSERT else TrackOp.MOVE, clip.id, (clip.startFrame + drag.frames).coerceAtLeast(0).toLong(), drag.lane)
                    DRAG_TRIM_IN -> if (drag.frames != 0) when {
                        drag.neighbour != null -> store.editTrackClip(TrackOp.ROLL, drag.neighbour.id, (clip.startFrame + drag.frames).toLong(), 0)
                        else -> store.editTrackClip(if (ripple) TrackOp.RIPPLE_TRIM_IN else TrackOp.TRIM_IN, clip.id, (clip.startFrame + drag.frames).toLong(), 0)
                    }
                    DRAG_TRIM_OUT -> if (drag.frames != 0) store.editTrackClip(
                        when { currentTool == EditTool.Roll -> TrackOp.ROLL; ripple -> TrackOp.RIPPLE_TRIM_OUT; else -> TrackOp.TRIM_OUT },
                        clip.id, (clip.endFrame + drag.frames).toLong(), 0,
                    )
                    DRAG_SLIP -> if (drag.frames != 0) store.editTrackClip(TrackOp.SLIP, clip.id, drag.frames.toLong(), 0)
                    DRAG_SLIDE -> if (drag.frames != 0) store.editTrackClip(TrackOp.SLIDE, clip.id, drag.frames.toLong(), 0)
                    DRAG_AUDIO_LANE -> if (drag.lane != currentData.linkedAudio[clip.id]) store.editTrackClip(TrackOp.MOVE_LINKED_AUDIO, clip.id, 0, drag.lane)
                }
            }

            fun tap(p: Offset) {
                val hit = hitClip(p)
                if (currentTool == EditTool.Razor) {
                    if (hit != null && !hit.second.virtual) {
                        var frame = frameAt(p.x).roundToInt()
                        // Cair em cima do cabeçote é quase sempre o que se queria.
                        if (abs(frame - store.playhead) * view.pxPerFrame < edge) frame = store.playhead
                        store.editTrackClip(TrackOp.RAZOR, hit.second.row.id, frame.toLong(), 0)
                    }
                    return
                }
                if (hit == null) { store.clearSelection(); return }
                val (lane, clip, _) = hit
                // Um corte encostado de clipes não escolhidos abre as transições dele.
                if (!clip.virtual && clip.row.id !in store.selection && !currentData.lanes[lane].audio) {
                    val atCut = { f: Int -> abs(frameAt(p.x) - f) * view.pxPerFrame < edge * 0.75f }
                    if (atCut(clip.row.startFrame)) neighbourLeft(lane, clip.row)?.let { currentTransition(it.id, clip.row.id); return }
                    if (atCut(clip.row.endFrame)) currentLanes[lane].firstOrNull { !it.virtual && it.row.startFrame == clip.row.endFrame }?.let { currentTransition(clip.row.id, it.row.id); return }
                }
                store.select(clip.row.id)
            }

            awaitEachGesture {
                val down = awaitFirstDown(requireUnconsumed = false)
                val start = down.position
                if (start.y < rulerH) {
                    store.pause()
                    view.scrubbing = true
                    view.scrollFrames = frameAt(start.x)
                    clampScrollX()
                    store.scrubStart(view.scrollFrames.roundToInt())
                    var lastX = start.x
                    try {
                        while (true) {
                            val event = awaitPointerEvent()
                            val change = event.changes.firstOrNull { it.id == down.id } ?: break
                            if (!change.pressed) break
                            view.scrollFrames -= (change.position.x - lastX) / view.pxPerFrame
                            lastX = change.position.x
                            clampScrollX()
                            store.scrubTo(view.scrollFrames.roundToInt())
                            change.consume()
                        }
                    } finally {
                        store.scrubEnd()
                        view.scrubbing = false
                    }
                    return@awaitEachGesture
                }
                val hit = hitClip(start)
                // 0 toque, 1 passou da folga, 2 dois dedos.
                val phase = withTimeoutOrNull(longPressMs) {
                    var result = 0
                    while (true) {
                        val event = awaitPointerEvent()
                        if (event.changes.count { it.pressed } >= 2) { result = 2; break }
                        val change = event.changes.firstOrNull { it.id == down.id }
                        if (change == null || !change.pressed) { result = 0; break }
                        if ((change.position - start).getDistance() > slop) { result = 1; break }
                    }
                    result
                }
                if (phase == 0) { tap(start); return@awaitEachGesture }
                var drag: ClipDrag? = null
                if (phase == null && hit != null) {
                    drag = beginDrag(hit, longPress = true)
                    if (drag != null) haptics.performHapticFeedback(HapticFeedbackType.LongPress)
                } else if (phase == 1 && hit != null) {
                    drag = beginDrag(hit, longPress = false)
                }
                view.drag = drag
                if (drag == null) {
                    view.scrubbing = true
                    store.scrubStart(view.scrollFrames.roundToInt())
                }
                var last = start
                var pinching = phase == 2
                try {
                    while (true) {
                        val event = awaitPointerEvent()
                        val pressed = event.changes.count { it.pressed }
                        if (pressed == 0) break
                        if (pressed >= 2 && view.drag == null) pinching = true
                        val change = event.changes.firstOrNull { it.id == down.id }
                        if (pinching) {
                            if (pressed >= 2) {
                                val zoom = event.calculateZoom()
                                val focus = event.calculateCentroid(useCurrent = true)
                                if (zoom != 1f && focus != Offset.Unspecified) {
                                    val anchor = frameAt(focus.x)
                                    view.pxPerFrame = (view.pxPerFrame * zoom).coerceIn(with(density) { 0.15.dp.toPx() }, with(density) { 40.dp.toPx() })
                                    view.scrollFrames = anchor - (focus.x - headW) / view.pxPerFrame
                                }
                                val pan = event.calculatePan()
                                view.scrollFrames -= pan.x / view.pxPerFrame
                                clampScrollX()
                                 store.scrubTo(view.scrollFrames.roundToInt())
                            }
                            event.changes.forEach { if (it.positionChanged()) it.consume() }
                            continue
                        }
                        if (change == null) break
                        val current = view.drag
                        if (current != null) updateDrag(current, start, change.position)
                        else {
                            view.scrollFrames -= (change.position.x - last.x) / view.pxPerFrame
                            clampScrollX()
                             store.scrubTo(view.scrollFrames.roundToInt())
                            view.scrollY -= change.position.y - last.y
                            clampScrollY()
                        }
                        last = change.position
                        change.consume()
                    }
                    view.drag?.let { commitDrag(it) }
                } finally {
                     if (view.scrubbing) store.scrubEnd()
                     view.scrubbing = false
                    view.drag = null
                }
            }
        },
    ) {
        drawLanes(store, data, layout, laneClips, view, waves, headW, rulerH, fps, density.density)
    }

    data.lanes.forEachIndexed { index, lane ->
        Box(Modifier.fillMaxWidth().height(with(density) { layout.heights[index].toDp() })
            .offset { IntOffset(0, (layout.tops[index] - view.scrollY).roundToInt()) }
            .testTag("edit.lane.${lane.id}").semantics { contentDescription = lane.label })
    }
    laneClips.forEachIndexed { i, lane ->
        lane.forEach { clip ->
            if (!clip.virtual) ClipTarget(store, clip.row, view, layout.tops[i], layout.heights[i], headW, rulerH, width)
        }
    }
    val main = data.lanes.indexOfFirst { it.id == data.mainTrackId }.coerceAtLeast(0)
    val mainClips = laneClips.getOrNull(main).orEmpty().filter { !it.virtual }
    val audioClips = remember(store.layers, store.curveRevision, data.mainTrackId) {
        mainClips.mapNotNull { clip -> store.queryDetail(clip.row.id)?.takeIf { it.hasAudio }?.let { clip.row.id to it.audioMuted } }
    }
    val muted = audioClips.isNotEmpty() && audioClips.all { it.second }
    val startLabel = stringResource(R.string.panel_inicio)
    val startDescription = stringResource(R.string.edit_mobile_sequence_start)
    val muteLabel = stringResource(if (muted) R.string.edit_mobile_main_audio_unmute else R.string.edit_mobile_main_audio_mute)
    val prefixWidth = if (audioClips.isEmpty()) 58.dp else 98.dp
    Row(Modifier.offset { IntOffset((xOf(0f) - with(density) { prefixWidth.toPx() }).roundToInt(),
        ((layout.tops.getOrNull(main) ?: rulerH) - view.scrollY).roundToInt()) }
        .height(with(density) { laneH.toDp() }), verticalAlignment = Alignment.CenterVertically) {
        if (audioClips.isNotEmpty()) {
            Box(Modifier.width(32.dp).fillMaxSize().testTag("edit.mainAudio")
                .semantics { contentDescription = muteLabel; selected = muted; role = Role.Switch }
                .tocavel { store.setLayersAudioMuted(audioClips.map { it.first }, !muted) }, contentAlignment = Alignment.Center) {
                CupertinoIcon(if (muted) CupertinoGlyph.SpeakerSlash else CupertinoGlyph.Speaker2, 22.dp, AureaColors.Text)
            }
            Box(Modifier.width(8.dp))
        }
        Column(Modifier.width(50.dp).fillMaxHeight().testTag("edit.sequenceStart")
            .semantics { contentDescription = startDescription; role = Role.Button }
            .tocavel { store.pause(); view.scrollFrames = 0f; store.seek(0) },
            horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
            CupertinoIcon(CupertinoGlyph.BackwardEnd, 18.dp, AureaColors.Text)
            Text(startLabel, style = AureaType.of(10f, color = AureaColors.Text), maxLines = 1)
        }
    }
    val addLabel = stringResource(R.string.edit_add_media)
    Box(Modifier.offset { IntOffset((width - with(density) { 48.dp.toPx() }).roundToInt(),
        (rowsTop + (laneH - with(density) { 44.dp.toPx() }) / 2f).roundToInt()) }
        .size(44.dp).testTag("edit.add")
        .semantics { contentDescription = addLabel; role = Role.Button }.tocavel(haptic = true, onClick = onAddMedia),
        contentAlignment = Alignment.Center) {
        Box(Modifier.size(36.dp).clip(RoundedCornerShape(12.dp)).background(AureaColors.Accent), contentAlignment = Alignment.Center) {
            CupertinoIcon(CupertinoGlyph.Plus, 20.dp, AureaColors.OnAction)
        }
    }
    if (!compact) {
        val hasAudio = data.linkedAudio.isNotEmpty() || store.layers.any { it.kind == 3 }
        val hasText = store.layers.any { it.kind == 4 }
        val actions = buildList {
            if (!hasAudio && onAddAudio != null) add(Triple("audio", R.string.edit_dock_audio, onAddAudio))
            if (!hasText && onAddText != null) add(Triple("text", R.string.edit_text_add, onAddText))
        }
        actions.forEachIndexed { index, (id, labelId, action) ->
            val label = stringResource(labelId)
            Row(Modifier.offset { IntOffset(max(8f, xOf(0f)).roundToInt(),
                (layout.bottom - view.scrollY + with(density) { (index * 44).dp.toPx() }).roundToInt()) }
                .height(44.dp).widthIn(min = 130.dp).testTag("edit.inline.$id")
                .semantics { contentDescription = label; role = Role.Button }.tocavel(onClick = action)
                .padding(horizontal = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                CupertinoIcon(CupertinoGlyph.Plus, 14.dp, AureaColors.Muted)
                Text(label, style = AureaType.of(12f, color = AureaColors.Muted), modifier = Modifier.padding(start = 8.dp), maxLines = 1)
            }
        }
    }
}

@Composable
private fun TrackHead(store: EditorStore, lane: EditTrackInfo, onTarget: () -> Unit) {
    val targetLabel = stringResource(R.string.edit_track_target, lane.label)
    val lockLabel = stringResource(R.string.edit_track_lock)
    val muteLabel = stringResource(if (lane.audio) R.string.edit_track_mute else R.string.edit_track_visible)
    val soloLabel = stringResource(R.string.edit_track_solo)
    Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(
            Modifier.width(64.dp).fillMaxHeight().testTag("edit.track.${lane.label}.target")
                .semantics { contentDescription = targetLabel; role = Role.Switch; selected = lane.target }
                .tocavel { store.setEditTrackFlag(lane.id, 3, !lane.target); onTarget() },
            contentAlignment = Alignment.Center,
        ) {
            Text(lane.label, style = AureaType.of(12f, FontWeight.W600,
                color = if (lane.target) AureaColors.Accent else AureaColors.Muted))
        }
        Box(Modifier.size(48.dp).testTag("edit.track.${lane.label}.lock")
            .semantics { contentDescription = lockLabel; role = Role.Switch; selected = lane.locked }
            .tocavel { store.setEditTrackFlag(lane.id, 0, !lane.locked) }, contentAlignment = Alignment.Center) {
            CupertinoIcon(if (lane.locked) CupertinoGlyph.LockFill else ShellGlyph.LockOpenFill, 20.dp, AureaColors.Text)
        }
        Box(Modifier.size(48.dp).testTag("edit.track.${lane.label}.${if (lane.audio) "mute" else "visible"}")
            .semantics { contentDescription = muteLabel; role = Role.Switch; selected = lane.muted }
            .tocavel { store.setEditTrackFlag(lane.id, 1, !lane.muted) }, contentAlignment = Alignment.Center) {
            CupertinoIcon(if (lane.audio) { if (lane.muted) CupertinoGlyph.SpeakerSlash else CupertinoGlyph.Speaker2 }
                else if (lane.muted) CupertinoGlyph.EyeSlash else CupertinoGlyph.Eye, 20.dp, AureaColors.Text)
        }
        if (lane.audio) Box(Modifier.size(48.dp).testTag("edit.track.${lane.label}.solo")
            .semantics { contentDescription = soloLabel; role = Role.Switch; selected = lane.solo }
            .tocavel { store.setEditTrackFlag(lane.id, 2, !lane.solo) }, contentAlignment = Alignment.Center) {
            Text("S", style = AureaType.of(14f, FontWeight.W700, color = if (lane.solo) AureaColors.Accent else AureaColors.Text))
        }
    }
}

@Composable
private fun ClipTarget(store: EditorStore, clip: LayerRow, view: EditView, top: Float, heightPx: Float, headW: Float, rulerH: Float, viewportWidth: Float) {
    val selected = clip.id in store.selection
    val description = if (selected) stringResource(R.string.edit_clip_selected, clip.name) else clip.name
    Box(
        Modifier
            .offset { IntOffset(max(0f, headW + (clip.startFrame - view.scrollFrames) * view.pxPerFrame).roundToInt(),
                (top - view.scrollY).roundToInt().coerceAtLeast(rulerH.roundToInt())) }
            .layout { measurable, _ ->
                val left = max(0f, headW + (clip.startFrame - view.scrollFrames) * view.pxPerFrame)
                val right = min(viewportWidth, headW + (clip.endFrame - view.scrollFrames) * view.pxPerFrame)
                val visibleWidth = max(0f, right - left).roundToInt()
                val placeable = measurable.measure(Constraints.fixed(visibleWidth, heightPx.roundToInt()))
                layout(placeable.width, placeable.height) { placeable.place(0, 0) }
            }
            .testTag("edit.clip.${clip.id}")
            .semantics { contentDescription = description; this.selected = selected; role = Role.Button },
    )
}

private val TextPaint by lazy(LazyThreadSafetyMode.NONE) { android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG) }

private fun DrawScope.label(text: String, x: Float, y: Float, sizePx: Float, color: Color, maxW: Float = Float.MAX_VALUE) {
    TextPaint.textSize = sizePx
    TextPaint.color = color.toArgb()
    val shown = if (maxW == Float.MAX_VALUE) text else {
        val n = TextPaint.breakText(text, true, maxW, null)
        if (n <= 0) return
        text.substring(0, n)
    }
    platformCanvas.drawText(shown, x, y, TextPaint)
}

private fun rulerStepFrames(pxPerFrame: Float, fps: Float, minGap: Float): Int {
    val seconds = intArrayOf(1, 2, 5, 10, 15, 30, 60, 120, 300, 600).firstOrNull { it * fps * pxPerFrame >= minGap } ?: 1200
    return (seconds * fps).roundToInt().coerceAtLeast(1)
}

private fun DrawScope.drawLanes(
    store: EditorStore,
    data: EditTimelineData,
    layout: EditLaneLayout,
    lanes: List<List<LaneClip>>,
    view: EditView,
    waves: EditWaveCache,
    headW: Float,
    rulerH: Float,
    fps: Float,
    dp: Float,
) {
    val pxPerFrame = view.pxPerFrame
    val scroll = view.scrollFrames
    val scrollY = view.scrollY
    val drag = view.drag
    val width = size.width
    val generation = store.thumbnailGeneration
    val selection = store.selection
    fun xOf(frame: Float) = headW + (frame - scroll) * pxPerFrame

    clipRect(left = 0f, top = rulerH, right = width, bottom = size.height) {
        data.lanes.forEachIndexed { i, _ ->
            val top = layout.tops[i] - scrollY
            drawRect(EditPalette.Lane, Offset(0f, top), Size(width, layout.heights[i]))
        }
        lanes.forEachIndexed { i, clips ->
            val top = layout.tops[i] - scrollY
            val laneH = layout.heights[i]
            if (top + laneH < rulerH || top > size.height) return@forEachIndexed
            clips.forEach { clip ->
                // Enquanto arrasta, o clipe levado se desenha no fim, por cima.
                if (drag != null && drag.clip.id == clip.row.id && !clip.virtual && drag.kind == DRAG_MOVE) return@forEach
                drawClip(store, clip, top, laneH, drag, data.lanes[i], selection, xOf(0f), pxPerFrame, waves, generation, 0f, width, dp)
            }
            // A marca de cada corte encostado.
            if (!data.lanes[i].audio) clips.zipWithNext().forEach { (a, b) ->
                if (a.row.endFrame == b.row.startFrame) {
                    val cx = xOf(a.row.endFrame.toFloat())
                    if (cx > 0f && cx < width) {
                        val mid = top + laneH / 2f
                        val s = 3.5f * dp
                        val bow = Path().apply { moveTo(cx - s, mid - s); lineTo(cx + s, mid + s); lineTo(cx + s, mid - s); lineTo(cx - s, mid + s); close() }
                        drawPath(bow, EditPalette.Cut)
                    }
                }
            }
        }
        if (drag != null && drag.kind == DRAG_MOVE) {
            val index = data.lanes.indexOfFirst { it.id == drag.lane }
            if (index >= 0) drawClip(store, LaneClip(drag.clip, false), layout.tops[index] - scrollY, layout.heights[index], drag, data.lanes[index], selection, xOf(0f), pxPerFrame, waves, generation, 0f, width, dp)
        }
        if (drag != null && drag.snapped >= 0) {
            val x = xOf(drag.snapped.toFloat())
            drawLine(EditPalette.Snap, Offset(x, rulerH), Offset(x, size.height), strokeWidth = 1.5f * dp)
        }
        if (data.markIn >= 0 || data.markOut >= 0) {
            val from = xOf((if (data.markIn >= 0) data.markIn else 0L).toFloat()).coerceAtLeast(0f)
            val to = if (data.markOut >= 0) xOf(data.markOut.toFloat()).coerceAtMost(width) else width
            if (to > from) drawRect(EditPalette.Range, Offset(from, rulerH), Size(to - from, size.height - rulerH))
        }
    }

    // Régua por cima das faixas.
    drawRect(EditPalette.RulerBg, Offset.Zero, Size(width, rulerH))
    val step = rulerStepFrames(pxPerFrame, fps, 64f * dp)
    var tick = floor((scroll - headW / pxPerFrame) / step).toInt().coerceAtLeast(0) * step
    val lastFrame = scroll + (width - headW) / pxPerFrame
    while (tick <= lastFrame) {
        val x = xOf(tick.toFloat())
        if (x >= 0f) {
            drawLine(EditPalette.Tick, Offset(x, rulerH - 8f * dp), Offset(x, rulerH), strokeWidth = 1f * dp)
            val seconds = (tick / fps).roundToInt()
            label("%02d:%02d".format(java.util.Locale.ROOT, seconds / 60, seconds % 60), x + 3f * dp, 12f * dp, 10f * dp, EditColors.Ruler)
        }
        val half = xOf(tick + step / 2f)
        if (half >= 0f) drawLine(EditPalette.Tick, Offset(half, rulerH - 4f * dp), Offset(half, rulerH), strokeWidth = 1f * dp)
        tick += step
    }
    if (data.markIn >= 0) drawMark(xOf(data.markIn.toFloat()), rulerH, 0f, dp, true)
    if (data.markOut >= 0) drawMark(xOf(data.markOut.toFloat()), rulerH, 0f, dp, false)

    // Cabeçote: lido aqui, na fase de desenho.
    drawLine(EditPalette.Playhead, Offset(headW, 0f), Offset(headW, size.height), strokeWidth = 1.5f * dp)
    val marker = 4f * dp
    drawPath(Path().apply {
        moveTo(headW, 0f)
        lineTo(headW + marker, marker)
        lineTo(headW, marker * 2f)
        lineTo(headW - marker, marker)
        close()
    }, EditPalette.Playhead)
}

private fun DrawScope.drawMark(x: Float, rulerH: Float, headW: Float, dp: Float, entrance: Boolean) {
    if (x < headW) return
    val w = 5f * dp * if (entrance) 1f else -1f
    val path = Path().apply { moveTo(x, rulerH - 12f * dp); lineTo(x + w, rulerH - 12f * dp); lineTo(x, rulerH - 7f * dp); lineTo(x, rulerH); close() }
    drawPath(path, EditPalette.Snap)
}

private fun DrawScope.drawClip(
    store: EditorStore,
    clip: LaneClip,
    top: Float,
    laneH: Float,
    drag: ClipDrag?,
    lane: EditTrackInfo,
    selection: Set<Long>,
    originX: Float,
    pxPerFrame: Float,
    waves: EditWaveCache,
    generation: Int,
    headW: Float,
    width: Float,
    dp: Float,
) {
    val row = clip.row
    var startFrame = row.startFrame
    var endFrame = row.endFrame
    if (drag != null) {
        if (drag.clip.id == row.id) when (drag.kind) {
            DRAG_MOVE, DRAG_SLIDE -> { startFrame += drag.frames; endFrame += drag.frames }
            DRAG_TRIM_IN -> startFrame = min(startFrame + drag.frames, endFrame - 1)
            DRAG_TRIM_OUT -> endFrame = max(endFrame + drag.frames, startFrame + 1)
        } else if (drag.neighbour?.id == row.id && drag.kind == DRAG_TRIM_IN) {
            endFrame += drag.frames
        }
    }
    val left = originX + startFrame * pxPerFrame
    val right = originX + endFrame * pxPerFrame
    if (right < headW || left > width) return
    val selected = row.id in selection
    val kind = row.kind
    val adjustment = row.flags and (1 shl 6) != 0
    val audioKind = kind == 3
    val body = when {
        clip.virtual -> EditPalette.LinkedAudio
        audioKind -> EditPalette.Audio
        adjustment -> EditPalette.Adjustment
        kind == 1 || kind == 2 || kind == 12 -> EditPalette.Video
        else -> EditPalette.Overlay
    }
    val pad = 1f * dp
    val w = max(right - left, 1f)
    val alpha = if (lane.muted || lane.locked) 0.45f else 1f
    drawRoundRect(body.copy(alpha = alpha), Offset(left, top + pad), Size(w, laneH - 2 * pad), CornerRadius(2f * dp))
    val visibleLeft = max(left, headW)
    val visibleRight = min(right, width)
    val visibleW = visibleRight - visibleLeft
    if (audioKind || clip.virtual) {
        drawWave(store, row, waves, generation, top + pad, laneH - 2 * pad, originX, pxPerFrame, visibleLeft, visibleRight, dp,
            if (clip.virtual) EditPalette.LinkedWave else EditPalette.AudioWave)
    } else {
        val stripH = min(13f * dp, laneH * 0.34f)
        val textX = visibleLeft + 3f * dp
        if (kind == 1 || kind == 2 || kind == 12) {
            val heightPx = (laneH - 2 * pad).roundToInt().coerceIn(16, 192)
            val tileWidth = max(32f * dp, laneH)
            var tile = floor((visibleLeft - left) / tileWidth).toInt().coerceAtLeast(0)
            clipRect(visibleLeft, top + pad, visibleRight, top + laneH - pad) {
                while (left + tile * tileWidth < visibleRight) {
                    val tileX = left + tile * tileWidth
                    val frame = (row.startFrame + tile * tileWidth / pxPerFrame).roundToInt().coerceIn(row.startFrame, max(row.startFrame, row.endFrame - 1))
                    val thumb = waves.thumbnail(store, row.id, frame, heightPx, generation)
                    if (thumb != null) {
                        val image = thumb.asImageBitmap()
                        val sourceWidth = min(image.width, (image.height * tileWidth / heightPx).roundToInt()).coerceAtLeast(1)
                        val sourceHeight = min(image.height, (image.width * heightPx / tileWidth).roundToInt()).coerceAtLeast(1)
                        drawImage(image, IntOffset((image.width - sourceWidth) / 2, (image.height - sourceHeight) / 2),
                            IntSize(sourceWidth, sourceHeight), IntOffset(tileX.roundToInt(), (top + pad).roundToInt()),
                            IntSize(tileWidth.roundToInt(), heightPx), alpha = alpha)
                    }
                    tile++
                }
            }
        }
        if (visibleW > 16f * dp && (selected || (kind != 1 && kind != 2 && kind != 12))) {
            drawRect(Color.Black.copy(alpha = .55f), Offset(visibleLeft, top + pad), Size(visibleW, stripH))
            label(row.name, textX, top + pad + stripH - 3f * dp, 9.5f * dp, Color.White, visibleW - 6f * dp - if (row.effectCount > 0) 16f * dp else 0f)
        }
        if (row.effectCount > 0 && visibleW > 26f * dp) label("fx", visibleRight - 14f * dp, top + pad + stripH - 3f * dp, 9.5f * dp, Color.White)
    }
    if (selected) drawRoundRect(EditPalette.Selected, Offset(left, top + pad), Size(w, laneH - 2 * pad), CornerRadius(2f * dp), style = Stroke(width = 1.5f * dp))
    if (selected && !clip.virtual && w > 26f * dp) {
        // As bordas ativas do clipe escolhido.
        val tick = Size(3f * dp, laneH * 0.5f)
        val y = top + laneH * 0.25f
        drawRoundRect(EditPalette.Selected, Offset(left + 1f * dp, y), tick, CornerRadius(1.5f * dp))
        drawRoundRect(EditPalette.Selected, Offset(right - 4f * dp, y), tick, CornerRadius(1.5f * dp))
    }
}

private fun DrawScope.drawWave(
    store: EditorStore,
    row: LayerRow,
    waves: EditWaveCache,
    generation: Int,
    top: Float,
    height: Float,
    originX: Float,
    pxPerFrame: Float,
    visibleLeft: Float,
    visibleRight: Float,
    dp: Float,
    color: Color,
) {
    if (visibleRight - visibleLeft < 1f || pxPerFrame <= 0f) return
    val fpb = WaveGrid.framesPerBucket(1.5f * dp, pxPerFrame)
    val fromFrame = ((visibleLeft - originX) / pxPerFrame).toDouble()
    val toFrame = ((visibleRight - originX) / pxPerFrame).toDouble()
    val first = WaveGrid.bucketAt(max(fromFrame, row.startFrame.toDouble()), fpb)
    val last = WaveGrid.bucketAt(min(toFrame, row.endFrame.toDouble()) - 1e-6, fpb)
    if (last < first) return
    val wave = waves.get(store, row, fpb, generation, first, last) ?: return
    val stroke = max(1f, (fpb * pxPerFrame).toFloat() * 0.72f)
    val mid = top + height / 2f
    var b = first
    while (b <= last) {
        val v = wave.at(b) / 255f
        val x = (originX + (b + 0.5) * fpb * pxPerFrame).toFloat()
        b++
        if (v <= 0f) continue
        val half = v * height / 2f * 0.92f
        drawLine(color, Offset(x, mid - half), Offset(x, mid + half), strokeWidth = stroke)
    }
}

/**
 * Janelas da onda por clipe. O motor só é consultado quando a vista sai da
 * janela guardada, o degrau de zoom muda, o clipe muda de lugar ou chega
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

    // 0.5.3: miniatura que o motor ainda não tinha → geração em que foi pedida. Sem
    // isto, cada clipe visível ainda sem miniatura fazia uma consulta bloqueante ao
    // motor (no lock do modelo que o render segura) a CADA desenho da timeline,
    // inclusive a cada quadro tocando. Só repergunta quando a geração de miniaturas
    // muda, como a timeline principal (ThumbStrip).
    private data class ThumbKey(val layer: Long, val frame: Int, val height: Int)
    private val thumbMisses = HashMap<ThumbKey, Int>()

    fun thumbnail(store: EditorStore, layer: Long, frame: Int, heightPx: Int, generation: Int) =
        ThumbKey(layer, frame, heightPx).let { key ->
            if (thumbMisses[key] == generation) null
            else store.thumbnails.get(layer, frame, heightPx).also { bmp ->
                if (bmp == null) {
                    if (thumbMisses.size > MAX_THUMB_MISSES) thumbMisses.clear()
                    thumbMisses[key] = generation
                } else {
                    thumbMisses.remove(key)
                }
            }
        }

    private companion object { const val MAX = 4096; const val MAX_THUMB_MISSES = 512 }
}
