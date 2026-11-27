package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.input.pointer.positionChanged
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.BG_REMOVE_TYPE_ID
import com.aurea.aurea.editor.panels.bgRemoveFailedText
import com.aurea.aurea.editor.panels.effectTypeId
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.delay

// =============================================================================
//  Roto Brush do Rotobrush IA (referência: Roto Brush do After Effects).
//  "Pintar recorte" no cartão liga o modo: o palco recebe traços de Objeto
//  (verde) ou Fundo (vermelho); o motor (EngineRoto.cpp) leva os pontos da
//  composição para a camada, guarda no efeito e refaz os recortes.
// =============================================================================

internal data class CutoutTarget(val layer: Long, val effectId: Int, val automatic: Boolean)

private val brushEffectType by lazy { effectTypeId("aurea.key.rotobrush") }

internal data class CutoutStatus(
    val done: Int = 0, val total: Int = 0, val running: Boolean = false, val failure: Long = 0,
    val frameReady: Boolean = false, val framesReady: Int = 0, val frames: Int = 0,
    val strokesHere: Long = 0, val hasStrokes: Boolean = false, val effectiveModel: Long = 0,
)

internal object RotoPaint {
    var session by mutableStateOf<CutoutTarget?>(null)
    var background by mutableStateOf(false)
    /** Visualização DENTRO do modo pintar: 0 final, 1 máscara, 2 sobreposição. */
    var view by mutableStateOf(2)
    /** Raio do pincel em px da composição. */
    var radius by mutableFloatStateOf(24f)
    /** Traço em curso, em px da tela (desenho ao vivo). */
    val live = mutableStateListOf<Offset>()
    var statuses by mutableStateOf<Map<CutoutTarget, CutoutStatus>>(emptyMap())
}

/** O aviso de falha pelo motivo do motor (Renderer::roto_status [3]). */
@androidx.annotation.StringRes
internal fun rotoFailedText(reason: Long): Int = when (reason) {
    2L, 4L -> R.string.om_roto_failed_model
    3L -> R.string.om_roto_failed_decode
    else -> R.string.roto_failed
}

fun rotoPaintActive(store: EditorStore): Boolean = RotoPaint.session?.let { it.layer == store.primary } == true

private fun rotoEnd(store: EditorStore) {
    val s = RotoPaint.session ?: return
    store.engineForStress.rotoSetView(s.layer, s.effectId, 3)
    RotoPaint.session = null
    RotoPaint.live.clear()
    store.rotoChanged()
}

/**
 * Um toque seleciona o objeto (RotoClick no motor); arrastar pinta um traço. Soltar
 * manda tudo ao motor, em um passo de desfazer.
 */
internal suspend fun AwaitPointerEventScope.rotoGesture(store: EditorStore, m: StageMapper, down: PointerInputChange) {
    val s = RotoPaint.session ?: return
    if (!m.valid) return
    val xy = ArrayList<Float>()
    val screen = RotoPaint.live
    screen.clear()
    val spacing = (RotoPaint.radius * kotlin.math.abs(m.sx(1f) - m.sx(0f)) * .18f).coerceIn(1f, 4f)
    fun add(p: Offset) {
        xy += m.cx(p.x); xy += m.cy(p.y)
        screen += p
    }
    down.consume()
    add(down.position)
    val slop = viewConfiguration.touchSlop
    var moved = false
    while (true) {
        val ev = awaitPointerEvent()
        if (RotoPaint.session != s) return
        val ch = ev.changes.firstOrNull { it.id == down.id } ?: break
        if (ev.changes.any { it.id != down.id && it.pressed }) {
            screen.clear()
            return
        }
        if (!ch.pressed) {
            if (moved && ch.position != screen.last()) add(ch.position)
            break
        }
        if (ch.positionChanged()) {
            if ((ch.position - screen.last()).getDistance() >= spacing) add(ch.position)
            ch.consume()
            if ((ch.position - down.position).getDistance() > slop) moved = true
        }
    }
    screen.clear()
    if (RotoPaint.session != s || store.primary != s.layer) return
    val engine = store.engineForStress
    val added = if (moved) engine.rotoAddStroke(s.layer, s.effectId, RotoPaint.background, RotoPaint.radius, xy.toFloatArray())
                else engine.rotoAddClick(s.layer, s.effectId, RotoPaint.background, xy[0], xy[1])
    if (added) store.rotoChanged()
}

/** O traço ao vivo e o cursor do pincel, na cor do modo. */
internal fun DrawScope.drawRotoOverlay(m: StageMapper) {
    val pts = RotoPaint.live
    val scale = kotlin.math.abs(m.sx(1f) - m.sx(0f)).coerceAtLeast(0.01f)
    val r = RotoPaint.radius * scale
    val color = if (RotoPaint.background) Color(0xCCE5484D) else Color(0xCC30C46C)
    for (i in 1 until pts.size) drawLine(color, pts[i - 1], pts[i], strokeWidth = r * 2f, cap = StrokeCap.Round)
    pts.lastOrNull()?.let { drawCircle(color, r, it) }
}

private fun readCutout(store: EditorStore, target: CutoutTarget): CutoutStatus? {
    val engine = store.engineForStress
    val brush = engine.rotoStatus(target.layer, target.effectId)
    if (brush.getOrElse(10) { 0L } == 0L) return null
    if (brush[9] != 0L) return CutoutStatus(
        brush[0].toInt(), brush[1].toInt(), brush[2] != 0L, brush[3],
        brush[6] != 0L, brush[7].toInt(), brush[8].toInt(), brush[4], hasStrokes = true,
    )
    if (!target.automatic) return CutoutStatus()
    val auto = engine.bgRemoveStatus(target.layer, target.effectId)
    if (auto[8] == 0L) return null
    return CutoutStatus(
        auto[0].toInt(), auto[1].toInt(), auto[2] != 0L, auto[3],
        auto[4] != 0L, auto[5].toInt(), auto[6].toInt(), effectiveModel = auto[7],
    )
}

@Composable
internal fun CutoutStageStatus(store: EditorStore, modifier: Modifier) {
    val layer = store.primary
    val targets = store.effects.mapNotNull { effect ->
        if (!effect.enabled || layer == null || (effect.typeId != BG_REMOVE_TYPE_ID && effect.typeId != brushEffectType)) null
        else CutoutTarget(layer, effect.effectId, effect.typeId == BG_REMOVE_TYPE_ID)
    }
    LaunchedEffect(store, targets) {
        RotoPaint.statuses = RotoPaint.statuses.filterKeys { it in targets }
        while (targets.isNotEmpty()) {
            val previous = RotoPaint.statuses
            val next = targets.mapNotNull { target ->
                (readCutout(store, target) ?: previous[target])?.let { target to it }
            }.toMap()
            RotoPaint.statuses = next
            delay(if (next.size < targets.size || next.values.any { it.running || (it.frames > 0 && !it.frameReady && it.failure == 0L) }) 250 else 800)
        }
    }
    val states = targets.mapNotNull { RotoPaint.statuses[it] }
    val running = states.firstOrNull { it.running }
    val pending = states.any { it.frames > 0 && !it.frameReady && it.failure == 0L } ||
        (targets.any { !it.automatic } && store.localAiActivity and 2 != 0)
    if (running == null && !pending) return
    val label = stringResource(if (running != null) R.string.cutout_analyzing_scene else R.string.cutout_analyzing_frame)
    Column(
        modifier.widthIn(max = 264.dp).clip(RoundedCornerShape(12.dp)).background(AureaColors.EditorPanelHigh)
            .border(1.dp, AureaColors.Accent.copy(alpha = .35f), RoundedCornerShape(12.dp))
            .semantics { liveRegion = LiveRegionMode.Polite }.testTag("stage.cutout.progress")
            .padding(horizontal = 12.dp, vertical = 8.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.SemiBold, color = AureaColors.Text)))
        if (running != null && running.total > 0) {
            val done = running.done.coerceIn(0, running.total)
            LinearProgressIndicator(
                progress = { done.toFloat() / running.total },
                modifier = Modifier.fillMaxWidth(),
                color = AureaColors.Accent,
                trackColor = AureaColors.Chip,
            )
            Text(stringResource(R.string.bg_remove_progress, done, running.total), style = cutoutMuted())
        }
    }
}

@Composable
private fun cutoutMuted() = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted))

@Composable
private fun RotoChip(label: String, tag: String, on: Boolean = false, tint: Color = AureaColors.Accent, onClick: () -> Unit) {
    val shape = RoundedCornerShape(10.dp)
    Box(
        Modifier.heightIn(min = 44.dp).clip(shape)
            .background(if (on) tint.copy(alpha = .16f) else AureaColors.Chip)
            .border(1.dp, if (on) tint.copy(alpha = .48f) else Color.Transparent, shape)
            .testTag(tag)
            .semantics { role = Role.Button; contentDescription = label; selected = on }
            .tocavel(haptic = true, onClick = onClick)
            .padding(horizontal = 12.dp, vertical = 12.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = AureaColors.Text)))
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun RotoPaintControls(store: EditorStore, effectId: Int, automatic: Boolean = false) {
    val layer = store.primary ?: return
    val selected = store.editingLayers.firstOrNull { it.id == layer } ?: return
    if (selected.locked || store.effects.firstOrNull { it.effectId == effectId }?.enabled != true) return
    val target = CutoutTarget(layer, effectId, automatic)
    val mine = RotoPaint.session == target
    DisposableEffect(store, target) {
        onDispose { if (RotoPaint.session == target) rotoEnd(store) }
    }
    val status = RotoPaint.statuses[target] ?: CutoutStatus()
    val video = selected.kind == LayerType.Video.kind
    val failed = stringResource(R.string.bg_remove_failed)
    val model = store.effectParams[effectId]?.firstOrNull { it.index == 10 }?.value?.firstOrNull()?.toInt()
    Column(Modifier.fillMaxWidth().padding(bottom = 6.dp), verticalArrangement = Arrangement.spacedBy(7.dp)) {
        Text(
            stringResource(if (mine) R.string.cutout_brush_help else if (automatic) R.string.cutout_auto_help else R.string.cutout_brush_help),
            style = cutoutMuted(),
        )
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            if (!mine) RotoChip(stringResource(R.string.cutout_brush), "fx.roto.paint", true) {
                rotoEnd(store)
                store.pause()
                if (store.engineForStress.rotoSetView(layer, effectId, 2)) {
                    RotoPaint.background = false
                    RotoPaint.view = 2
                    RotoPaint.session = target
                    store.rotoChanged()
                }
            }
            if (video && (automatic || status.hasStrokes)) {
                val tag = if (status.hasStrokes) "fx.roto.propagate" else if (status.running) "fx.bg.cancel" else "fx.bg.analyze"
                RotoChip(
                    stringResource(if (status.running) R.string.bg_remove_cancel else if (status.hasStrokes) R.string.cutout_track else R.string.bg_remove_analyze),
                    tag, status.running,
                ) {
                    val engine = store.engineForStress
                    if (status.running) {
                        if (status.hasStrokes) engine.rotoCancel() else engine.bgRemoveCancel()
                    } else {
                        if (store.playing) store.pause()
                        val ok = if (status.hasStrokes) engine.rotoPropagate(layer, effectId)
                        else engine.bgRemoveAnalyze(layer, effectId)
                        if (!ok) store.showToast(failed)
                        else RotoPaint.statuses = RotoPaint.statuses + (target to status.copy(running = true, done = 0, total = status.frames))
                    }
                    readCutout(store, target)?.let { RotoPaint.statuses = RotoPaint.statuses + (target to it) }
                }
            }
            if (mine) RotoChip(stringResource(R.string.roto_done), "fx.roto.done", true) { rotoEnd(store) }
        }
        if (mine) {
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                RotoChip(stringResource(R.string.cutout_brush_add), "fx.roto.object", !RotoPaint.background, AureaColors.Success) { RotoPaint.background = false }
                RotoChip(stringResource(R.string.cutout_brush_subtract), "fx.roto.background", RotoPaint.background, AureaColors.Danger) { RotoPaint.background = true }
                if (status.strokesHere > 0) RotoChip(stringResource(R.string.roto_undo), "fx.roto.undo") {
                    if (store.engineForStress.rotoUndoStroke(layer, effectId)) store.rotoChanged()
                }
            }
            val sizeLabel = stringResource(R.string.roto_brush_size)
            Text("$sizeLabel · ${RotoPaint.radius.toInt()} px", style = cutoutMuted())
            Slider(
                value = RotoPaint.radius, onValueChange = { RotoPaint.radius = it }, valueRange = 4f..160f,
                modifier = Modifier.testTag("fx.roto.size").semantics { contentDescription = sizeLabel },
            )
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                for ((mode, res, tag) in listOf(
                    Triple(2, R.string.roto_view_overlay, "fx.roto.view.overlay"),
                    Triple(0, R.string.roto_view_final, "fx.roto.view.final"),
                    Triple(1, R.string.roto_view_mask, "fx.roto.view.mask"),
                )) RotoChip(stringResource(res), tag, RotoPaint.view == mode) {
                    if (store.engineForStress.rotoSetView(layer, effectId, mode)) {
                        RotoPaint.view = mode
                        store.rotoChanged()
                    }
                }
            }
        }
        if (status.frameReady) Text(stringResource(R.string.roto_frame_ready), style = cutoutMuted())
        if (video && status.frames > 1 && !status.running && status.framesReady > 0) Text(
            stringResource(R.string.bg_remove_clip_status, status.framesReady, status.frames),
            style = cutoutMuted(),
        )
        if (status.failure != 0L && !status.running) Text(
            stringResource(if (status.hasStrokes) rotoFailedText(status.failure) else bgRemoveFailedText(status.failure)),
            style = cutoutMuted().copy(color = AureaColors.Danger),
        )
        if (automatic && !status.hasStrokes && model == 1 && status.frames > 0 && status.effectiveModel == 0L) {
            Text(stringResource(R.string.bg_remove_person_unavailable), style = cutoutMuted())
        }
    }
}
