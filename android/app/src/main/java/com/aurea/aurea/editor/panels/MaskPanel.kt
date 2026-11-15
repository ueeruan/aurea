package com.aurea.aurea.editor.panels

import androidx.annotation.StringRes
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.Alignment
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.LaunchedEffect
import com.aurea.aurea.editor.EditPointsState
import com.aurea.aurea.editor.MaskPointsColumn
import com.aurea.aurea.editor.PointsStrip
import com.aurea.aurea.editor.Trackpad
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import com.aurea.aurea.ui.ds.MenuEntry
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaIconStyle
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.LayerType

/**
 * MÁSCARA (redesenho de 13/11/2026, `VECTOR_MASK_AM.md` §3.1): o jeito de trabalhar da
 * referência, com a cara do AUREA, na moldura comum.
 *
 * - **Lista** (nada aberto): abas Máscaras · Recorte por camada. Uma linha de 37 dp por
 *   máscara; "+ Adicionar máscara" no fim mostra Desenhar · Retângulo · Elipse.
 * - **Máscara aberta** (tocar na linha): abas Forma · Modo · Borda · Seguir (vídeo).
 *   Forma = o editor de pontos (coluna · trackpad · faixa), o mesmo do vetor. Borda: o ◆
 *   e a Curva do trilho agem na linha escolhida (sem ◆ por linha). O ‹ volta à lista.
 *
 * Com uma máscara aberta o palco entra no modo de máscara (pontos, alças, laço). Tudo é
 * rasterizado pelo motor (GPU) antes dos efeitos. Par do iOS (`MaskPanel`, LayerPanels.swift).
 */
@Composable
internal fun MaskPanel(env: PanelEnv) {
    val store = env.store
    val st by remember(store) { derivedStateOf { store.masks } }
    val editId by remember(store) { derivedStateOf { store.maskEdit } }
    var listTab by rememberSaveable { mutableStateOf(MT_LIST) }
    var openTab by rememberSaveable { mutableStateOf(MT_SHAPE) }
    var edgeParam by rememberSaveable(editId) { mutableIntStateOf(0) }
    val m = st?.find(editId)
    val isVideo = store.layers.firstOrNull { it.id == store.primary }?.kind == LayerType.Video.kind
    val tab = when {
        m == null -> listTab
        openTab == MT_FOLLOW && !isVideo -> MT_SHAPE
        else -> openTab
    }
    val look = when {
        m == null -> KeyframeLook.None
        m.keyHere -> KeyframeLook.KeyHere
        m.keyCount > 0 -> KeyframeLook.Animated
        else -> KeyframeLook.None
    }
    val maskId = m?.id
    val edgeTarget = maskId?.let { TrackKey(TrackProperty.MASK_PARAM, it, edgeParam) }
    // Os keyframes do que está em foco aparecem na camada: o caminho (Forma, Modo, Seguir)
    // ou o parâmetro escolhido da Borda.
    DisposableEffect(store, store.primary, maskId, tab, edgeParam) {
        store.timelineFocus = when {
            maskId == null -> null
            tab == MT_EDGE -> listOf(TrackKey(TrackProperty.MASK_PARAM, maskId, edgeParam))
            else -> listOf(TrackKey(TrackProperty.MASK_PATH, maskId, 0))
        }
        onDispose { store.timelineFocus = null }
    }
    val pen = m != null && tab == MT_SHAPE
    // A aba Forma usa o Editar pontos sobre o caminho desta máscara.
    DisposableEffect(maskId, pen) {
        EditPointsState.mask = if (pen) maskId else null
        EditPointsState.reset()
        EditPointsState.chosen = store.maskPoint
        onDispose { EditPointsState.mask = null; EditPointsState.reset() }
    }
    // Ponto escolhido no palco: a coluna e a faixa acompanham.
    LaunchedEffect(store.maskPoint, pen) {
        if (!pen || store.maskPoint == EditPointsState.chosen) return@LaunchedEffect
        EditPointsState.choosePoint(store, store.maskPoint)
    }
    val backToList = {
        store.maskEdit = null
        store.maskDrawing = false
        store.maskPoint = -1
    }

    if (m == null) {
        // ---- Lista -------------------------------------------------------------------
        PanelFrame(
            rail = PanelRail(onBack = env.onClose),
            tabs = listOf(
                PanelTab(MT_LIST, stringResource(R.string.panel_mascaras), glyph = CupertinoGlyph.RectangleStack),
                PanelTab(MT_MATTE, stringResource(R.string.ui_mask_tab_matte), glyph = CupertinoGlyph.Crop),
            ),
            selectedTab = tab,
            onTab = { listTab = it },
        ) {
            Column(Modifier.fillMaxWidth().padding(start = 4.dp, end = 10.dp, bottom = 16.dp).testTag("mask.scroll")) {
                if (tab == MT_MATTE) TrackMatteTab(store)
                else MaskList(store, st?.masks.orEmpty()) { openTab = MT_SHAPE }
            }
        }
        return
    }

    // ---- Máscara aberta ------------------------------------------------------------------
    val tabs = buildList {
        add(PanelTab(MT_SHAPE, stringResource(R.string.fn_mask_tab_shape), glyph = CupertinoGlyph.Pencil))
        add(PanelTab(MT_MODE, stringResource(R.string.panel_mask_mode), glyph = CupertinoGlyph.SquareStack3dUp))
        add(PanelTab(MT_EDGE, stringResource(R.string.panel_mask_edge), glyph = CupertinoGlyph.Drop))
        if (isVideo) add(PanelTab(MT_FOLLOW, stringResource(R.string.panel_mask_follow), glyph = CupertinoGlyph.CameraViewfinder))
    }
    val pathKeys = store.primary?.let { id -> store.keyframes[id].orEmpty().filter { it.property == TrackProperty.MASK_PATH && it.effectIndex == m.id } }.orEmpty()
    val edgeKeys = store.primaryKeys().filter { it.property == TrackProperty.MASK_PARAM && it.effectIndex == m.id && it.paramIndex == edgeParam }
    val local = store.detail?.localPlayhead
    val edge = tab == MT_EDGE
    val railKeys = if (edge) edgeKeys else pathKeys
    val railLook = when {
        !edge -> look
        edgeKeys.any { it.time == local } -> KeyframeLook.KeyHere
        edgeKeys.isNotEmpty() -> KeyframeLook.Animated
        else -> KeyframeLook.None
    }
    val curveFrom = store.curveStart(railKeys)
    val deleteLabel = stringResource(R.string.panel_apagar_mascara)
    val rail = PanelRail(
        onBack = backToList,
        keyframeLook = railLook,
        onKeyframe = when {
            edge && edgeTarget != null -> ({ store.timelineFocus = listOf(edgeTarget); store.toggleMaskParamKey(m.id, edgeParam) })
            m.closed && !store.maskDrawing -> ({ store.toggleMaskKey(m.id) })
            else -> null
        },
        onCurve = if (curveFrom != null) ({
            store.primary?.let { id ->
                store.selectKeyframe(id, curveFrom)
                env.onOpenPanel(EditorPanel.Curve)
            }
        }) else null,
        curveReady = railKeys.size >= 2,
        moreEntries = listOf(MenuEntry.Item(deleteLabel, glyph = CupertinoGlyph.Trash, destructive = true, tag = "mask.more.delete") { store.deleteMask(m.id) }),
    )
    val at = if (pen) EditPointsState.at(store) else null
    PanelFrame(
        rail = rail,
        tabs = tabs,
        selectedTab = tab,
        onTab = { openTab = it },
        scrollContent = !pen,
        leftColumn = if (pen) ({ MaskPointsColumn(env, m, look, onBack = backToList) }) else null,
        trailing = if (pen) ({ PointsStrip(store, at?.path, Modifier.width(60.dp).fillMaxHeight()) }) else null,
    ) {
        if (pen) {
            val masks = st?.masks.orEmpty()
            if (masks.size > 1) Box(Modifier.padding(horizontal = 6.dp)) { MaskChips(store, masks, m) }
            Trackpad(store, at, Modifier.weight(1f).fillMaxWidth())
        } else Column(Modifier.fillMaxWidth().padding(start = 4.dp, end = 10.dp, bottom = 16.dp).testTag("mask.scroll")) {
            MaskOpenHeader(store, st?.masks.orEmpty(), m)
            when (tab) {
                MT_MODE -> MaskModeTab(store, m)
                MT_EDGE -> MaskEdgeTab(env, m, edgeParam) { edgeParam = it }
                MT_FOLLOW -> MaskFollowTab(store, m)
            }
        }
    }
}

private const val MT_LIST = "list"
private const val MT_MATTE = "matte"
private const val MT_SHAPE = "shape"
private const val MT_MODE = "mode"
private const val MT_EDGE = "edge"
private const val MT_FOLLOW = "follow"

/** A lista de máscaras (uma linha por máscara) e o "+ Adicionar máscara" no fim. */
@Composable
private fun MaskList(store: EditorStore, masks: List<EditorStore.MaskPath>, onOpen: () -> Unit) {
    var adding by rememberSaveable { mutableStateOf(false) }
    Spacer(Modifier.height(6.dp))
    masks.forEachIndexed { i, mk ->
        Box(Modifier.fillMaxWidth().padding(vertical = 2.dp).clip(RoundedCornerShape(5.dp)).background(AureaColors.FxLowSurface)) {
            MaskListRow(i, mk) {
                store.maskEdit = mk.id
                store.maskDrawing = !mk.closed
                store.maskPoint = -1
                onOpen()
            }
        }
    }
    val showTools = adding || masks.isEmpty()
    if (masks.isNotEmpty()) {
        val addLabel = stringResource(R.string.fn_mask_add)
        Row(
            Modifier.fillMaxWidth().height(40.dp).testTag("mask.add")
                .semantics { contentDescription = addLabel; stateDescription = if (adding) "open" else "closed" }
                .tocavel(shrink = 1f) { adding = !adding }.padding(horizontal = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(if (adding) CupertinoGlyph.Minus else CupertinoGlyph.Plus, 16.dp, AureaColors.Accent)
            Spacer(Modifier.width(8.dp))
            Text(addLabel, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)))
        }
    }
    if (showTools) {
        Spacer(Modifier.height(4.dp))
        // As três formas de criar (como as formas prontas e o desenho vetorial da referência).
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            ToolTile(stringResource(R.string.panel_desenhar_mao), on = false, icon = { drawMaskPenIcon(it) }) { adding = false; store.startMaskDrawing(); onOpen() }
            ToolTile(stringResource(R.string.panel_retangulo), on = false, icon = { drawMaskShapeIcon(it, oval = false) }) { adding = false; store.addMaskPreset(0); onOpen() }
            ToolTile(stringResource(R.string.panel_elipse), on = false, icon = { drawMaskShapeIcon(it, oval = true) }) { adding = false; store.addMaskPreset(1); onOpen() }
        }
    }
    if (masks.isEmpty()) {
        Spacer(Modifier.height(10.dp))
        KitHint(stringResource(R.string.panel_mascara_mostra_so_parte_camada_escolha))
    }
}

@Composable
private fun MaskChips(store: EditorStore, masks: List<EditorStore.MaskPath>, m: EditorStore.MaskPath?) {
    ChipRow {
        masks.forEachIndexed { i, mk ->
            KitChip(if (mk.closed) stringResource(R.string.pn_mask_n, i + 1) else stringResource(R.string.pn_mask_n_open, i + 1), m?.id == mk.id) {
                store.maskEdit = mk.id
                store.maskDrawing = !mk.closed
                store.maskPoint = -1
            }
        }
    }
}

/** A linha da máscara: ícone, nome, o modo em que ela está e ›. */
@Composable
private fun MaskListRow(index: Int, mask: EditorStore.MaskPath, onClick: () -> Unit) {
    val modes = maskModes()
    Row(
        Modifier.fillMaxWidth().height(37.dp).testTag("mask.item.$index").tocavel(shrink = 1f, onClick = onClick).padding(horizontal = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(CupertinoGlyph.SquareOnSquare, 18.dp, AureaColors.Text, autoMirror = false)
        Spacer(Modifier.width(8.dp))
        Text(
            if (mask.closed) stringResource(R.string.pn_mask_n, index + 1) else stringResource(R.string.pn_mask_n_open, index + 1),
            modifier = Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600)),
        )
        modes.firstOrNull { it.op == mask.op }?.let { Text(stringResource(it.label), maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted))) }
        Spacer(Modifier.width(6.dp))
        CupertinoIcon(CupertinoGlyph.ChevronRight, 14.dp, AureaColors.Muted)
    }
}

/** O cabeçalho da máscara aberta (nome e quantos pontos), acima do conteúdo da aba. */
@Composable
private fun MaskOpenHeader(store: EditorStore, masks: List<EditorStore.MaskPath>, m: EditorStore.MaskPath) {
    val index = masks.indexOfFirst { it.id == m.id }.coerceAtLeast(0)
    Row(Modifier.fillMaxWidth().height(36.dp).padding(start = 6.dp).testTag("mask.open.header"), verticalAlignment = Alignment.CenterVertically) {
        Text(
            if (m.closed) stringResource(R.string.pn_mask_n, index + 1) else stringResource(R.string.pn_mask_n_open, index + 1),
            modifier = Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Text)),
        )
        Text(pluralStringResource(R.plurals.pn_mask_points_so_far, m.count, m.count), maxLines = 1,
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
    }
    if (masks.size > 1) MaskChips(store, masks, m)
}

private class MaskMode(val op: Int, @StringRes val label: Int, val glyph: Char)

private fun maskModes() = listOf(
    MaskMode(0, R.string.panel_somar, CupertinoGlyph.Plus),
    MaskMode(1, R.string.panel_subtrair, CupertinoGlyph.Minus),
    MaskMode(2, R.string.panel_intersecao, CupertinoGlyph.SquareOnSquare),
    // AE compositing: Clarear (o maior valor) e Escurecer (o menor), na ordem do AE.
    MaskMode(5, R.string.ae_mask_lighten, CupertinoGlyph.Circle),
    MaskMode(6, R.string.ae_mask_darken, CupertinoGlyph.CircleFill),
    MaskMode(3, R.string.panel_diferenca, CupertinoGlyph.CircleLefthalfFill),
    MaskMode(4, R.string.panel_desligada, CupertinoGlyph.EyeSlash),
)

/** Aba Modo: os 7 modos, Inverter, a expressão do caminho (só PC) e Apagar. */
@Composable
private fun MaskModeTab(store: EditorStore, m: EditorStore.MaskPath) {
    PanelItemGrid(maskModes().map { mode ->
        PanelItem(mode.glyph, stringResource(mode.label), "mask.mode.${mode.op}", on = mode.op == m.op) {
            store.setMaskProps(m.id, mode.op, m.inverted, m.feather, m.expansion, m.opacity)
        }
    }, columns = 3)
    ToggleLine(stringResource(R.string.panel_inverter_mostrar_lado_fora), m.inverted) { on ->
        store.setMaskProps(m.id, m.op, on, m.feather, m.expansion, m.opacity)
    }
    // AE expressions (lote E), só no Aurea PC: a expressão do CAMINHO da máscara aberta.
    if (com.aurea.aurea.ui.platform.PlatformFeatures.desktopAe) {
        val exprTitle = stringResource(R.string.expr_mask_path_title)
        val exprUnit = " " + stringResource(R.string.expr_mask_path_unit)
        Spacer(Modifier.height(8.dp))
        Box(
            Modifier
                .padding(start = 6.dp)
                .clip(RoundedCornerShape(8.dp))
                .background(AureaColors.Chip)
                .tocavel { store.openExpression(exprTitle, listOf(TrackKey(TrackProperty.MASK_PATH, m.id, 0)), 1f, exprUnit) }
                .padding(horizontal = 10.dp, vertical = 7.dp),
        ) {
            Text(exprTitle, style = TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = AureaColors.Keyframe))
        }
    }
    Spacer(Modifier.height(6.dp))
    PanelItemRow(CupertinoGlyph.Trash, stringResource(R.string.panel_apagar_mascara), "mask.delete", danger = true) { store.deleteMask(m.id) }
}

/** Aba Borda: Suavizar (com a queda), Expandir e Opacidade; o ◆ e a Curva do trilho agem na escolhida. */
@Composable
private fun MaskEdgeTab(env: PanelEnv, m: EditorStore.MaskPath, chosen: Int, onChoose: (Int) -> Unit) {
    val store = env.store
    MaskRow(env, stringResource(R.string.panel_suavizar), m.id, 0, m.feather, 0.5f, 0f, 500f, "px", 0f, chosen == 0) { onChoose(0) }
    // AE compositing: queda do feather (Mask Feather Falloff), suave ou linear.
    PanelItemGrid(listOf(
        PanelItem(CupertinoGlyph.Drop, stringResource(R.string.ae_mask_falloff_smooth), "mask.falloff.0", on = m.falloff == 0) {
            store.setMaskFalloff(m.id, 0)
        },
        PanelItem(CupertinoGlyph.Minus, stringResource(R.string.ae_mask_falloff_linear), "mask.falloff.1", on = m.falloff == 1) {
            store.setMaskFalloff(m.id, 1)
        },
    ), columns = 2)
    MaskRow(env, stringResource(R.string.panel_expandir), m.id, 1, m.expansion, 0.5f, -500f, 500f, "px", 0f, chosen == 1) { onChoose(1) }
    MaskRow(env, stringResource(R.string.panel_opacidade), m.id, 2, m.opacity * 100f, 0.5f, 0f, 100f, "%", 100f, chosen == 2) { onChoose(2) }
    KitHint(stringResource(R.string.fn_mask_edge_hint))
}

/** Aba Seguir (vídeo): a máscara acompanha o que está embaixo dela. */
@Composable
private fun MaskFollowTab(store: EditorStore, m: EditorStore.MaskPath) {
    if (store.maskTracking) {
        KitHint(stringResource(R.string.panel_rastreando))
    } else {
        KitHint(stringResource(R.string.panel_mascara_segue_esta_embaixo_dela_cabecote))
        Spacer(Modifier.height(6.dp))
        PanelItemRow(CupertinoGlyph.Move, stringResource(R.string.panel_seguir_posicao), "mask.follow.0") { store.trackMask(m.id, 0) }
        PanelItemRow(CupertinoGlyph.ArrowCounterclockwise, stringResource(R.string.panel_seguir_posicao_tamanho_giro), "mask.follow.1") { store.trackMask(m.id, 1) }
    }
}
/** Ícone "desenhar": um caminho com dois pontos (mesmo traço do iOS). */
private fun DrawScope.drawMaskPenIcon(c: Color) {
    val w = size.width
    // 0.3.4: o traço fino do estilo dos ícones (AureaIconStyle.Line na caixa de 24).
    val stroke = Stroke(AureaIconStyle.Line * w / 24f, cap = StrokeCap.Round, join = StrokeJoin.Round)
    val path = Path().apply {
        moveTo(w * 0.06f, w * 0.82f)
        cubicTo(w * 0.22f, w * 0.18f, w * 0.62f, w * 0.98f, w * 0.94f, w * 0.2f)
    }
    drawPath(path, c, style = stroke)
    val r = w * 0.09f
    drawRect(c, Offset(w * 0.06f - r, w * 0.82f - r), Size(r * 2, r * 2))
    drawRect(c, Offset(w * 0.94f - r, w * 0.2f - r), Size(r * 2, r * 2))
}

/** Ícone retângulo / elipse (mesmo traço do iOS). */
private fun DrawScope.drawMaskShapeIcon(c: Color, oval: Boolean) {
    val w = size.width
    // 0.3.4: o traço fino do estilo dos ícones (AureaIconStyle.Line na caixa de 24).
    val stroke = Stroke(AureaIconStyle.Line * w / 24f, join = StrokeJoin.Round)
    val topLeft = Offset(w * 0.1f, w * 0.2f)
    val box = Size(w * 0.8f, w * 0.6f)
    if (oval) drawOval(c, topLeft, box, style = stroke) else drawRect(c, topLeft, box, style = stroke)
}

/**
 * Suavizar / Expandir / Opacidade: um arrasto = um desfazer. Tocar no rótulo escolhe a
 * linha (o ◆ e a Curva do trilho agem nela); o losango pequeno no rótulo só mostra o estado.
 */
@Composable
private fun MaskRow(env: PanelEnv, label: String, mask: Int, prop: Int, value: Float, step: Float, min: Float, max: Float, unit: String, default: Float, selected: Boolean, onSelect: () -> Unit) {
    val store = env.store
    val target = TrackKey(TrackProperty.MASK_PARAM, mask, prop)
    val keys = store.primaryKeys().filter { it.property == target.property && it.effectIndex == mask && it.paramIndex == prop }
    val local = store.detail?.localPlayhead
    val look = when { keys.any { it.time == local } -> KeyframeLook.KeyHere; keys.isNotEmpty() -> KeyframeLook.Animated; else -> KeyframeLook.None }
    fun write(v: Float) {
        onSelect()
        store.setMaskParam(mask, prop, if (prop == 2) v / 100f else v)
    }
    Box(Modifier.fillMaxWidth().testTag("mask.$mask.param.$prop").semantics { this.selected = selected }) {
        HumanRow(
            env, label, value, step, min, max, unit, 1, default,
            onStart = { store.beginGesture("máscara") },
            onValue = { write(it) },
            onEnd = { store.endGesture() },
            onCommit = { v ->
                store.beginGesture("máscara")
                write(v)
                store.endGesture()
            },
            selected = selected,
            onSelect = onSelect,
            keyframe = look,
        )
    }
}

/**
 * RECORTE POR OUTRA CAMADA (track matte): a camada só aparece através da
 * forma (alfa) ou do brilho (luma) de outra; a camada usada some da tela
 * enquanto recorta — como no AE.
 */
@Composable
private fun TrackMatteTab(store: EditorStore) {
    val tm by remember(store) { derivedStateOf { store.trackMatte } }
    val self = store.primary ?: return
    val matte = tm?.getOrNull(0) ?: 0L
    val mode = (tm?.getOrNull(1) ?: 0L).toInt()
    val rows = store.layers
    val me = rows.firstOrNull { it.id == self }
    val candidates = rows.filter { it.id != self && it.kind != LayerType.Audio.kind && it.kind != LayerType.Camera.kind && it.kind != LayerType.Light.kind }
    PanelSection(stringResource(R.string.panel_1_recortar_pelo), CupertinoGlyph.Crop, "matte.sec.mode") {
        KitHint(stringResource(R.string.pn_mask_target, me?.name.orEmpty()))
        val sourceLayer = rows.firstOrNull { it.id == matte }
        if (sourceLayer != null && mode != 0) {
            KitHint(stringResource(R.string.pn_mask_source, sourceLayer.name))
            MatteRelationDiagram(inverted = mode == 2 || mode == 4)
        }
        Spacer(Modifier.height(6.dp))
        val pickBelowMsg = stringResource(R.string.pn_mask_pick_layer_below)
        val modes = listOf(
            Triple(0, R.string.panel_nao_recortar, CupertinoGlyph.XmarkCircle),
            Triple(1, R.string.panel_pela_forma, CupertinoGlyph.Circle),
            Triple(2, R.string.panel_pela_forma_invertido, CupertinoGlyph.CircleFill),
            Triple(3, R.string.panel_pelo_brilho, CupertinoGlyph.Lightbulb),
            Triple(4, R.string.panel_pelo_brilho_invertido, CupertinoGlyph.CircleLefthalfFill),
        )
        PanelItemGrid(modes.map { (value, label, glyph) ->
            PanelItem(glyph, stringResource(label), "matte.mode.$value", on = value == mode) {
                if (value == 0) {
                    store.setTrackMatte(0L, 0)
                } else {
                    // Sem camada escolhida: a logo ABAIXO na pilha. `zIndex` 0 é a frente
                    // (`Engine::query_layers`), então o vizinho de trás é o de zIndex
                    // maior mais próximo. O motor (`set_track_matte`) usa a camada que a
                    // UI passar; os textos do painel dizem "abaixo" (P1-7).
                    val target = if (matte != 0L) matte
                    else candidates.filter { me != null && it.zIndex > me.zIndex }.minByOrNull { it.zIndex }?.id ?: 0L
                    if (target == 0L) store.showToast(pickBelowMsg) else store.setTrackMatte(target, value)
                }
            }
        })
    }
    PanelSection(stringResource(R.string.panel_2_qual_camada_recorta), CupertinoGlyph.RectangleStack, "matte.sec.layer", count = candidates.size) {
        if (candidates.isEmpty()) KitHint(stringResource(R.string.panel_nao_ha_outra_camada_possa_recortar))
        candidates.forEachIndexed { i, r ->
            val type = LayerType.of(r.kind)
            PanelItemRow(type.glyph, r.name.ifEmpty { stringResource(type.labelRes) }, "matte.layer.$i", on = matte == r.id) { store.setTrackMatte(r.id, if (mode == 0) 1 else mode) }
        }
        Spacer(Modifier.height(4.dp))
        KitHint(if (matte != 0L) stringResource(R.string.panel_camada_escolhida_recorta_esta_some_tela) else stringResource(R.string.panel_pela_forma_aparece_onde_outra_camada))
    }
}

/** Original schematic: circle = this layer, rectangle = mask. */
@Composable
private fun MatteRelationDiagram(inverted: Boolean) {
    val accent = AureaColors.Accent
    val outline = AureaColors.Muted
    Canvas(Modifier.fillMaxWidth().height(64.dp)) {
        val center = Offset(size.width / 2, size.height / 2)
        val radius = size.height * .36f
        drawCircle(outline.copy(alpha = .18f), radius, center)
        clipRect(left = if (inverted) 0f else center.x, right = if (inverted) center.x else size.width) {
            drawCircle(accent, radius, center)
        }
        drawRect(outline, Offset(center.x, center.y - radius), Size(radius * 1.4f, radius * 2),
            style = Stroke(1.dp.toPx()))
    }
}
