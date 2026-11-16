package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.ChoiceChips
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.ds.PropertyRow
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.roundToInt
import kotlin.math.sin

// =============================================================================
//  Lote M do After Effects no Aurea PC (só o PC: o celular não muda).
//
//  MÁSCARAS E RIG. As regras moram no motor (EngineAeMasks.cpp); aqui ficam:
//    · as entradas do menu do botão direito da CAMADA na timeline do PC
//      (`layerMaskRigEntries`): Máscara ▸ Traçado automático… e Criar nulos a
//      partir de caminhos ▸ Pontos seguem nulos / Nulos seguem pontos / Traçar
//      caminho; Soltar do caminho (num nulo que segue um caminho);
//    · o menu da linha de uma MÁSCARA (`maskRowEntries`): RotoBezier, Travada,
//      Oculta no palco, Suavizar (X, Y e pontos)…, a ferramenta de suavização
//      no palco, Transformar pontos…, Criar nulos e Soltar os pontos dos nulos;
//    · os cartões flutuantes com os parâmetros (`PcMaskRigHost`), no jeito da
//      janela de painéis (PcPanelWindow.kt): cartão do Aurea arrastável.
//  A edição dos pontos de feather no palco fica em editor/EditPoints.kt
//  (`MaskFeatherTool`, atrás de PlatformFeatures.desktopAe).
// =============================================================================

/** O cartão aberto e o alvo dele. */
internal object PcMaskRig {
    const val NONE = 0
    const val AUTO_TRACE = 1
    const val NULLS = 2
    const val FEATHER = 3
    const val TRANSFORM = 4

    var dialog by mutableIntStateOf(NONE)
    var layer by mutableStateOf(0L)
    var mask by mutableIntStateOf(-1)
    /** Create Nulls: 0 pontos seguem nulos, 1 nulos seguem pontos, 2 traçar caminho. */
    var nullsMode by mutableIntStateOf(0)
    /** Canto superior esquerdo (dp); NaN = ainda não arrastado. */
    var x by mutableStateOf(Float.NaN)
    var y by mutableStateOf(Float.NaN)

    fun open(kind: Int, layerId: Long, maskId: Int = -1) {
        layer = layerId
        mask = maskId
        dialog = kind
    }
    fun close() { dialog = NONE }
}

private const val MASK_LOCKED = 1
private const val MASK_HIDDEN = 2
private const val MASK_ROTO = 4

/** aurea::Errc dos retornos negativos que a tela explica. */
private const val ERRC_OUT_OF_RANGE = 2
private const val ERRC_NOT_SUPPORTED = 6

private fun selectOnly(store: EditorStore, id: Long) {
    if (store.primary != id || store.selection.size != 1) store.select(id)
}

// -----------------------------------------------------------------------------
// Menu da camada
// -----------------------------------------------------------------------------
@Composable
internal fun layerMaskRigEntries(store: EditorStore, row: LayerRow): List<MenuEntry> {
    val id = row.id
    val type = LayerType.of(row.kind)
    if (type == LayerType.Audio) return emptyList()
    val out = mutableListOf<MenuEntry>()
    val sub = mutableListOf<MenuEntry>()
    val traceable = type == LayerType.Video || type == LayerType.Image
    sub += MenuEntry.Item(stringResource(R.string.pcm_auto_trace), glyph = CupertinoGlyph.Scribble,
        enabled = traceable && !row.locked && !store.autoTracing) {
        selectOnly(store, id)
        PcMaskRig.open(PcMaskRig.AUTO_TRACE, id)
    }
    val modes = listOf(R.string.pcm_points_follow_nulls, R.string.pcm_nulls_follow_points, R.string.pcm_trace_path)
    val nulls = modes.mapIndexed { mode, label ->
        MenuEntry.Item(stringResource(label), glyph = CupertinoGlyph.SmallcircleCircle, enabled = !row.locked) {
            selectOnly(store, id)
            PcMaskRig.nullsMode = mode
            PcMaskRig.open(PcMaskRig.NULLS, id)
        }
    }
    sub += MenuEntry.Item(stringResource(R.string.pcm_nulls_from_paths), glyph = CupertinoGlyph.SmallcircleCircle, submenu = nulls)
    out += MenuEntry.Item(stringResource(R.string.pcm_mask_menu), glyph = CupertinoGlyph.Square, submenu = sub)
    // Nulo (ou outra camada) que segue um caminho: soltar.
    val follows = remember(id, store.layers, store.detail) { (store.pathRig(id)?.get(0) ?: 0f) > 0.5f }
    if (follows) {
        out += MenuEntry.Item(stringResource(R.string.pcm_release_follow), glyph = CupertinoGlyph.SmallcircleCircle, enabled = !row.locked) {
            store.clearPathFollow(id)
        }
    }
    return out
}

// -----------------------------------------------------------------------------
// Menu da linha da máscara
// -----------------------------------------------------------------------------
@Composable
internal fun maskRowEntries(store: EditorStore, ui: EditorUi, maskId: Int): List<MenuEntry> {
    val layer = store.primary ?: return emptyList()
    val m = store.masks?.takeIf { it.layer == layer }?.find(maskId) ?: return emptyList()
    val out = mutableListOf<MenuEntry>()
    val lockedToast = stringResource(R.string.pcm_mask_locked_toast)
    val nullsDone = stringResource(R.string.pcm_nulls_done)
    val nullsFailed = stringResource(R.string.pcm_nulls_failed)
    out += MenuEntry.Header(stringResource(R.string.pn_mask_n, (store.masks?.masks?.indexOfFirst { it.id == maskId } ?: 0) + 1))
    out += MenuEntry.Item(stringResource(R.string.pcm_roto_bezier), glyph = CupertinoGlyph.Scribble, checked = m.rotoBezier,
        enabled = !m.locked, keepOpen = true) {
        store.setMaskFlags(maskId, if (m.rotoBezier) 0 else MASK_ROTO, MASK_ROTO)
    }
    out += MenuEntry.Item(stringResource(R.string.pcm_locked), glyph = CupertinoGlyph.LockFill, checked = m.locked, keepOpen = true) {
        if (!m.locked && store.maskEdit == maskId) MaskFeatherTool.active = false
        store.setMaskFlags(maskId, if (m.locked) 0 else MASK_LOCKED, MASK_LOCKED)
    }
    out += MenuEntry.Item(stringResource(R.string.pcm_hidden), glyph = CupertinoGlyph.EyeSlash, checked = m.hidden, keepOpen = true) {
        store.setMaskFlags(maskId, if (m.hidden) 0 else MASK_HIDDEN, MASK_HIDDEN)
    }
    out += MenuEntry.Divider
    out += MenuEntry.Item(stringResource(R.string.pcm_feather), glyph = CupertinoGlyph.CircleLefthalfFill) {
        PcMaskRig.open(PcMaskRig.FEATHER, layer, maskId)
    }
    val toolOn = MaskFeatherTool.active && store.maskEdit == maskId
    out += MenuEntry.Item(stringResource(R.string.pcm_feather_tool), glyph = CupertinoGlyph.CircleLefthalfFill, checked = toolOn) {
        if (m.locked) { store.showToast(lockedToast); return@Item }
        toggleFeatherTool(store, ui, maskId, !toolOn)
    }
    out += MenuEntry.Item(stringResource(R.string.pcm_transform_points), glyph = CupertinoGlyph.Rhombus, enabled = !m.locked && m.count >= 2) {
        PcMaskRig.open(PcMaskRig.TRANSFORM, layer, maskId)
    }
    out += MenuEntry.Divider
    val create: (Int, Boolean) -> Unit = { mode, loop ->
        val made = store.createNullsFromPath(layer, 0, maskId, 0, mode, loop)
        store.showToast(if (made > 0) String.format(nullsDone, made) else nullsFailed)
    }
    val nulls = listOf(
        MenuEntry.Item(stringResource(R.string.pcm_points_follow_nulls), glyph = CupertinoGlyph.SmallcircleCircle, enabled = !m.locked) { create(0, false) },
        MenuEntry.Item(stringResource(R.string.pcm_nulls_follow_points), glyph = CupertinoGlyph.SmallcircleCircle) { create(1, false) },
        MenuEntry.Item(stringResource(R.string.pcm_trace_path), glyph = CupertinoGlyph.SmallcircleCircle) { create(2, false) },
        MenuEntry.Item(stringResource(R.string.pcm_trace_path_loop), glyph = CupertinoGlyph.SmallcircleCircle) { create(2, true) },
    )
    out += MenuEntry.Item(stringResource(R.string.pcm_nulls_from_paths), glyph = CupertinoGlyph.SmallcircleCircle, submenu = nulls)
    if (m.drivers > 0) {
        out += MenuEntry.Item(stringResource(R.string.pcm_release_drivers), glyph = CupertinoGlyph.SmallcircleCircle, enabled = !m.locked) {
            store.clearPathDrivers(layer, 0, maskId, 0)
        }
    }
    return out
}

/** Liga a ferramenta de suavização no palco: o painel Máscara aberto, esta máscara em edição. */
private fun toggleFeatherTool(store: EditorStore, ui: EditorUi, maskId: Int, on: Boolean) {
    if (on) {
        pcOpenPanel(store, ui, EditorPanel.Mask)
        store.maskDrawing = false
        store.maskEdit = maskId
        store.maskPoint = -1
        MaskFeatherTool.selected = -1
    }
    MaskFeatherTool.active = on
}

// -----------------------------------------------------------------------------
// Os cartões flutuantes
// -----------------------------------------------------------------------------
@Composable
internal fun PcMaskRigHost(store: EditorStore, ui: EditorUi, maxW: Float, maxH: Float) {
    val kind = PcMaskRig.dialog
    // A ferramenta de feather só vale com o painel Máscara aberto nesta máscara.
    LaunchedEffect(ui.panel, store.maskEdit, store.primary) {
        if (MaskFeatherTool.active && (ui.panel != EditorPanel.Mask || store.maskEdit == null)) MaskFeatherTool.active = false
    }
    if (kind == PcMaskRig.NONE) return
    if (store.screen != com.aurea.aurea.state.Screen.Editor || store.primary != PcMaskRig.layer) {
        LaunchedEffect(Unit) { PcMaskRig.close() }
        return
    }
    val density = LocalDensity.current
    val w = 340f
    if (PcMaskRig.x.isNaN()) {
        PcMaskRig.x = ((maxW - w) * 0.5f).coerceAtLeast(0f)
        PcMaskRig.y = 110f
    }
    val x = PcMaskRig.x.coerceIn(0f, (maxW - 80f).coerceAtLeast(0f))
    val y = PcMaskRig.y.coerceIn(0f, (maxH - 40f).coerceAtLeast(0f))
    val shape = RoundedCornerShape(12.dp)
    val title = stringResource(when (kind) {
        PcMaskRig.AUTO_TRACE -> R.string.pcm_auto_trace_title
        PcMaskRig.NULLS -> R.string.pcm_nulls_from_paths
        PcMaskRig.FEATHER -> R.string.pcm_feather_title
        else -> R.string.pcm_transform_title
    })
    val layerName = store.layers.firstOrNull { it.id == PcMaskRig.layer }?.name.orEmpty()
    Column(
        Modifier.offset { IntOffset((x * density.density).roundToInt(), (y * density.density).roundToInt()) }
            .width(w.dp).heightIn(max = (maxH - 60f).coerceAtLeast(240f).dp).shadow(18.dp, shape).clip(shape)
            .background(AureaColors.EditorPanel).border(1.dp, AureaColors.Hairline, shape),
    ) {
        Row(
            Modifier.fillMaxWidth().height(40.dp)
                .pointerInput(Unit) {
                    detectDragGestures { c, d ->
                        c.consume()
                        PcMaskRig.x = (PcMaskRig.x + d.x / density.density).coerceIn(0f, (maxW - 80f).coerceAtLeast(0f))
                        PcMaskRig.y = (PcMaskRig.y + d.y / density.density).coerceIn(0f, (maxH - 40f).coerceAtLeast(0f))
                    }
                }
                .padding(start = 12.dp, end = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text(title, color = AureaColors.Text, fontSize = 13.sp, fontWeight = FontWeight.Medium, maxLines = 1, overflow = TextOverflow.Ellipsis)
                if (layerName.isNotEmpty()) Text(layerName, color = AureaColors.Subtle, fontSize = 10.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            Box(Modifier.size(28.dp).clip(RoundedCornerShape(6.dp)).tocavel(shrink = 1f) { PcMaskRig.close() }, contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.Xmark, 13.dp, AureaColors.Muted)
            }
        }
        Box(Modifier.fillMaxWidth().height(1.dp).background(AureaColors.Hairline))
        Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 10.dp)) {
            when (kind) {
                PcMaskRig.AUTO_TRACE -> AutoTraceCard(store)
                PcMaskRig.NULLS -> NullsCard(store)
                PcMaskRig.FEATHER -> FeatherCard(store, ui)
                PcMaskRig.TRANSFORM -> TransformCard(store)
            }
        }
    }
}

// --- Peças dos cartões -------------------------------------------------------

@Composable
private fun CardLabel(text: String) {
    Text(text, color = AureaColors.Muted, fontSize = 11.sp, modifier = Modifier.padding(top = 6.dp, bottom = 2.dp))
}

@Composable
private fun CardNote(text: String) {
    Text(text, color = AureaColors.Subtle, fontSize = 11.sp, lineHeight = 15.sp, modifier = Modifier.padding(vertical = 4.dp))
}

@Composable
private fun CardToggle(label: String, on: Boolean, enabled: Boolean = true, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().padding(vertical = 5.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, color = if (enabled) AureaColors.Text else AureaColors.Subtle, fontSize = 12.sp, modifier = Modifier.weight(1f))
        AureaToggle(on, onChange, enabled = enabled)
    }
}

/** Uma linha de valor com a régua do Aurea (arrasta para mudar). */
@Composable
private fun CardValue(
    label: String, value: Float, min: Float, max: Float, unit: String, perDp: Float, decimals: Int = 1,
    onBegin: () -> Unit = {}, onEnd: () -> Unit = {}, onChange: (Float) -> Unit,
) {
    PropertyRow(
        label = label, value = value, unitsPerDp = perDp, min = min, max = max,
        format = { v -> (if (decimals == 0) v.roundToInt().toString() else String.format(java.util.Locale.ROOT, "%.${decimals}f", v)) + unit },
        selected = false, onSelect = {}, onGestureStart = onBegin, onValue = onChange, onGestureEnd = onEnd, onTapValue = {},
    )
}

@Composable
private fun CardButtons(primary: String, enabled: Boolean = true, secondary: String? = null, onSecondary: () -> Unit = {}, onPrimary: () -> Unit) {
    Row(Modifier.fillMaxWidth().padding(top = 10.dp), horizontalArrangement = Arrangement.End, verticalAlignment = Alignment.CenterVertically) {
        if (secondary != null) {
            CardButton(secondary, AureaColors.Chip, AureaColors.Text, true, onSecondary)
            Spacer(Modifier.width(8.dp))
        }
        CardButton(primary, AureaColors.Action, Color.White, enabled, onPrimary)
    }
}

@Composable
private fun CardButton(text: String, fill: Color, ink: Color, enabled: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.height(30.dp).clip(RoundedCornerShape(8.dp)).background(if (enabled) fill else AureaColors.Chip)
            .then(if (enabled) Modifier.tocavel(shrink = 1f, onClick = onClick) else Modifier).padding(horizontal = 14.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(text, color = if (enabled) ink else AureaColors.Subtle, fontSize = 12.sp, fontWeight = FontWeight.Medium, maxLines = 1)
    }
}

// --- Traçado automático ------------------------------------------------------

@Composable
private fun AutoTraceCard(store: EditorStore) {
    val layer = PcMaskRig.layer
    val row = store.layers.firstOrNull { it.id == layer }
    val video = row != null && LayerType.of(row.kind) == LayerType.Video
    // Vídeo não traz alfa na decodificação: começa na luminância.
    var channel by remember(layer) { mutableIntStateOf(if (video) 1 else 0) }
    var threshold by remember(layer) { mutableFloatStateOf(50f) }
    var tolerance by remember(layer) { mutableFloatStateOf(1f) }
    var minArea by remember(layer) { mutableFloatStateOf(10f) }
    var roundness by remember(layer) { mutableFloatStateOf(50f) }
    var workArea by remember(layer) { mutableStateOf(false) }
    var newLayer by remember(layer) { mutableStateOf(false) }
    var invert by remember(layer) { mutableStateOf(false) }
    val working = stringResource(R.string.pcm_tracing)
    val done = stringResource(R.string.pcm_trace_done)
    val failed = stringResource(R.string.pcm_trace_failed)
    val unsupported = stringResource(R.string.pcm_trace_unsupported)
    val full = stringResource(R.string.pcm_trace_full)

    CardLabel(stringResource(R.string.pcm_channel))
    ChoiceChips(
        listOf(R.string.pcm_ch_alpha, R.string.pcm_ch_luma, R.string.pcm_ch_red, R.string.pcm_ch_green, R.string.pcm_ch_blue).map { stringResource(it) },
        channel, { channel = it },
    )
    CardValue(stringResource(R.string.pcm_threshold), threshold, 1f, 99f, " %", 0.25f, 0) { threshold = it }
    CardValue(stringResource(R.string.pcm_tolerance), tolerance, 0.2f, 20f, " px", 0.05f) { tolerance = it }
    CardValue(stringResource(R.string.pcm_min_area), minArea, 0f, 5000f, " px²", 2f, 0) { minArea = it }
    CardValue(stringResource(R.string.pcm_corner_roundness), roundness, 0f, 100f, " %", 0.5f, 0) { roundness = it }
    CardToggle(stringResource(R.string.pcm_invert), invert) { invert = it }
    CardToggle(stringResource(R.string.pcm_work_area), workArea, enabled = video) { workArea = it }
    CardToggle(stringResource(R.string.pcm_new_layer), newLayer) { newLayer = it }
    if (video && channel == 0) CardNote(stringResource(R.string.pcm_trace_video_alpha))
    CardButtons(stringResource(R.string.pcm_trace), enabled = !store.autoTracing, secondary = stringResource(R.string.pcm_cancel),
        onSecondary = { PcMaskRig.close() }) {
        store.showToast(working)
        store.autoTrace(layer, channel, threshold / 100f, tolerance, minArea, roundness, workArea && video, newLayer, invert) { made ->
            store.showToast(when {
                made > 0 -> String.format(done, made)
                made == -ERRC_NOT_SUPPORTED -> unsupported
                made == -ERRC_OUT_OF_RANGE -> full
                else -> failed
            })
        }
        PcMaskRig.close()
    }
}

// --- Criar nulos a partir de caminhos ----------------------------------------

/** Um caminho da camada: máscara (tipo 0, a = id) ou caminho vetorial (tipo 1, a = grupo, b = caminho). */
private class RigPath(val kind: Int, val a: Int, val b: Int, val label: String)

@Composable
private fun NullsCard(store: EditorStore) {
    val layer = PcMaskRig.layer
    val maskLabel = stringResource(R.string.pn_mask_n, 0).replace("0", "").trim()
    val pathLabel = stringResource(R.string.pcm_path)
    val paths = ArrayList<RigPath>()
    store.masks?.takeIf { it.layer == layer }?.masks?.forEachIndexed { n, m ->
        if (m.count >= 1) paths += RigPath(0, m.id, 0, "$maskLabel ${n + 1}")
    }
    if (store.isVectorLayer) store.vectorDoc?.groups?.forEachIndexed { gi, g ->
        g.paths.forEachIndexed { pi, _ -> paths += RigPath(1, gi, pi, "$pathLabel ${gi + 1}.${pi + 1}") }
    }
    var chosen by remember(layer, paths.size) { mutableIntStateOf(0) }
    var loop by remember(layer) { mutableStateOf(false) }
    val done = stringResource(R.string.pcm_nulls_done)
    val failed = stringResource(R.string.pcm_nulls_failed)
    if (paths.isEmpty()) {
        CardNote(stringResource(R.string.pcm_no_paths))
        CardButtons(stringResource(R.string.pcm_cancel)) { PcMaskRig.close() }
        return
    }
    CardLabel(stringResource(R.string.pcm_path))
    ChoiceChips(paths.map { it.label }, chosen.coerceIn(0, paths.size - 1), { chosen = it })
    CardLabel(stringResource(R.string.pcm_mode))
    ChoiceChips(
        listOf(R.string.pcm_points_follow_nulls, R.string.pcm_nulls_follow_points, R.string.pcm_trace_path).map { stringResource(it) },
        PcMaskRig.nullsMode, { PcMaskRig.nullsMode = it },
    )
    CardNote(stringResource(when (PcMaskRig.nullsMode) {
        0 -> R.string.pcm_points_follow_nulls_hint
        1 -> R.string.pcm_nulls_follow_points_hint
        else -> R.string.pcm_trace_path_hint
    }))
    if (PcMaskRig.nullsMode == 2) CardToggle(stringResource(R.string.pcm_loop), loop) { loop = it }
    CardButtons(stringResource(R.string.pcm_create), secondary = stringResource(R.string.pcm_cancel), onSecondary = { PcMaskRig.close() }) {
        val p = paths[chosen.coerceIn(0, paths.size - 1)]
        val made = store.createNullsFromPath(layer, p.kind, p.a, p.b, PcMaskRig.nullsMode, loop && PcMaskRig.nullsMode == 2)
        store.showToast(if (made > 0) String.format(done, made) else failed)
        PcMaskRig.close()
    }
}

// --- Suavizar (feather X/Y, pontos de feather, RotoBezier) ----------------------

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun FeatherCard(store: EditorStore, ui: EditorUi) {
    val maskId = PcMaskRig.mask
    val m = store.masks?.find(maskId)
    if (m == null) {
        LaunchedEffect(Unit) { PcMaskRig.close() }
        return
    }
    val locked = m.locked
    val linked = m.featherY < 0f
    val fy = if (linked) m.feather else m.featherY
    var dragging by remember { mutableStateOf(false) }
    val begin = { dragging = false }
    val end = { dragging = false; store.maskGestureEnd() }
    if (locked) CardNote(stringResource(R.string.pcm_mask_locked_toast))
    CardValue(stringResource(R.string.pcm_feather_x), m.feather, 0f, 500f, " px", 0.25f, onBegin = begin, onEnd = end) { v ->
        if (!locked) { store.setMaskFeatherXY(maskId, v, if (linked) -1f else m.featherY, dragging); dragging = true }
    }
    CardValue(stringResource(R.string.pcm_feather_y), fy, 0f, 500f, " px", 0.25f, onBegin = begin, onEnd = end) { v ->
        if (!locked) { store.setMaskFeatherXY(maskId, m.feather, v, dragging); dragging = true }
    }
    CardToggle(stringResource(R.string.pcm_feather_link), linked, enabled = !locked) { on ->
        store.setMaskFeatherXY(maskId, m.feather, if (on) -1f else m.feather, false)
        store.maskGestureEnd()
    }
    Box(Modifier.fillMaxWidth().padding(vertical = 6.dp).height(1.dp).background(AureaColors.Hairline))
    // Pontos de feather de largura variável.
    CardLabel(stringResource(R.string.pcm_feather_points))
    val toolOn = MaskFeatherTool.active && store.maskEdit == maskId
    CardToggle(stringResource(R.string.pcm_feather_tool), toolOn, enabled = !locked) { on -> toggleFeatherTool(store, ui, maskId, on) }
    if (toolOn) CardNote(stringResource(R.string.pcm_feather_tool_hint))
    val fp = m.featherPoints
    val count = fp.size / MaskFeatherTool.FLOATS
    if (count == 0) {
        CardNote(stringResource(R.string.pcm_feather_none))
    } else {
        val sel = MaskFeatherTool.selected.coerceIn(0, count - 1)
        FlowRow(Modifier.padding(vertical = 4.dp), horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            for (i in 0 until count) {
                val on = i == sel
                Box(
                    Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                        .tocavel(shrink = 1f) { MaskFeatherTool.selected = i }.padding(horizontal = 10.dp, vertical = 5.dp),
                ) {
                    Text("${i + 1}", color = if (on) AureaColors.Accent else AureaColors.Text, fontSize = 12.sp)
                }
            }
        }
        val o = sel * MaskFeatherTool.FLOATS
        val write: (Int, Float, Boolean) -> Unit = { slot, v, continuing ->
            val next = fp.copyOf()
            next[o + slot] = v
            store.setMaskFeatherPoints(maskId, next, continuing)
        }
        CardValue(stringResource(R.string.pcm_radius), fp[o + 2], -500f, 500f, " px", 0.25f, onBegin = begin, onEnd = end) { v ->
            if (!locked) { write(2, v, dragging); dragging = true }
        }
        CardValue(stringResource(R.string.pcm_tension), fp[o + 3] * 100f, 0f, 100f, " %", 0.5f, 0, onBegin = begin, onEnd = end) { v ->
            if (!locked) { write(3, v / 100f, dragging); dragging = true }
        }
        CardToggle(stringResource(R.string.pcm_hold), fp[o + 4] > 0.5f, enabled = !locked) { on ->
            write(4, if (on) 1f else 0f, false)
            store.maskGestureEnd()
        }
        Row(Modifier.fillMaxWidth().padding(top = 4.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            CardButton(stringResource(R.string.pcm_remove), AureaColors.Chip, AureaColors.Text, !locked) {
                val kept = FloatArray(fp.size - MaskFeatherTool.FLOATS)
                fp.copyInto(kept, 0, 0, o)
                fp.copyInto(kept, o, o + MaskFeatherTool.FLOATS, fp.size)
                MaskFeatherTool.selected = -1
                store.setMaskFeatherPoints(maskId, kept, false)
                store.maskGestureEnd()
            }
            CardButton(stringResource(R.string.pcm_remove_all), AureaColors.Chip, AureaColors.Text, !locked) {
                MaskFeatherTool.selected = -1
                store.setMaskFeatherPoints(maskId, FloatArray(0), false)
                store.maskGestureEnd()
            }
        }
    }
    if (m.rotoBezier) {
        Box(Modifier.fillMaxWidth().padding(vertical = 6.dp).height(1.dp).background(AureaColors.Hairline))
        // Tensão do RotoBezier: do ponto escolhido no palco, ou de todos.
        val point = store.maskPoint.takeIf { store.maskEdit == maskId && it in 0 until m.count } ?: -1
        var tension by remember(maskId, point) { mutableFloatStateOf(100f) }
        CardValue(
            stringResource(R.string.pcm_roto_tension) + if (point >= 0) " · ${point + 1}" else "", tension, 0f, 100f, " %", 0.5f, 0,
            onBegin = begin, onEnd = end,
        ) { v ->
            tension = v
            if (!locked) { store.setMaskRotoTension(maskId, point, v / 100f, dragging); dragging = true }
        }
    }
}

// --- Transformar pontos (Free Transform Points) --------------------------------

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun TransformCard(store: EditorStore) {
    val maskId = PcMaskRig.mask
    val m = store.masks?.find(maskId)
    if (m == null || m.locked) {
        LaunchedEffect(Unit) { PcMaskRig.close() }
        return
    }
    val n = m.count
    // Os pontos escolhidos (nenhum = todos) e o centro deles na abertura do cartão.
    var chosen by remember(maskId) { mutableStateOf(emptySet<Int>()) }
    var sx by remember(maskId) { mutableFloatStateOf(100f) }
    var sy by remember(maskId) { mutableFloatStateOf(100f) }
    var rot by remember(maskId) { mutableFloatStateOf(0f) }
    // O afim já aplicado (a b c d tx ty) e o pivô: cada mudança manda só a diferença.
    val applied = remember(maskId) { floatArrayOf(1f, 0f, 0f, 1f, 0f, 0f) }
    val pivot = remember(maskId) { floatArrayOf(Float.NaN, Float.NaN) }
    var sent by remember(maskId) { mutableStateOf(false) }

    fun resetAll() {
        sx = 100f; sy = 100f; rot = 0f
        applied[0] = 1f; applied[1] = 0f; applied[2] = 0f; applied[3] = 1f; applied[4] = 0f; applied[5] = 0f
        pivot[0] = Float.NaN
        sent = false
    }
    fun applyNow() {
        val idx = chosen.filter { it < n }.sorted()
        if (pivot[0].isNaN()) {
            val use = if (idx.isEmpty()) (0 until n).toList() else idx
            var cx = 0f; var cy = 0f
            for (i in use) { cx += m.points[i * 6]; cy += m.points[i * 6 + 1] }
            pivot[0] = cx / use.size; pivot[1] = cy / use.size
        }
        val rad = Math.toRadians(rot.toDouble())
        val kx = (sx / 100f).let { if (abs(it) < 0.01f) 0.01f else it }
        val ky = (sy / 100f).let { if (abs(it) < 0.01f) 0.01f else it }
        val a = (cos(rad) * kx).toFloat(); val b = (sin(rad) * kx).toFloat()
        val c = (-sin(rad) * ky).toFloat(); val d = (cos(rad) * ky).toFloat()
        val tx = pivot[0] - (a * pivot[0] + c * pivot[1])
        val ty = pivot[1] - (b * pivot[0] + d * pivot[1])
        // diferença = novo ∘ aplicado⁻¹
        val det = applied[0] * applied[3] - applied[1] * applied[2]
        if (abs(det) < 1e-9f) return
        val ia = applied[3] / det; val ib = -applied[1] / det; val ic = -applied[2] / det; val id = applied[0] / det
        val itx = -(ia * applied[4] + ic * applied[5]); val ity = -(ib * applied[4] + id * applied[5])
        val delta = floatArrayOf(
            a * ia + c * ib, b * ia + d * ib, a * ic + c * id, b * ic + d * id,
            a * itx + c * ity + tx, b * itx + d * ity + ty,
        )
        if (store.transformMaskPoints(maskId, idx.toIntArray(), delta, sent)) {
            sent = true
            applied[0] = a; applied[1] = b; applied[2] = c; applied[3] = d; applied[4] = tx; applied[5] = ty
        }
    }

    CardLabel(stringResource(if (chosen.isEmpty()) R.string.pcm_transform_scope_all else R.string.pcm_transform_scope_sel, chosen.size))
    if (n <= 96) {
        FlowRow(Modifier.padding(vertical = 4.dp), horizontalArrangement = Arrangement.spacedBy(4.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            for (i in 0 until n) {
                val on = i in chosen
                Box(
                    Modifier.clip(RoundedCornerShape(6.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                        .tocavel(shrink = 1f) {
                            // Trocar a escolha fecha a transformação em curso (o que foi feito fica).
                            if (sent) { store.maskGestureEnd(); resetAll() }
                            chosen = if (on) chosen - i else chosen + i
                        }
                        .padding(horizontal = 8.dp, vertical = 4.dp),
                ) {
                    Text("${i + 1}", color = if (on) AureaColors.Accent else AureaColors.Text, fontSize = 11.sp)
                }
            }
        }
    }
    CardValue(stringResource(R.string.pcm_scale_x), sx, -400f, 400f, " %", 0.25f) { sx = it; applyNow() }
    CardValue(stringResource(R.string.pcm_scale_y), sy, -400f, 400f, " %", 0.25f) { sy = it; applyNow() }
    CardValue(stringResource(R.string.pcm_rotation), rot, -360f, 360f, "°", 0.25f) { rot = it; applyNow() }
    CardButtons(stringResource(R.string.pcm_apply), secondary = stringResource(R.string.pcm_cancel), onSecondary = {
        // Cancelar: volta a identidade (dentro do mesmo passo de desfazer) e fecha.
        if (sent) { sx = 100f; sy = 100f; rot = 0f; applyNow(); store.maskGestureEnd() }
        PcMaskRig.close()
    }) {
        if (sent) store.maskGestureEnd()
        PcMaskRig.close()
    }
}
