package com.aurea.aurea.editor

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
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
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.positionChanged
import androidx.compose.ui.input.pointer.pointerInput
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

// =============================================================================
//  A LINHA DO TEMPO DA EDIÇÃO: faixas de montagem (V3, V2, V1 em cima; A1, A2,
//  A3 embaixo), no fluxo de um editor não linear.
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

private val ToolH = 44.dp
private val RulerH = 26.dp
private val HeadW = 98.dp
private val LaneH = 44.dp
private val CompactLaneH = 34.dp
private val LaneGap = 1.dp
private val EdgeW = 13.dp

private object EditPalette {
    val Lane = Color(0xFF17191E)
    val Head = Color(0xFF20232A)
    val RulerBg = Color(0xFF101114)
    val Tick = Color(0xFF5B606B)
    val Separator = Color(0xFF3A3E47)
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
    val Playhead = Color(0xFF4EA8F2)
    val Selected = Color.White
    val Range = Color(0x334EA8F2)
    val Snap = Color(0xFFFFD54A)
    val Cut = Color(0xFFE8EDF5)
}

/** Faixa como a interface mostra: `label` é V1, A2… */
internal class EditTrackInfo(val id: Int, val audio: Boolean, val locked: Boolean, val muted: Boolean, val solo: Boolean, val target: Boolean, val label: String)

/** O que `Engine::query_edit_timeline` devolve, em fileiras prontas para desenhar. */
internal class EditTimelineData(val tracks: List<EditTrackInfo>, val linkedAudio: Map<Long, Int>, val markIn: Long, val markOut: Long) {
    /** De cima para baixo na tela: V3, V2, V1, A1, A2, A3. */
    val lanes: List<EditTrackInfo> = tracks.filter { !it.audio }.reversed() + tracks.filter { it.audio }

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
    var scrollFrames by mutableFloatStateOf(-8f)
    var scrollY by mutableFloatStateOf(0f)
    var drag by mutableStateOf<ClipDrag?>(null)
}

@Composable
internal fun EditTimeline(
    store: EditorStore,
    modifier: Modifier,
    compact: Boolean,
    onAddMedia: () -> Unit,
    onTransition: (Long, Long) -> Unit = { _, _ -> },
) {
    val density = LocalDensity.current
    val fps = store.project.fps.takeIf { it > 0f } ?: 30f
    val view = remember(store) { EditView(with(density) { 72.dp.toPx() } / fps) }
    var tool by remember { mutableStateOf(EditTool.Select) }
    var snap by remember { mutableStateOf(true) }
    val timelineLabel = stringResource(R.string.edit_timeline)
    val data = store.editTimeline

    Column(modifier.background(EditColors.Canvas).testTag("edit.timeline").semantics { contentDescription = timelineLabel }) {
        EditToolStrip(store, tool, { tool = it }, snap, { snap = it }, onAddMedia)
        BoxWithConstraints(Modifier.fillMaxWidth().weight(1f).clipToBounds()) {
            if (data != null) EditLanes(store, data, view, tool, snap, compact, fps, constraints.maxWidth.toFloat(), constraints.maxHeight.toFloat(), onTransition)
        }
    }
}

/** A linha fixa: mídia, as seis ferramentas num grupo só, encaixe e o resto no "⋯". */
@Composable
private fun EditToolStrip(
    store: EditorStore,
    tool: EditTool,
    onTool: (EditTool) -> Unit,
    snap: Boolean,
    onSnap: (Boolean) -> Unit,
    onAddMedia: () -> Unit,
) {
    var more by remember { mutableStateOf(false) }
    Row(
        Modifier.fillMaxWidth().height(ToolH).background(EditColors.Bar).padding(horizontal = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        val addLabel = stringResource(R.string.edit_add_media)
        Box(
            Modifier.size(34.dp).clip(RoundedCornerShape(9.dp)).background(if (store.editPlacement != 0) AureaColors.Accent else Color.White).testTag("edit.add")
                .semantics { contentDescription = addLabel; role = Role.Button }
                .tocavel(shrink = 0.9f, haptic = true, onClick = onAddMedia),
            contentAlignment = Alignment.Center,
        ) { CupertinoIcon(CupertinoGlyph.Plus, 18.dp, Color.Black) }
        Row(
            Modifier.padding(start = 8.dp).clip(RoundedCornerShape(10.dp)).background(EditColors.DockLevel).padding(2.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            EditTool.values().forEach { t ->
                ToolButton(t.icon, stringResource(t.label), "edit.tool.${t.tag}", t == tool) { onTool(t) }
            }
        }
        Box(Modifier.weight(1f))
        ToolButton(EditIcon.Snap, stringResource(R.string.edit_snap), "edit.snap", snap) { onSnap(!snap) }
        val moreLabel = stringResource(R.string.edit_more)
        Box(
            Modifier.size(36.dp).testTag("edit.tools.more").semantics { contentDescription = moreLabel; role = Role.Button }
                .tocavel(shrink = 0.92f) { more = true },
            contentAlignment = Alignment.Center,
        ) { CupertinoIcon(CupertinoGlyph.Ellipsis, 20.dp, AureaColors.Text) }
    }
    if (more) EditMoreSheet(store) { more = false }
}

@Composable
private fun ToolButton(icon: EditIcon, label: String, tag: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.size(36.dp, 34.dp).clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else Color.Transparent)
            .testTag(tag).semantics { contentDescription = label; role = Role.Button; selected = on }
            .tocavel(shrink = 0.92f, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { EditToolIcon(icon, if (on) AureaColors.Accent else AureaColors.Text) }
}

/** O que não é ferramenta de arrasto, em três grupos. */
@Composable
private fun EditMoreSheet(store: EditorStore, onDismiss: () -> Unit) {
    com.aurea.aurea.ui.ds.AureaModalSheet(onDismiss = onDismiss) {
        Column(Modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, bottom = 24.dp), verticalArrangement = Arrangement.spacedBy(14.dp)) {
            MoreSection(stringResource(R.string.edit_section_marks)) {
                MoreTile(EditIcon.MarkIn, stringResource(R.string.edit_mark_in), "edit.markIn") { store.toggleEditMark(0); onDismiss() }
                MoreTile(EditIcon.MarkOut, stringResource(R.string.edit_mark_out), "edit.markOut") { store.toggleEditMark(1); onDismiss() }
                MoreTile(EditIcon.Lift, stringResource(R.string.edit_lift), "edit.lift") { store.editMarkedRange(false); onDismiss() }
                MoreTile(EditIcon.Extract, stringResource(R.string.edit_extract), "edit.extract") { store.editMarkedRange(true); onDismiss() }
            }
            MoreSection(stringResource(R.string.edit_section_tracks)) {
                MoreTile(EditIcon.RazorAll, stringResource(R.string.edit_razor_all), "edit.razorAll") { store.razorAllAtPlayhead(); onDismiss() }
                MoreTile(EditIcon.AddVideo, stringResource(R.string.edit_add_video_track), "edit.addVideoTrack") { store.addEditTrack(false); onDismiss() }
                MoreTile(EditIcon.AddAudio, stringResource(R.string.edit_add_audio_track), "edit.addAudio") { store.addEditTrack(true); onDismiss() }
                MoreTile(EditIcon.ClearTracks, stringResource(R.string.edit_remove_empty_tracks), "edit.clearTracks") { store.removeEmptyEditTracks(); onDismiss() }
            }
            MoreSection(stringResource(R.string.edit_section_placement)) {
                MoreTile(EditIcon.PlaceEnd, stringResource(R.string.edit_place_end), "edit.placement.0", store.editPlacement == 0) { store.editPlacement = 0; onDismiss() }
                MoreTile(EditIcon.PlaceInsert, stringResource(R.string.edit_place_insert), "edit.placement.1", store.editPlacement == 1) { store.editPlacement = 1; onDismiss() }
                MoreTile(EditIcon.PlaceOverwrite, stringResource(R.string.edit_place_overwrite), "edit.placement.2", store.editPlacement == 2) { store.editPlacement = 2; onDismiss() }
                Box(Modifier.weight(1f))
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
) {
    val density = LocalDensity.current
    val haptics = LocalHapticFeedback.current
    val headW = with(density) { HeadW.toPx() }
    val rulerH = with(density) { RulerH.toPx() }
    val laneH = with(density) { (if (compact) CompactLaneH else LaneH).toPx() }
    val gap = with(density) { LaneGap.toPx() }
    val edge = with(density) { EdgeW.toPx() }
    val layout = remember(data.lanes.size, rulerH, laneH, gap) { EditLaneLayout.compute(data.lanes.size, rulerH, laneH, gap) }
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
    val currentTransition by rememberUpdatedState(onTransition)

    fun xOf(frame: Float) = headW + (frame - view.scrollFrames) * view.pxPerFrame
    fun frameAt(x: Float) = view.scrollFrames + (x - headW) / view.pxPerFrame
    fun clampScrollY() { view.scrollY = view.scrollY.coerceIn(0f, EditLaneLayout.maxScroll(currentLayout.bottom, currentHeight)) }

    // Tocando, o cabeçote nunca sai da tela: a vista vira a página, como no Premiere.
    LaunchedEffect(view, width) {
        snapshotFlow { store.playhead }.collect { frame ->
            if (view.drag != null) return@collect
            val visible = (width - headW) / view.pxPerFrame
            if (frame < view.scrollFrames || frame > view.scrollFrames + visible * 0.92f) view.scrollFrames = frame - visible * 0.08f
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

    Canvas(
        Modifier.fillMaxSize().pointerInput(store, width, headW) {
            val slop = viewConfiguration.touchSlop
            val longPressMs = viewConfiguration.longPressTimeoutMillis

            fun hitClip(p: Offset): Triple<Int, LaneClip, Int>? {
                if (p.x < headW || p.y < rulerH) return null
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
                if (start.x < headW) return@awaitEachGesture
                if (start.y < rulerH) {
                    // Régua: o cabeçote vai para o dedo e segue o arrasto.
                    store.pause()
                    store.scrubStart(frameAt(start.x).roundToInt().coerceAtLeast(0))
                    while (true) {
                        val event = awaitPointerEvent()
                        val change = event.changes.firstOrNull { it.id == down.id } ?: break
                        if (!change.pressed) break
                        store.scrubTo(frameAt(change.position.x).roundToInt().coerceIn(0, store.project.durationFrames.coerceAtLeast(0)))
                        change.consume()
                    }
                    store.scrubEnd()
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
                            }
                            event.changes.forEach { if (it.positionChanged()) it.consume() }
                            continue
                        }
                        if (change == null) break
                        val current = view.drag
                        if (current != null) updateDrag(current, start, change.position)
                        else {
                            view.scrollFrames -= (change.position.x - last.x) / view.pxPerFrame
                            view.scrollY -= change.position.y - last.y
                            clampScrollY()
                        }
                        last = change.position
                        change.consume()
                    }
                    view.drag?.let { commitDrag(it) }
                } finally {
                    view.drag = null
                }
            }
        },
    ) {
        drawLanes(store, data, layout, laneClips, view, waves, headW, rulerH, fps, density.density)
    }

    // Colunas de faixa e os alvos de acessibilidade: compostos só quando o modelo muda.
    data.lanes.forEachIndexed { i, lane ->
        val top = layout.tops[i]
        val rowH = with(density) { layout.heights[i].toDp() }
        Box(
            Modifier.width(HeadW).height(rowH)
                .graphicsLayer { translationY = top - view.scrollY; alpha = if (top - view.scrollY + layout.heights[i] <= rulerH) 0f else 1f }
                .background(EditPalette.Head),
        ) { TrackHead(store, lane) }
    }
    laneClips.forEachIndexed { i, lane ->
        lane.forEach { clip ->
            if (!clip.virtual) ClipTarget(store, clip.row, view, layout.tops[i], layout.heights[i], headW, rulerH)
        }
    }
    // O canto da régua diz qual ferramenta está ativa: a barra é só de ícones.
    Box(Modifier.width(HeadW).height(RulerH).background(EditPalette.RulerBg), contentAlignment = Alignment.Center) {
        Text(stringResource(tool.label), style = AureaType.of(11f, FontWeight.W600, color = AureaColors.Accent), maxLines = 1)
    }
    if (store.layers.isEmpty()) {
        Text(
            stringResource(R.string.edit_empty_hint), style = AureaType.of(13f, color = AureaColors.Muted),
            modifier = Modifier.offset(x = HeadW + 12.dp, y = RulerH + 12.dp),
        )
    }
}

@Composable
private fun TrackHead(store: EditorStore, lane: EditTrackInfo) {
    val targetLabel = stringResource(R.string.edit_track_target, lane.label)
    Row(Modifier.fillMaxSize().padding(horizontal = 3.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(2.dp)) {
        Box(
            Modifier.size(24.dp).clip(RoundedCornerShape(5.dp)).background(if (lane.target) Color(0xFF3B6EA8) else EditColors.DockLevel)
                .testTag("edit.track.${lane.label}.target").semantics { contentDescription = targetLabel; role = Role.Switch; selected = lane.target }
                .tocavel(shrink = 0.92f) { store.setEditTrackFlag(lane.id, 3, !lane.target) },
            contentAlignment = Alignment.Center,
        ) { Text(lane.label, style = AureaType.of(10f, FontWeight.W700, color = Color.White), maxLines = 1) }
        HeadIcon(if (lane.locked) CupertinoGlyph.LockFill else CupertinoGlyph.LockOpen, lane.locked, stringResource(R.string.edit_track_lock), "edit.track.${lane.label}.lock") {
            store.setEditTrackFlag(lane.id, 0, !lane.locked)
        }
        if (lane.audio) {
            HeadLetter("M", lane.muted, stringResource(R.string.edit_track_mute), "edit.track.${lane.label}.mute") { store.setEditTrackFlag(lane.id, 1, !lane.muted) }
            HeadLetter("S", lane.solo, stringResource(R.string.edit_track_solo), "edit.track.${lane.label}.solo") { store.setEditTrackFlag(lane.id, 2, !lane.solo) }
        } else {
            HeadIcon(if (lane.muted) CupertinoGlyph.EyeSlash else CupertinoGlyph.Eye, lane.muted, stringResource(R.string.edit_track_visible), "edit.track.${lane.label}.visible") {
                store.setEditTrackFlag(lane.id, 1, !lane.muted)
            }
        }
    }
}

@Composable
private fun HeadIcon(glyph: Char, on: Boolean, label: String, tag: String, onClick: () -> Unit) {
    Box(
        Modifier.size(22.dp).testTag(tag).semantics { contentDescription = label; role = Role.Switch; selected = on }.tocavel(shrink = 0.9f, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { CupertinoIcon(glyph, 13.dp, if (on) AureaColors.Accent else AureaColors.Muted) }
}

@Composable
private fun HeadLetter(letter: String, on: Boolean, label: String, tag: String, onClick: () -> Unit) {
    Box(
        Modifier.size(22.dp).clip(RoundedCornerShape(4.dp)).background(if (on) Color(0xFFB9472F) else Color.Transparent)
            .testTag(tag).semantics { contentDescription = label; role = Role.Switch; selected = on }.tocavel(shrink = 0.9f, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { Text(letter, style = AureaType.of(11f, FontWeight.W700, color = if (on) Color.White else AureaColors.Muted)) }
}

/** O alvo de acessibilidade de um clipe: não intercepta toque, só dá nome e ação ao leitor de tela. */
@Composable
private fun ClipTarget(store: EditorStore, clip: LayerRow, view: EditView, top: Float, heightPx: Float, headW: Float, rulerH: Float) {
    val density = LocalDensity.current
    val selected = clip.id in store.selection
    val description = if (selected) stringResource(R.string.edit_clip_selected, clip.name) else clip.name
    Box(
        Modifier
            .offset { IntOffset((headW + (clip.startFrame - view.scrollFrames) * view.pxPerFrame).roundToInt(), (top - view.scrollY).roundToInt().coerceAtLeast(rulerH.roundToInt())) }
            .size(with(density) { ((clip.endFrame - clip.startFrame) * view.pxPerFrame).coerceAtLeast(1f).toDp() }, with(density) { heightPx.toDp() })
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
    drawContext.canvas.nativeCanvas.drawText(shown, x, y, TextPaint)
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

    clipRect(left = headW, top = rulerH, right = width, bottom = size.height) {
        val firstVideoAudio = data.lanes.indexOfFirst { it.audio }
        data.lanes.forEachIndexed { i, _ ->
            val top = layout.tops[i] - scrollY
            drawRect(EditPalette.Lane, Offset(headW, top), Size(width - headW, layout.heights[i]))
            if (i == firstVideoAudio) drawRect(EditPalette.Separator, Offset(headW, top - 1.5f * dp), Size(width - headW, 1.5f * dp))
        }
        lanes.forEachIndexed { i, clips ->
            val top = layout.tops[i] - scrollY
            val laneH = layout.heights[i]
            if (top + laneH < rulerH || top > size.height) return@forEachIndexed
            clips.forEach { clip ->
                // Enquanto arrasta, o clipe levado se desenha no fim, por cima.
                if (drag != null && drag.clip.id == clip.row.id && !clip.virtual && drag.kind == DRAG_MOVE) return@forEach
                drawClip(store, clip, top, laneH, drag, data.lanes[i], selection, xOf(0f), pxPerFrame, waves, generation, headW, width, dp)
            }
            // A marca de cada corte encostado.
            if (!data.lanes[i].audio) clips.zipWithNext().forEach { (a, b) ->
                if (a.row.endFrame == b.row.startFrame) {
                    val cx = xOf(a.row.endFrame.toFloat())
                    if (cx > headW && cx < width) {
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
            if (index >= 0) drawClip(store, LaneClip(drag.clip, false), layout.tops[index] - scrollY, layout.heights[index], drag, data.lanes[index], selection, xOf(0f), pxPerFrame, waves, generation, headW, width, dp)
        }
        if (drag != null && drag.snapped >= 0) {
            val x = xOf(drag.snapped.toFloat())
            drawLine(EditPalette.Snap, Offset(x, rulerH), Offset(x, size.height), strokeWidth = 1.5f * dp)
        }
        if (data.markIn >= 0 || data.markOut >= 0) {
            val from = xOf((if (data.markIn >= 0) data.markIn else 0L).toFloat()).coerceAtLeast(headW)
            val to = if (data.markOut >= 0) xOf(data.markOut.toFloat()).coerceAtMost(width) else width
            if (to > from) drawRect(EditPalette.Range, Offset(from, rulerH), Size(to - from, size.height - rulerH))
        }
    }

    // Régua por cima das faixas.
    drawRect(EditPalette.RulerBg, Offset(headW, 0f), Size(width - headW, rulerH))
    val step = rulerStepFrames(pxPerFrame, fps, 64f * dp)
    var tick = (scroll / step).toInt().coerceAtLeast(0) * step
    val lastFrame = scroll + (width - headW) / pxPerFrame
    while (tick <= lastFrame) {
        val x = xOf(tick.toFloat())
        if (x >= headW) {
            drawLine(EditPalette.Tick, Offset(x, rulerH - 8f * dp), Offset(x, rulerH), strokeWidth = 1f * dp)
            val seconds = (tick / fps).roundToInt()
            label("%02d:%02d".format(java.util.Locale.ROOT, seconds / 60, seconds % 60), x + 3f * dp, 12f * dp, 10f * dp, EditColors.Ruler)
        }
        val half = xOf(tick + step / 2f)
        if (half >= headW) drawLine(EditPalette.Tick, Offset(half, rulerH - 4f * dp), Offset(half, rulerH), strokeWidth = 1f * dp)
        tick += step
    }
    if (data.markIn >= 0) drawMark(xOf(data.markIn.toFloat()), rulerH, headW, dp, true)
    if (data.markOut >= 0) drawMark(xOf(data.markOut.toFloat()), rulerH, headW, dp, false)

    // Cabeçote: lido aqui, na fase de desenho.
    val playX = xOf(store.playhead.toFloat())
    if (playX >= headW && playX <= width) {
        drawLine(EditPalette.Playhead, Offset(playX, rulerH - 8f * dp), Offset(playX, size.height), strokeWidth = 1.5f * dp)
        val s = 5f * dp
        drawPath(Path().apply { moveTo(playX - s, rulerH - 12f * dp); lineTo(playX + s, rulerH - 12f * dp); lineTo(playX, rulerH - 5f * dp); close() }, EditPalette.Playhead)
    }
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
    val body: Color
    val strip: Color
    when {
        clip.virtual -> { body = EditPalette.LinkedAudio; strip = EditPalette.LinkedAudio }
        audioKind -> { body = EditPalette.Audio; strip = EditPalette.Audio }
        adjustment -> { body = EditPalette.Adjustment; strip = EditPalette.AdjustmentTop }
        kind == 1 || kind == 2 || kind == 12 -> { body = EditPalette.Video; strip = EditPalette.VideoTop }
        else -> { body = EditPalette.Overlay; strip = EditPalette.OverlayTop }
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
        drawRect(strip.copy(alpha = alpha), Offset(left, top + pad), Size(w, stripH))
        val textX = visibleLeft + 3f * dp
        // Miniatura no começo do clipe, como no Premiere.
        if ((kind == 1 || kind == 2 || kind == 12) && w > 44f * dp && left >= headW - 44f * dp) {
            val heightPx = (laneH - stripH - 2 * pad).roundToInt().coerceIn(16, 128)
            val thumb = store.thumbnails.get(row.id, row.startFrame, heightPx)
            if (thumb != null) {
                val image = thumb.asImageBitmap()
                val scale = heightPx.toFloat() / image.height
                val thumbW = (image.width * scale).roundToInt()
                drawImage(image, IntOffset.Zero, IntSize(image.width, image.height), IntOffset(left.roundToInt(), (top + pad + stripH).roundToInt()), IntSize(thumbW, heightPx), alpha = alpha)
            }
        }
        if (visibleW > 16f * dp) label(row.name, textX, top + pad + stripH - 3f * dp, 9.5f * dp, Color.White, visibleW - 6f * dp - if (row.effectCount > 0) 16f * dp else 0f)
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

    private companion object { const val MAX = 4096 }
}
