package com.aurea.aurea.editor.panels

import com.aurea.aurea.ui.i18n.KeepLtr
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.CurveRailIcon
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs
// Omega painel: (área 04) velocidade e volume na moldura comum.
import androidx.annotation.StringRes
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.progressBarRangeInfo
import androidx.compose.ui.semantics.setProgress
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.Dp
import com.aurea.aurea.editor.LayerPanelBits
import com.aurea.aurea.editor.layerAudioEnabled
import com.aurea.aurea.editor.toastText
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.ui.ds.NumericKeypadSheet
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.pow
import kotlin.math.roundToInt

@Composable
internal fun ClipEditPanel(env: PanelEnv) {
    val store = env.store
    val row = store.layers.firstOrNull { it.id == store.primary } ?: return
    var mode by remember(row.id) { mutableStateOf(2) }
    var step by remember(row.id) { mutableStateOf("1") }
    var previous by remember(row.id) { mutableStateOf(0L) }
    var next by remember(row.id) { mutableStateOf(0L) }
    val before = store.layers.filter { it.id != row.id && !it.locked && it.endFrame == row.startFrame }
    val after = store.layers.filter { it.id != row.id && !it.locked && it.startFrame == row.endFrame }
    val left = before.firstOrNull { it.id == previous }?.id ?: before.singleOrNull()?.id ?: 0L
    val right = after.firstOrNull { it.id == next }?.id ?: after.singleOrNull()?.id ?: 0L
    val needsLeft = mode == 3 || mode == 5
    val needsRight = mode == 4 || mode == 5
    val frames = step.toIntOrNull()?.takeIf { it in 1..3600 }
    val enabled = !row.locked && frames != null && (!needsLeft || left != 0L) && (!needsRight || right != 0L)
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(row.name, color = AureaColors.Text, fontSize = 15.sp)
        Text(stringResource(R.string.edt_clip_range, row.startFrame, row.endFrame, row.durationFrames), color = AureaColors.Muted, fontSize = 12.sp)
        TextButton(onClick = { store.editClipTime(7, 0) }, enabled = !row.locked,
            modifier = Modifier.fillMaxWidth().height(48.dp)) { Text(stringResource(R.string.beta_extend_clip)) }
        listOf(2 to stringResource(R.string.i18n_clip_slip), 3 to stringResource(R.string.edt_clip_roll_start), 4 to stringResource(R.string.edt_clip_roll_end), 5 to stringResource(R.string.i18n_clip_slide)).chunked(2).forEach { choices ->
            Row(Modifier.fillMaxWidth()) {
                choices.forEach { (value, name) ->
                    TextButton(onClick = { mode = value }, modifier = Modifier.weight(1f).height(48.dp)) {
                        Text(if (mode == value) "✓ $name" else name, color = if (mode == value) AureaColors.Accent else AureaColors.Text)
                    }
                }
            }
        }
        Text(stringResource(when(mode) {
            2 -> R.string.edt_clip_slip_desc
            3, 4 -> R.string.edt_clip_roll_desc
            else -> R.string.edt_clip_slide_desc
        }), color = AureaColors.Muted, fontSize = 13.sp)
        if (needsLeft) ClipNeighbour(stringResource(R.string.edt_clip_previous), before.map { it.id to it.name }, left) { previous = it }
        if (needsRight) ClipNeighbour(stringResource(R.string.edt_clip_next), after.map { it.id to it.name }, right) { next = it }
        OutlinedTextField(value = step, onValueChange = { value -> if (value.length <= 4 && value.all(Char::isDigit)) step = value },
            label = { Text(stringResource(R.string.edt_clip_step)) }, singleLine = true, modifier = Modifier.fillMaxWidth())
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            listOf(-1 to stringResource(R.string.edt_clip_back), 1 to stringResource(R.string.edt_clip_forward)).forEach { (sign, title) ->
                val editLabel = stringResource(R.string.edt_clip_edit_desc, title)
                TextButton(onClick = { store.editClipTime(mode, sign * (frames ?: 1), left, right) }, enabled = enabled,
                    modifier = Modifier.weight(1f).height(48.dp).semantics { contentDescription = editLabel }) {
                    Text("${if(sign < 0) "−" else "+"}${frames ?: 0} · $title")
                }
            }
        }
        if (!enabled) Text(stringResource(if(row.locked) R.string.edt_clip_unlock else R.string.edt_clip_pick), color = AureaColors.Muted, fontSize = 12.sp)
    }
}

@Composable
private fun ClipNeighbour(title: String, items: List<Pair<Long, String>>, selected: Long, select: (Long) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    Box {
        TextButton(onClick = { expanded = true }, modifier = Modifier.fillMaxWidth().height(48.dp)) {
            Text("$title: ${items.firstOrNull { it.first == selected }?.second ?: stringResource(R.string.edt_clip_choose)}")
        }
        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            items.forEach { (id, name) -> DropdownMenuItem(text = { Text(name) }, onClick = { select(id); expanded = false }) }
        }
    }
}

/** Duração da camada em "m:ss". */
private fun clock(frames: Int, fps: Float): String {
    val s = if (fps > 0f) (frames / fps).roundToInt() else 0
    return "${s / 60}:${(s % 60).toString().padStart(2, '0')}"
}

// =============================================================================
// Omega painel (área 04, P1-7): VELOCIDADE NA MOLDURA COMUM
// (`fragment_speed_keyframe` da referência: retempo · valor · régua)
//
// Coluna esquerda: ‹ · ◆ (velocidade no cabeçote) · Curva (só Bézier) · ⋯.
// Vídeo e áudio têm os extras do AUREA nas abas da direita: Velocidade ·
// Curva de tempo · Opções. Nas camadas sem fonte (forma, texto, nulo…) a
// velocidade é constante e o motor reescala a animação da própria camada.
// =============================================================================

@Composable
internal fun SpeedPanel(env: PanelEnv) {
    val store = env.store
    val d by remember(store) { derivedStateOf { store.detail } }
    val detail = d ?: return
    val kind = detail.kind
    val media = kind == LayerType.Video.kind || kind == LayerType.Audio.kind
    val group = kind == LayerType.Group.kind
    val keyed = media || group
    var tab by rememberSaveable(detail.id) { mutableStateOf("speed") }
    val speedKeys = store.keyframes[detail.id].orEmpty().filter { it.property == TrackProperty.SPEED }
    val keyHere = speedKeys.any { it.time == detail.localPlayhead }
    val look = when {
        !keyed -> KeyframeLook.None
        keyHere -> KeyframeLook.KeyHere
        detail.speedAnimated -> KeyframeLook.Animated
        else -> KeyframeLook.None
    }
    val canKey = keyed && !detail.timeRemap
    val rail = PanelRail(
        onBack = env.onClose,
        keyframeLook = look,
        onKeyframe = if (canKey) ({ store.toggleSpeedKeyframe() }) else null,
        onDeleteAllKeyframes = { store.deleteAllKeyframes(detail.id, listOf(TrackKey(TrackProperty.SPEED))) },
        onCurve = if (canKey) ({ openTrackCurve(env, TrackProperty.SPEED) }) else null,
        curveReady = speedKeys.size >= 2,
    )
    val tabs = if (media) listOf(
        PanelTab("speed", stringResource(R.string.editor_velocidade), glyph = CupertinoGlyph.Speedometer),
        PanelTab("curve", stringResource(R.string.om_lp_speed_tab_curve), glyph = CupertinoGlyph.Waveform),
        PanelTab("options", stringResource(R.string.om_lp_speed_tab_options), glyph = CupertinoGlyph.SliderHorizontal3),
    ) else emptyList()
    PanelFrame(rail = rail, tabs = tabs, selectedTab = tab, onTab = { tab = it }) {
        when (if (media) tab else "speed") {
            "curve" -> SpeedCurveTab(env)
            "options" -> SpeedOptionsTab(env)
            else -> SpeedMainTab(env, keyed)
        }
    }
}

/** Aba Velocidade: retempo, valor no cabeçote (toque = teclado) e a régua. */
@Composable
private fun SpeedMainTab(env: PanelEnv, keyed: Boolean) {
    val store = env.store
    val detail = store.detail ?: return
    val id = detail.id
    val row = store.layers.firstOrNull { it.id == id }
    // O cabeçote é lido dentro do derivedStateOf: só recompõe quando os bits mudam.
    val bits by remember(id, row?.startFrame, row?.endFrame, row?.locked, detail.speed, detail.timeFlags) {
        derivedStateOf { store.layerPanelBits(id) }
    }
    val animatedSpeed = store.speedCurve?.getOrNull(6)
    val speed = if (detail.speedAnimated && animatedSpeed != null) animatedSpeed else detail.speed
    val title = stringResource(R.string.editor_velocidade)
    val muted = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted))
    Column(Modifier.fillMaxWidth().padding(horizontal = 15.dp)) {
        Spacer(Modifier.height(10.dp))
        RetimeRow(store, id, bits)
        Spacer(Modifier.height(8.dp))
        val frozen = keyed && detail.speed == 0f && !detail.speedAnimated
        val notBakeable = !keyed && bits and LayerPanelBits.SPEED_BAKEABLE == 0
        val blocked = detail.timeRemap || frozen || notBakeable
        val value = "${speedLabel(speed)} · ${clock(detail.endFrame - detail.startFrame, store.project.fps)}"
        when {
            detail.timeRemap -> Text(stringResource(R.string.om_lp_speed_remap_on), style = muted)
            frozen -> Text(stringResource(R.string.panel_este_trecho_quadro_parado_apare_bordas), style = muted)
            notBakeable -> Text(stringResource(R.string.om_lp_speed_keyed), style = muted)
            else -> Text(
                value,
                modifier = Modifier.testTag("speed.value").semantics { contentDescription = "$title $value" }
                    .tocavel {
                        env.openKeypad(KeypadRequest(title, speed, "x", 0.05f, 16f, 2) { v ->
                            if (keyed) store.setLayerSpeed(v) else store.setLayerSpeedBaked(v)
                        })
                    },
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Text)),
            )
        }
        Spacer(Modifier.height(6.dp))
        SpeedSlider(
            value = speed,
            enabled = !blocked,
            height = 40.dp,
            tag = "speed.slider",
            onStart = { store.beginGesture("velocidade") },
            onValue = { v -> if (keyed) store.setLayerSpeed(v) else store.setLayerSpeedBaked(v) },
            onEnd = { store.endGesture() },
        )
        if (!keyed) {
            Spacer(Modifier.height(4.dp))
            Text(stringResource(R.string.om_lp_speed_constant_hint), style = muted)
        }
        Spacer(Modifier.height(10.dp))
    }
}

/** Aba Curva de tempo (extra do AUREA): Normal/Remapear, a curva de velocidade e os prontos. */
@Composable
private fun SpeedCurveTab(env: PanelEnv) {
    val store = env.store
    val remapping by remember(store) { derivedStateOf { store.detail?.timeRemap ?: false } }
    Column(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp)) {
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            RemapChip(stringResource(R.string.remap_normal), on = !remapping) { store.applySpeedRamp(-1) }
            RemapChip(stringResource(R.string.remap_linha_tempo), on = remapping) { store.enableManualTimeRemap() }
        }
        Spacer(Modifier.height(8.dp))
        // As mesmas linhas do painel antigo (par do iOS): o remapeamento em lista,
        // ou a dica de como a curva de velocidade nasce dos keyframes (◆ na coluna).
        if (remapping) TimeRemapEffectEditor(env, -1)
        else Text(stringResource(R.string.panel_velocidade_keyframes_duracao_fica), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
    }
}

/** Aba Opções (extras do AUREA): valores prontos, de trás para a frente e câmera lenta. */
@Composable
private fun SpeedOptionsTab(env: PanelEnv) {
    val store = env.store
    val speed by remember(store) { derivedStateOf { store.detail?.speed ?: 1f } }
    val reversed by remember(store) { derivedStateOf { store.detail?.reversed ?: false } }
    val frameBlend by remember(store) { derivedStateOf { store.detail?.frameBlendMode ?: 0 } }
    val isVideo by remember(store) { derivedStateOf { store.detail?.kind == LayerType.Video.kind } }
    Column(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp)) {
        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf(0.25f to "0,25x", 0.5f to "0,5x", 1f to "1x", 2f to "2x", 4f to "4x").forEach { (value, label) ->
                RemapChip(label, on = abs(speed - value) < 0.005f) { store.setLayerSpeed(value) }
            }
        }
        if (isVideo) {
            Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.panel_passar_tras_frente), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
                AureaToggle(checked = reversed, onCheckedChange = { store.setLayerReversed(it) })
            }
            Text(stringResource(R.string.panel_quadros_camera_lenta), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            Spacer(Modifier.height(6.dp))
            Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                listOf(0 to stringResource(R.string.panel_repetir_quadro), 1 to stringResource(R.string.panel_misturar), 2 to stringResource(R.string.panel_movimento_suave)).forEach { (m, label) ->
                    RemapChip(label, on = frameBlend == m) { store.setFrameBlend(m) }
                }
            }
        }
    }
}

/**
 * A curva do trecho de [property] no cabeçote: escolhe o keyframe que abre o
 * trecho e abre o editor de curva (a Curva da coluna esquerda).
 */
internal fun openTrackCurve(env: PanelEnv, property: Int) = openTrackCurve(env.store, property) { env.onOpenPanel(EditorPanel.Curve) }

internal fun openTrackCurve(store: EditorStore, property: Int, open: () -> Unit) {
    val layer = store.primary ?: return
    val local = store.detail?.localPlayhead ?: 0
    val keys = store.keyframes[layer].orEmpty().filter { it.property == property }.sortedBy { it.time }
    if (keys.size < 2) return
    val key = keys.lastOrNull { it.time <= local }?.takeIf { it != keys.last() }
        ?: keys[keys.size - 2].takeIf { local >= keys.last().time }
        ?: keys.first()
    store.selectKeyframe(layer, key)
    open()
}

// =============================================================================
// Peças da velocidade (o painel acima e a velocidade embutida do grupo)
// =============================================================================

/** Os quatro botões de retempo (37 dp, S9; alfa 0,35 desabilitado). Estados do motor. */
@Composable
internal fun RetimeRow(store: EditorStore, layerId: Long, bits: Int, modifier: Modifier = Modifier) {
    val locked = bits and LayerPanelBits.LOCKED != 0
    fun act(edge: Int) {
        if (locked) { store.toastText(R.string.editor_camada_bloqueada_desbloqueie_editar); return }
        if (store.playing) store.pause()
        if (!store.retimeLayerToPlayhead(layerId, edge)) store.toastText(R.string.timeline_cut_failed)
    }
    // A ordem do tempo fica LTR em qualquer idioma (como a timeline).
    KeepLtr {
        Row(modifier.fillMaxWidth().height(37.dp), horizontalArrangement = Arrangement.spacedBy(1.5.dp)) {
            RetimeButton(RetimeGlyph.ExtendStart, R.string.om_lp_retime_extend_start, "speed.retime.extendStart",
                bits and LayerPanelBits.RETIME_EXTEND_START != 0, RoundedCornerShape(topStart = 4.dp, bottomStart = 4.dp)) { act(0) }
            RetimeButton(RetimeGlyph.ShortenStart, R.string.om_lp_retime_shorten_start, "speed.retime.shortenStart",
                bits and LayerPanelBits.RETIME_SHORTEN_START != 0, RoundedCornerShape(0.dp)) { act(1) }
            RetimeButton(RetimeGlyph.ShortenEnd, R.string.om_lp_retime_shorten_end, "speed.retime.shortenEnd",
                bits and LayerPanelBits.RETIME_SHORTEN_END != 0, RoundedCornerShape(0.dp)) { act(2) }
            RetimeButton(RetimeGlyph.ExtendEnd, R.string.om_lp_retime_extend_end, "speed.retime.extendEnd",
                bits and LayerPanelBits.RETIME_EXTEND_END != 0, RoundedCornerShape(topEnd = 4.dp, bottomEnd = 4.dp)) { act(3) }
        }
    }
}

private enum class RetimeGlyph { ExtendStart, ShortenStart, ShortenEnd, ExtendEnd }

@Composable
private fun RowScope.RetimeButton(
    glyph: RetimeGlyph,
    @StringRes label: Int,
    tag: String,
    enabled: Boolean,
    shape: RoundedCornerShape,
    onClick: () -> Unit,
) {
    val text = stringResource(label)
    Box(
        Modifier
            .weight(1f)
            .fillMaxHeight()
            .clip(shape)
            .background(AureaColors.LayerPanelRetime)
            .testTag(tag)
            .semantics {
                contentDescription = text
                if (!enabled) disabled()
            }
            .tocavel(enabled = enabled, haptic = true, onClick = onClick)
            .alpha(if (enabled) 1f else 0.35f),
        contentAlignment = Alignment.Center,
    ) {
        val color = AureaColors.Text
        Canvas(Modifier.size(22.dp)) {
            val u = size.width / 24f
            val stroke = 1.6f * u
            val start = glyph == RetimeGlyph.ExtendStart || glyph == RetimeGlyph.ShortenStart
            // O clipe com os "quadros" dentro (retempo muda a velocidade, não só a borda).
            val l = if (start) 9f else 3f
            val r = if (start) 21f else 15f
            drawRoundRect(color, topLeft = Offset(l * u, 7f * u), size = Size((r - l) * u, 10f * u),
                cornerRadius = CornerRadius(1.5f * u), style = Stroke(stroke))
            for (i in 1..3) {
                val x = l + (r - l) * i / 4f
                drawLine(color.copy(alpha = 0.55f), Offset(x * u, 9.5f * u), Offset(x * u, 14.5f * u), 1f * u)
            }
            fun arrow(from: Float, to: Float) {
                drawLine(color, Offset(from * u, 12f * u), Offset(to * u, 12f * u), stroke, StrokeCap.Round)
                val dd = if (to < from) 2.5f else -2.5f
                drawLine(color, Offset(to * u, 12f * u), Offset((to + dd) * u, 9.5f * u), stroke, StrokeCap.Round)
                drawLine(color, Offset(to * u, 12f * u), Offset((to + dd) * u, 14.5f * u), stroke, StrokeCap.Round)
            }
            when (glyph) {
                RetimeGlyph.ExtendStart -> arrow(8f, 2f)
                RetimeGlyph.ShortenStart -> arrow(2f, 8f)
                RetimeGlyph.ShortenEnd -> arrow(22f, 16f)
                RetimeGlyph.ExtendEnd -> arrow(16f, 22f)
            }
        }
    }
}

private const val SPEED_MIN = 0.05f
private const val SPEED_MAX = 4f

/** Encosta em 0,25 · 0,5 · 0,75 · 1 · 2 · 3 e anda de 0,05 em 0,05 (a régua da referência). */
internal fun snapLinearSpeed(v: Float): Float {
    for (s in floatArrayOf(0.25f, 0.5f, 0.75f, 1f, 2f, 3f)) if (abs(v - s) < 0.06f) return s
    return ((v / 0.05f).roundToInt() * 0.05f).coerceIn(SPEED_MIN, SPEED_MAX)
}

/**
 * A RÉGUA DA VELOCIDADE (`AlightSlider` da referência): linear de 0,05 a 4×,
 * origem em 1×, marcas em 0,5 · 1 · 2 · 3 · 4, tartaruga e lebre a 50 % nas
 * pontas. Um arrasto = um gesto ([onStart]/[onEnd]). LTR em qualquer idioma.
 */
@Composable
internal fun SpeedSlider(
    value: Float,
    enabled: Boolean,
    height: Dp,
    tag: String,
    onStart: () -> Unit,
    onValue: (Float) -> Unit,
    onEnd: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val begin by rememberUpdatedState(onStart)
    val send by rememberUpdatedState(onValue)
    val finish by rememberUpdatedState(onEnd)
    val label = stringResource(R.string.editor_velocidade)
    val slower = stringResource(R.string.om_lp_speed_slower)
    val faster = stringResource(R.string.om_lp_speed_faster)
    val accent = AureaColors.Accent
    val text = AureaColors.Text
    val track = AureaColors.LayerPanelValueBox
    val state = speedLabel(value)
    KeepLtr {
        Row(modifier.fillMaxWidth().height(height).alpha(if (enabled) 1f else 0.35f), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(40.dp).semantics { contentDescription = slower }, contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.Tortoise, 20.dp, AureaColors.Text.copy(alpha = 0.5f))
            }
            Box(
                Modifier
                    .weight(1f)
                    .fillMaxHeight()
                    .testTag(tag)
                    .semantics {
                        contentDescription = label
                        stateDescription = state
                        progressBarRangeInfo = ProgressBarRangeInfo(value.coerceIn(SPEED_MIN, SPEED_MAX), SPEED_MIN..SPEED_MAX)
                        if (enabled) {
                            setProgress { v -> begin(); send(snapLinearSpeed(v)); finish(); true }
                        } else {
                            disabled()
                        }
                    }
                    .pointerInput(enabled) {
                        if (!enabled) return@pointerInput
                        val pad = 8.dp.toPx()
                        fun valueAt(x: Float): Float {
                            val w = (size.width - pad * 2).coerceAtLeast(1f)
                            return snapLinearSpeed(SPEED_MIN + ((x - pad) / w).coerceIn(0f, 1f) * (SPEED_MAX - SPEED_MIN))
                        }
                        awaitEachGesture {
                            val down = awaitFirstDown()
                            down.consume()
                            begin()
                            var last = valueAt(down.position.x)
                            send(last)
                            while (true) {
                                val change = awaitPointerEvent().changes.firstOrNull { it.id == down.id } ?: break
                                if (!change.pressed) break
                                change.consume()
                                val v = valueAt(change.position.x)
                                if (v != last) { last = v; send(v) }
                            }
                            finish()
                        }
                    }
                    .drawBehind {
                        val pad = 8.dp.toPx()
                        val w = (size.width - pad * 2).coerceAtLeast(1f)
                        fun xOf(v: Float) = pad + w * ((v.coerceIn(SPEED_MIN, SPEED_MAX) - SPEED_MIN) / (SPEED_MAX - SPEED_MIN))
                        val y = size.height / 2f
                        drawRoundRect(track, topLeft = Offset(pad, y - 2.dp.toPx()), size = Size(w, 4.dp.toPx()), cornerRadius = CornerRadius(2.dp.toPx()))
                        for (t in floatArrayOf(0.5f, 1f, 2f, 3f, 4f)) {
                            val x = xOf(t)
                            drawLine(text.copy(alpha = 0.35f), Offset(x, y + 5.dp.toPx()), Offset(x, y + 9.dp.toPx()), 1.dp.toPx())
                        }
                        // Da origem (1×) até o valor, em destaque.
                        val o = xOf(1f)
                        val vx = xOf(value)
                        drawLine(accent, Offset(minOf(o, vx), y), Offset(maxOf(o, vx), y), 4.dp.toPx(), StrokeCap.Round)
                        drawCircle(text, radius = 8.dp.toPx(), center = Offset(vx, y))
                    },
            )
            Box(Modifier.size(40.dp).semantics { contentDescription = faster }, contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.Hare, 20.dp, AureaColors.Text.copy(alpha = 0.5f))
            }
        }
    }
}

/** Caixa de valor dos painéis embutidos (56 × 36, branco 5 %); o toque abre o teclado. */
@Composable
internal fun PopoutValueBox(text: String, tag: String, label: String, onTap: () -> Unit) {
    Box(
        Modifier
            .size(56.dp, 36.dp)
            .clip(RoundedCornerShape(4.dp))
            .background(AureaColors.LayerPanelValueBox)
            .testTag(tag)
            .semantics { contentDescription = "$label $text" }
            .tocavel(onClick = onTap),
        contentAlignment = Alignment.Center,
    ) {
        Text(text, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Text)))
    }
}

/**
 * A VELOCIDADE EMBUTIDA DO GRUPO (o corpo do `panelSpeedControl`): retempo,
 * a régua de 30 dp e a pílula do valor (toque = teclado, 0,05 a 16×). Grava
 * por `setLayerSpeed` (o pré-comp anda junto), um passo de desfazer por arrasto.
 */
@Composable
internal fun GroupSpeedBody(store: EditorStore, layerId: Long, bits: Int) {
    val speed by remember(store) { derivedStateOf { store.detail?.speed ?: 1f } }
    val title = stringResource(R.string.editor_velocidade)
    var keypad by remember { mutableStateOf<KeypadRequest?>(null) }
    Column(Modifier.fillMaxSize().padding(horizontal = 15.dp)) {
        Spacer(Modifier.height(10.dp))
        RetimeRow(store, layerId, bits)
        Spacer(Modifier.weight(1f))
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            SpeedSlider(
                value = speed,
                enabled = bits and LayerPanelBits.LOCKED == 0,
                height = 30.dp,
                tag = "layer.panel.groupSpeed.slider",
                onStart = { store.beginGesture("velocidade") },
                onValue = { store.setLayerSpeed(it) },
                onEnd = { store.endGesture() },
                modifier = Modifier.weight(1f),
            )
            Spacer(Modifier.width(6.dp))
            PopoutValueBox(String.format(java.util.Locale.ROOT, "%.2fx", speed), "layer.panel.groupSpeed.value", title) {
                keypad = KeypadRequest(title, speed, "x", 0.05f, 16f, 2) { v -> store.setLayerSpeed(v) }
            }
        }
        Spacer(Modifier.height(8.dp))
    }
    keypad?.let { r -> NumericKeypadSheet(r, onDismiss = { keypad = null }) }
}

/**
 * CURVA DE VELOCIDADE: o gráfico com os pontos (chaves de VELOCIDADE), os
 * botões de ponto e os prontos. A barra não muda de tamanho: a fonte anda a
 * integral da curva (o motor mantém a mesma filmagem nos prontos).
 */
@Composable
private fun SpeedCurveSection(env: PanelEnv, data: FloatArray) {
    val store = env.store
    var selected by remember(store.primary) { mutableStateOf(-1) }
    val samples = data[0].toInt()
    val keyCount = data[1].toInt()
    val keyTimes = (0 until keyCount).mapNotNull { i -> data.getOrNull(8 + samples + i * 3)?.toInt() }
    if (selected >= keyCount) selected = -1
    val local = store.detail?.localPlayhead ?: 0
    val muted = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted))
    Text(
        stringResource(R.string.speed_at_playhead, speedLabel(data[6])),
        style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Accent)),
    )
    Spacer(Modifier.height(8.dp))
    KeepLtr {
        SpeedGraph(
            data = data,
            playhead = local,
            editable = true,
            selected = selected,
            description = stringResource(R.string.speed_graph_desc),
            onSelect = { i ->
                selected = i
                keyTimes.getOrNull(i)?.let { t -> store.detail?.let { d -> store.seek(d.timelineFrame(t)) } }
            },
            onMoveStart = { store.beginGesture("velocidade") },
            onMove = { i, t, v ->
                val moved = store.speedKeyMove(i, t, v)
                if (moved >= 0) selected = moved
            },
            onMoveEnd = { store.endGesture() },
            onScrub = { t ->
                selected = -1
                val lo = data[2].toInt()
                val hi = data[3].toInt()
                store.detail?.let { d -> store.seek(d.timelineFrame(t.coerceIn(lo, max(lo, hi - 1)))) }
            },
        )
    }
    Spacer(Modifier.height(10.dp))
    val target = if (selected >= 0) selected else keyTimes.indexOf(local)
    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        RemapChip(stringResource(R.string.speed_add_point), on = false) { selected = store.speedKeyInsert(local) }
        if (target >= 0) RemapChip(stringResource(R.string.speed_delete_point), on = false) { store.speedKeyRemove(target); selected = -1 }
        RemapChip(stringResource(R.string.speed_smooth), on = false) { store.applySpeedCurve(7) }
    }
    Spacer(Modifier.height(12.dp))
    Text(stringResource(R.string.speed_presets), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
    Spacer(Modifier.height(6.dp))
    Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        listOf(
            0 to R.string.speed_preset_flat, 1 to R.string.speed_preset_montage, 2 to R.string.speed_preset_hero,
            3 to R.string.speed_preset_bullet, 4 to R.string.speed_preset_flash_in, 5 to R.string.speed_preset_flash_out,
            6 to R.string.speed_preset_jump,
        ).forEach { (preset, label) ->
            RemapChip(stringResource(label), on = false) { selected = -1; store.applySpeedCurve(preset) }
        }
    }
    Spacer(Modifier.height(8.dp))
    Text(stringResource(R.string.speed_curve_hint), style = muted)
}

/**
 * Remapear: tempo da fonte, keyframes e rampas prontas.
 */
@Composable
private fun RemapSection(env: PanelEnv, @Suppress("UNUSED_PARAMETER") data: FloatArray?) {
    val store = env.store
    Text(stringResource(R.string.speed_presets), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
    Spacer(Modifier.height(6.dp))
    Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        listOf(
            0 to R.string.panel_linear, 1 to R.string.panel_suave, 2 to R.string.speed_preset_hero,
            3 to R.string.remap_acelerar, 4 to R.string.panel_desacelerar, 7 to R.string.remap_congelar_aqui,
            5 to R.string.speed_ramp_freeze_all,
        ).forEach { (preset, label) ->
            RemapChip(stringResource(label), on = false) { store.applySpeedRamp(preset) }
        }
    }
    Spacer(Modifier.height(4.dp))
    TimeRemapControls(env, showInterpolation = false)
}

private fun log2Speed(s: Float): Float = (kotlin.math.ln(s.coerceIn(0.05f, 16f)) / kotlin.math.ln(2f)) * 100f

/** Encosta nos valores redondos (0,25 · 0,5 · 1 · 2 · 4) quando passa perto. */
private fun snapSpeed(s: Float): Float {
    for (v in floatArrayOf(0.25f, 0.5f, 1f, 2f, 4f)) if (kotlin.math.abs(s - v) / v < 0.03f) return v
    return (s * 100f).roundToInt() / 100f
}

private fun speedLabel(s: Float): String = "${com.aurea.aurea.ui.ds.numeroPtBr(s, 2)}x"

/**
 * SOM na moldura comum (Omega painel, área 04, P1-8): coluna esquerda com ‹ ·
 * ◆ (volume) · Curva · "=" (expressão) e as abas Volume · Reforço · Balanço ·
 * Suavizar. É o mesmo conteúdo do volume embutido que sai do botão Áudio do
 * painel da camada ([VolumePopoutBody]); aqui ele abre pela trilha da timeline.
 */
@Composable
internal fun AudioPanel(env: PanelEnv) {
    VolumeFrame(env.store, onBack = env.onClose, onOpenPanel = env.onOpenPanel, openKeypad = env.openKeypad)
}

/** O volume embutido do painel da camada: a mesma moldura, com o teclado próprio. */
@Composable
internal fun VolumePopoutBody(store: EditorStore, onClose: () -> Unit, onOpenPanel: (EditorPanel) -> Unit) {
    var keypad by remember { mutableStateOf<KeypadRequest?>(null) }
    VolumeFrame(store, onBack = onClose, onOpenPanel = onOpenPanel, openKeypad = { keypad = it }, tagPrefix = "layer.panel.volume")
    keypad?.let { r -> NumericKeypadSheet(r, onDismiss = { keypad = null }) }
}

@Composable
private fun VolumeFrame(
    store: EditorStore,
    onBack: () -> Unit,
    onOpenPanel: (EditorPanel) -> Unit,
    openKeypad: (KeypadRequest) -> Unit,
    tagPrefix: String = "volume",
) {
    val d by remember(store) { derivedStateOf { store.detail } }
    val detail = d ?: return
    val row = store.layers.firstOrNull { it.id == detail.id }
    val bits = remember(detail.id, row?.startFrame, row?.endFrame, row?.locked, store.keyframes[detail.id]?.size, detail.audioFlags) {
        store.layerPanelBits(detail.id)
    }
    // O que a referência chama de `hasAnyAudio` (todo vídeo, áudio, grupo com som) + o som real do vídeo.
    val hasAudio = detail.hasAudio || layerAudioEnabled(bits)
    var tab by rememberSaveable(detail.id) { mutableStateOf("volume") }
    val volumeKeys = listOf(TrackKey(TrackProperty.AUDIO_VOLUME))
    val keys = store.keyframes[detail.id].orEmpty().filter { it.property == TrackProperty.AUDIO_VOLUME }
    val look = when {
        keys.any { it.time == detail.localPlayhead } -> KeyframeLook.KeyHere
        detail.volumeAnimated -> KeyframeLook.Animated
        else -> KeyframeLook.None
    }
    val volumeLabel = stringResource(R.string.panel_volume)
    val onVolume = hasAudio && tab == "volume"
    val rail = PanelRail(
        onBack = onBack,
        keyframeLook = if (onVolume) look else KeyframeLook.None,
        onKeyframe = if (onVolume) ({ store.toggleVolumeKeyframe() }) else null,
        onDeleteAllKeyframes = { store.deleteAllKeyframes(detail.id, volumeKeys) },
        onCurve = if (onVolume) ({ openTrackCurve(store, TrackProperty.AUDIO_VOLUME) { onOpenPanel(EditorPanel.Curve) } }) else null,
        curveReady = keys.size >= 2,
        expression = if (onVolume) store.expressionLook(volumeKeys) else com.aurea.aurea.engine.ExpressionLook.None,
        onExpression = if (onVolume) ({ store.openExpression(volumeLabel, volumeKeys, 100f, "%") }) else null,
    )
    val tabs = if (hasAudio) listOf(
        PanelTab("volume", volumeLabel, glyph = CupertinoGlyph.Speaker2),
        PanelTab("boost", stringResource(R.string.panel_reforco), glyph = CupertinoGlyph.Bolt),
        PanelTab("pan", stringResource(R.string.panel_esquerda_direita), glyph = CupertinoGlyph.ArrowLeftRight),
        PanelTab("fades", stringResource(R.string.om_lp_volume_tab_fades), glyph = CupertinoGlyph.Waveform),
    ) else emptyList()
    PanelFrame(rail = rail, tabs = tabs, selectedTab = tab, onTab = { tab = it }, modifier = Modifier.testTag("$tagPrefix.frame")) {
        if (!hasAudio) {
            // Sem áudio: só o aviso, centralizado, e o ◆ e a Curva desabilitados.
            Box(Modifier.fillMaxWidth().height(150.dp), contentAlignment = Alignment.Center) {
                Text(
                    stringResource(R.string.panel_esta_camada_nao_tem_audio),
                    textAlign = androidx.compose.ui.text.style.TextAlign.Center,
                    style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.LayerPanelDisabled)),
                    modifier = Modifier.padding(horizontal = 12.dp),
                )
            }
            return@PanelFrame
        }
        val fps = store.project.fps.takeIf { it > 0f } ?: 30f
        Column(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp)) {
            when (tab) {
                "boost" -> CompactRuler(
                    label = stringResource(R.string.panel_reforco), tag = "$tagPrefix.boost",
                    value = { dbValue(store.detail?.audioGain ?: 1f) }, text = db(detail.audioGain),
                    unitsPerDp = 0.1f, min = -24f, max = 12f, gesture = "reforço", store = store,
                    onTap = { openKeypad(KeypadRequest(it, dbValue(detail.audioGain), "dB", -24f, 12f, 1) { v -> store.setAudioGain(if (v <= -24f) 0f else 10f.pow(v / 20f)) }) },
                ) { store.setAudioGain(if (it <= -24f) 0f else 10f.pow(it / 20f)) }
                "pan" -> CompactRuler(
                    label = stringResource(R.string.panel_esquerda_direita), tag = "$tagPrefix.pan",
                    value = { (store.detail?.audioPan ?: 0f) * 100f }, text = pan(detail.audioPan),
                    unitsPerDp = 0.5f, min = -100f, max = 100f, gesture = "balanço", store = store,
                    onTap = { openKeypad(KeypadRequest(it, detail.audioPan * 100f, "", -100f, 100f, 0) { v -> store.setAudioPan(v / 100f) }) },
                ) { store.setAudioPan(it / 100f) }
                "fades" -> {
                    val maxFade = max(0f, (detail.endFrame - detail.startFrame) / fps / 2f)
                    CompactRuler(
                        label = stringResource(R.string.panel_entrada_suave), tag = "$tagPrefix.fadeIn",
                        value = { (store.detail?.audioFadeIn ?: 0) / fps }, text = secs(detail.audioFadeIn / fps),
                        unitsPerDp = 0.02f, min = 0f, max = maxFade, gesture = "entrada suave", store = store, showLabel = true,
                        onTap = { openKeypad(KeypadRequest(it, detail.audioFadeIn / fps, "s", 0f, maxFade, 2) { v -> store.setAudioFade(true, (v * fps).roundToInt()) }) },
                    ) { store.setAudioFade(true, (it * fps).roundToInt()) }
                    CompactRuler(
                        label = stringResource(R.string.panel_saida_suave), tag = "$tagPrefix.fadeOut",
                        value = { (store.detail?.audioFadeOut ?: 0) / fps }, text = secs(detail.audioFadeOut / fps),
                        unitsPerDp = 0.02f, min = 0f, max = maxFade, gesture = "saída suave", store = store, showLabel = true,
                        onTap = { openKeypad(KeypadRequest(it, detail.audioFadeOut / fps, "s", 0f, maxFade, 2) { v -> store.setAudioFade(false, (v * fps).roundToInt()) }) },
                    ) { store.setAudioFade(false, (it * fps).roundToInt()) }
                }
                else -> {
                    // Volume de 0 a 200 % (a faixa do AUREA; a referência vai a 150 %).
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        CupertinoIcon(if (detail.audioMuted) CupertinoGlyph.SpeakerSlash else CupertinoGlyph.Speaker2, 18.dp,
                            if (detail.audioMuted) AureaColors.Accent else AureaColors.Muted)
                        Spacer(Modifier.width(6.dp))
                        CompactRuler(
                            label = volumeLabel, tag = "$tagPrefix.slider",
                            value = { store.detail?.audioVolume?.times(100f) ?: 100f }, text = "${(detail.audioVolume * 100f).roundToInt()}%",
                            unitsPerDp = 0.5f, min = 0f, max = 200f, gesture = "volume", store = store,
                            onTap = { openKeypad(KeypadRequest(it, detail.audioVolume * 100f, "%", 0f, 200f, 0) { v -> store.setAudioVolume(v / 100f) }) },
                            valueTag = "$tagPrefix.value",
                        ) { store.setAudioVolume(it / 100f) }
                    }
                    Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
                        Text(stringResource(R.string.panel_mudo), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
                        AureaToggle(checked = detail.audioMuted, onCheckedChange = { store.setAudioMuted(it) }, modifier = Modifier.testTag("$tagPrefix.mute"))
                        Spacer(Modifier.width(16.dp))
                        Text(stringResource(R.string.panel_solo), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
                        AureaToggle(checked = detail.audioSolo, onCheckedChange = { store.setAudioSolo(it) }, modifier = Modifier.testTag("$tagPrefix.solo"))
                    }
                }
            }
        }
    }
}

/** Régua compacta (40 dp) + caixa de valor de 56 × 36 (toque = teclado). Um arrasto = um gesto. */
@Composable
private fun CompactRuler(
    label: String,
    tag: String,
    value: () -> Float,
    text: String,
    unitsPerDp: Float,
    min: Float,
    max: Float,
    gesture: String,
    store: EditorStore,
    onTap: (String) -> Unit,
    showLabel: Boolean = false,
    valueTag: String = "$tag.value",
    onValue: (Float) -> Unit,
) {
    Column(Modifier.fillMaxWidth()) {
        if (showLabel) Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f).height(40.dp).testTag(tag).semantics { contentDescription = "$label $text" }) {
                TickRuler(
                    value = value,
                    unitsPerDp = unitsPerDp,
                    active = true,
                    modifier = Modifier.fillMaxSize().valueDrag(
                        enabled = true,
                        start = value,
                        unitsPerDp = { unitsPerDp },
                        min = min,
                        max = max,
                        onStart = { store.beginGesture(gesture) },
                        onValue = onValue,
                        onEnd = { store.endGesture() },
                    ),
                )
            }
            Spacer(Modifier.width(6.dp))
            PopoutValueBox(text, valueTag, label) { onTap(label) }
        }
    }
}

private fun dbValue(linear: Float): Float = if (linear <= 0f) -24f else (20f * log10(linear)).coerceIn(-24f, 12f)

private fun db(linear: Float): String {
    if (linear <= 0f) return "−∞ dB"
    val v = 20f * log10(linear)
    val r = (v * 10f).roundToInt() / 10f
    return (if (r > 0f) "+" else if (r < 0f) "−" else "") + "%.1f".format(abs(r)).replace('.', ',') + " dB"
}

@Composable
private fun pan(p: Float): String {
    val v = (p * 100f).roundToInt()
    return when {
        v == 0 -> stringResource(R.string.panel_centro)
        v < 0 -> stringResource(R.string.edt_pan_left, -v)
        else -> stringResource(R.string.edt_pan_right, v)
    }
}

private fun secs(s: Float): String = "%.2f s".format(s).replace('.', ',')

@Composable
private fun AudioToggle(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        AureaToggle(checked = checked, onCheckedChange = onChange)
    }
}

/** Linha com régua arrastável (um gesto = um passo de desfazer) e o valor à direita. */
@Composable
private fun AudioRuler(
    label: String,
    value: () -> Float,
    text: String,
    unitsPerDp: Float,
    min: Float,
    max: Float,
    gesture: String,
    store: com.aurea.aurea.state.EditorStore,
    keyframe: KeyframeLook = KeyframeLook.None,
    onKeyframe: (() -> Unit)? = null,
    expression: com.aurea.aurea.engine.ExpressionLook = com.aurea.aurea.engine.ExpressionLook.None,
    onExpression: (() -> Unit)? = null,
    onValue: (Float) -> Unit,
) {
    PropertyCustomRow(
        label, selected = keyframe != KeyframeLook.None, onSelect = { onKeyframe?.invoke() }, keyframe = keyframe,
        expression = expression, onExpression = onExpression,
    ) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f).height(40.dp)) {
                TickRuler(
                    value = value,
                    unitsPerDp = unitsPerDp,
                    active = true,
                    modifier = Modifier.fillMaxSize().valueDrag(
                        enabled = true,
                        start = value,
                        unitsPerDp = { unitsPerDp },
                        min = min,
                        max = max,
                        onStart = { store.beginGesture(gesture) },
                        onValue = onValue,
                        onEnd = { store.endGesture() },
                    ),
                )
            }
            Spacer(Modifier.width(8.dp))
            ValueBox(text, onTap = null)
        }
    }
}

/**
 * REMAPEAR TEMPO em lista (inspirado no Node Video, com o tema do Aurea): três
 * linhas e o Ao contrário, nada de gráfico de valor.
 *  1. Manter o tom do áudio — liga/desliga (o som segue a curva sem mudar a nota).
 *  2. Remapear tempo — o MOMENTO DA FONTE no cabeçote, em timecode. Arrastar o
 *     valor para os lados (ajuste fino: 1 quadro a cada 4 dp) ou tocar para
 *     digitar grava a chave ali: "neste instante da timeline mostra este
 *     instante do vídeo". O ◇ do rótulo marca/tira a chave; a faixa fina embaixo
 *     mostra as chaves e o cabeçote (tocar numa chave vai até ela e a escolhe; a
 *     curva ao lado abre o editor de curva NORMAL do Aurea para aquele trecho).
 *  3. Interpolação do tempo — Desligado / Mistura de quadros / Optical flow.
 * O dado é a MESMA curva da camada: prévia, exportação e som seguem ela.
 */
@Composable
internal fun TimeRemapEffectEditor(env: PanelEnv, @Suppress("UNUSED_PARAMETER") effectId: Int) {
    TimeRemapControls(env, showInterpolation = true)
}

/**
 * As linhas do remapeamento, no efeito antigo (projetos salvos com ele) e na
 * aba Remapear do painel de velocidade (0.1.8). O momento da fonte no cabeçote
 * vem do motor (`query_speed_curve`), não do parâmetro do efeito.
 */
@Composable
internal fun TimeRemapControls(env: PanelEnv, showInterpolation: Boolean) {
    val store = env.store
    val q by remember(store) { derivedStateOf { store.timeRemap } }
    val local by remember(store) { derivedStateOf { store.detail?.localPlayhead ?: 0 } }
    // Manual remap has no effect id. Read the source moment from the same core
    // curve used by preview/export instead of looking up an effect parameter.
    val sourceFrames by remember(store) {
        derivedStateOf {
            if (store.timeRemap == null) 0f else store.detail?.let { detail ->
                // detail is observable and includes localPlayhead; the curve
                // state above also invalidates this value when keys change.
                store.timeRemapCurrentSource(detail.id).takeIf { it.isFinite() }?.toFloat()
            } ?: 0f
        }
    }
    val keepPitch by remember(store) { derivedStateOf { store.detail?.keepPitch ?: false } }
    val frameBlend by remember(store) { derivedStateOf { store.detail?.frameBlendMode ?: 0 } }
    val reversed by remember(store) { derivedStateOf { store.detail?.remapReversed ?: false } }
    val isVideo by remember(store) { derivedStateOf { store.detail?.kind == LayerType.Video.kind } }
    val selectedKey by remember(store) {
        derivedStateOf { store.selectedKeyframe?.second?.takeIf { it.property == TrackProperty.TIME_REMAP }?.time }
    }
    val muted = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted))
    val rowText = AureaType.Base.merge(TextStyle(fontSize = 13.sp))
    Column(Modifier.fillMaxWidth()) {
        // 1) Manter o tom do áudio.
        Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.remap_manter_tom), modifier = Modifier.weight(1f), style = rowText)
            AureaToggle(checked = keepPitch, onCheckedChange = { store.setKeepPitch(it) })
        }
        val data = q
        if (data == null || data.size < 5) {
            Text(stringResource(R.string.remap_ligue_efeito), style = muted)
            return@Column
        }
        val fps = store.project.fps.takeIf { it > 0f } ?: 30f
        val lo = data[1].toInt()
        val hi = data[2].toInt()
        val lastFrame = max(1f, if (data[3] > 0f) data[3] else data[2])
        val keyTimes = (0 until data[0].toInt()).mapNotNull { i -> data.getOrNull(5 + i * 7)?.toInt() }
        val keyHere = keyTimes.indexOf(local)
        val inside = local in lo..hi
        // 2) Remapear tempo: ◇ no rótulo, timecode arrastável/digitável e a curva.
        var drag by remember { mutableStateOf<Float?>(null) }
        var dragLocal by remember(store.primary) { mutableStateOf<Int?>(null) }
        val shownFrames = drag ?: sourceFrames
        val title = stringResource(R.string.remap_linha_tempo)
        val curveDesc = stringResource(R.string.remap_editar_curva)
        PropertyCustomRow(
            label = title,
            selected = keyHere >= 0,
            onSelect = {
                if (inside) {
                    if (keyHere >= 0) store.remapRemove(keyHere) else store.remapInsert(local.toLong())
                }
            },
            keyframe = if (keyHere >= 0) KeyframeLook.KeyHere else KeyframeLook.Animated,
        ) {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Box(
                    Modifier.weight(1f).height(48.dp).valueDrag(
                        enabled = inside,
                        start = { sourceFrames },
                        unitsPerDp = { 0.25f },
                        min = 0f,
                        max = lastFrame,
                        onStart = { dragLocal = local; store.beginGesture("tempo do vídeo") },
                        onValue = { f -> drag = f.roundToInt().toFloat(); store.setRemapTime(0, f.roundToInt() / fps, dragLocal ?: local) },
                        onEnd = { store.endGesture(); drag = null; dragLocal = null },
                    ),
                    contentAlignment = Alignment.CenterStart,
                ) {
                    ValueBox(
                        timecode(shownFrames.roundToInt(), fps),
                        width = 112.dp,
                        enabled = inside,
                        onTap = {
                            env.openKeypad(
                                KeypadRequest(title, (shownFrames / fps), "s", 0f, lastFrame / fps, 2) { s ->
                                    store.setRemapTime(0, s.coerceIn(0f, lastFrame / fps))
                                },
                            )
                        },
                    )
                }
                Box(
                    Modifier.size(44.dp).semantics { contentDescription = curveDesc }
                        .tocavel(enabled = keyTimes.size >= 2) { openRemapCurve(env, local) },
                    contentAlignment = Alignment.Center,
                ) {
                    CurveRailIcon(enabled = keyTimes.size >= 2, animated = selectedKey != null || keyHere >= 0)
                }
            }
        }
        RemapKeyStrip(
            keys = keyTimes,
            lo = lo,
            hi = hi,
            playhead = local,
            selected = selectedKey,
            description = stringResource(R.string.remap_faixa_chaves),
            onKey = { t -> store.selectRemapKey(t) },
            onScrub = { t -> store.detail?.let { d -> store.seek(d.timelineFrame(t.coerceIn(lo, max(lo, hi - 1)))) } },
        )
        if (!inside) Text(stringResource(R.string.remap_fora_clipe), style = muted)
        Spacer(Modifier.height(6.dp))
        // 3) Interpolação do tempo (quadros entre os da fonte: prévia e exportação).
        if (isVideo && showInterpolation) {
            Text(stringResource(R.string.remap_interpolacao), style = rowText)
            Spacer(Modifier.height(6.dp))
            Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                listOf(0 to R.string.remap_interp_off, 1 to R.string.remap_interp_blend, 2 to R.string.remap_interp_flow).forEach { (m, label) ->
                    RemapChip(stringResource(label), on = frameBlend == m) { store.setFrameBlend(m) }
                }
            }
            Spacer(Modifier.height(4.dp))
        }
        // Ao contrário: espelha a curva (duas vezes volta ao que era).
        Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.remap_ao_contrario), modifier = Modifier.weight(1f), style = rowText)
            AureaToggle(checked = reversed, onCheckedChange = { store.reverseRemap() })
        }
        Text(stringResource(R.string.remap_dica_lista), style = muted)
    }
}

/**
 * Timecode da fonte, H:MM:SS:QQ (quadros na taxa do projeto). Taxa livre
 * (29,97, 144…): os segundos são os REAIS — arredondar a taxa para inteiro
 * deixava 29,97 fps 3,6 s adiantado por hora. Mesma conta do relógio da timeline.
 */
internal fun timecode(frames: Int, fps: Float): String {
    val p = IntArray(4)
    com.aurea.aurea.editor.timeline.Timecode.split(max(0, frames), fps, p)
    return String.format(java.util.Locale.ROOT, if (fps > 100f) "%d:%02d:%02d:%03d" else "%d:%02d:%02d:%02d", p[0], p[1], p[2], p[3])
}

/**
 * Editor de curva NORMAL do Aurea para o trecho do remapeamento: a chave
 * escolhida na faixa, senão a que abre o trecho sob o cabeçote.
 */
private fun openRemapCurve(env: PanelEnv, local: Int) {
    val store = env.store
    val layer = store.primary ?: return
    val keys = store.keyframes[layer].orEmpty().filter { it.property == TrackProperty.TIME_REMAP }.sortedBy { it.time }
    if (keys.size < 2) return
    val chosen = store.selectedKeyframe?.takeIf { it.first == layer && it.second.property == TrackProperty.TIME_REMAP }?.second
    val key = chosen?.let { c -> keys.firstOrNull { it.time == c.time } }
        ?: keys.lastOrNull { it.time <= local }?.takeIf { it != keys.last() }
        ?: keys[keys.size - 2].takeIf { local >= keys.last().time }
        ?: keys.first()
    store.selectKeyframe(layer, key)
    env.onOpenPanel(EditorPanel.Curve)
}

@Composable
private fun RemapChip(label: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.height(44.dp).clip(RoundedCornerShape(8.dp))
            .background(if (on) AureaColors.AccentDim else AureaColors.Chip)
            .semantics { selected = on }
            .tocavel(onClick = onClick).padding(horizontal = 12.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
    }
}

/**
 * A faixa fina das chaves (tempo local do clipe, lo..hi): losangos nas chaves,
 * o escolhido aceso, e a linha do cabeçote. Tocar perto de um losango vai até
 * ele e o escolhe; tocar/arrastar no resto move o cabeçote (a prévia segue).
 */
@Composable
private fun RemapKeyStrip(
    keys: List<Int>,
    lo: Int,
    hi: Int,
    playhead: Int,
    selected: Int?,
    description: String,
    onKey: (Int) -> Unit,
    onScrub: (Int) -> Unit,
) {
    val span = max(1, hi - lo)
    val accent = AureaColors.Accent
    val keyColor = AureaColors.Text
    val track = AureaColors.Chip
    val head = AureaColors.Playhead
    val readKeys by androidx.compose.runtime.rememberUpdatedState(keys)
    val readKey by androidx.compose.runtime.rememberUpdatedState(onKey)
    val readScrub by androidx.compose.runtime.rememberUpdatedState(onScrub)
    Box(
        Modifier.fillMaxWidth().height(32.dp).padding(start = 102.dp, end = 4.dp)
            .semantics { contentDescription = description }
            .pointerInput(lo, hi) {
                val pad = 8.dp.toPx()
                fun frameAt(x: Float): Int {
                    val w = (size.width - pad * 2).coerceAtLeast(1f)
                    return lo + (((x - pad) / w).coerceIn(0f, 1f) * span).roundToInt()
                }
                fun xOf(t: Int): Float = pad + (size.width - pad * 2).coerceAtLeast(1f) * ((t - lo).toFloat() / span)
                awaitEachGesture {
                    val down = awaitFirstDown()
                    down.consume()
                    val hit = readKeys.minByOrNull { abs(xOf(it) - down.position.x) }
                        ?.takeIf { abs(xOf(it) - down.position.x) <= 14.dp.toPx() }
                    if (hit != null) readKey(hit) else readScrub(frameAt(down.position.x))
                    while (true) {
                        val change = awaitPointerEvent().changes.firstOrNull { it.id == down.id } ?: break
                        if (!change.pressed) break
                        change.consume()
                        if (hit == null) readScrub(frameAt(change.position.x))
                    }
                }
            }
            .drawBehind {
                val pad = 8.dp.toPx()
                val w = (size.width - pad * 2).coerceAtLeast(1f)
                fun xOf(t: Int): Float = pad + w * ((t - lo).toFloat() / span)
                val y = size.height / 2f
                drawRoundRect(track, topLeft = Offset(pad, y - 1.5.dp.toPx()), size = Size(w, 3.dp.toPx()), cornerRadius = CornerRadius(2f, 2f))
                val r = 5.dp.toPx()
                keys.forEach { t ->
                    if (t < lo - 1 || t > hi + 1) return@forEach
                    val x = xOf(t.coerceIn(lo, hi))
                    val path = androidx.compose.ui.graphics.Path().apply {
                        moveTo(x, y - r); lineTo(x + r, y); lineTo(x, y + r); lineTo(x - r, y); close()
                    }
                    drawPath(path, if (t == selected || t == playhead) accent else keyColor)
                }
                val px = xOf(playhead.coerceIn(lo, hi))
                drawLine(head, Offset(px, 2.dp.toPx()), Offset(px, size.height - 2.dp.toPx()), strokeWidth = 2.dp.toPx())
            },
    )
}
