package com.aurea.aurea.editor.panels

import androidx.compose.foundation.Canvas
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
import com.aurea.aurea.ui.ds.ChoiceChips
import com.aurea.aurea.ui.ds.KeyframeLook
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.engine.TrackKey
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaIconStyle
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.LayerType

/**
 * MÁSCARA E RECORTE.
 *
 * Três abas no topo: PONTOS, MÁSCARAS e RECORTE POR OUTRA CAMADA (track matte).
 * PONTOS é o modo de caneta, com o desenho do Editar pontos: coluna à esquerda,
 * trackpad no centro e a faixa de pontos à direita (EditPoints.kt). MÁSCARAS tem
 * os botões de criar, os chips e, da escolhida, Modo, Borda, Seguir objeto (só em
 * vídeo) e Apagar. Par do iOS (LayerPanels.swift).
 *
 * Com uma máscara escolhida o palco entra no modo de máscara: tocar põe
 * pontos (arrastar ao pôr curva o trecho), arrastar um ponto o move, as alças
 * do ponto escolhido mudam a curva. Tudo é rasterizado pelo motor (GPU) antes
 * dos efeitos.
 */
@Composable
internal fun MaskPanel(env: PanelEnv) {
    val store = env.store
    val st by remember(store) { derivedStateOf { store.masks } }
    val editId by remember(store) { derivedStateOf { store.maskEdit } }
    val drawing by remember(store) { derivedStateOf { store.maskDrawing } }
    var top by rememberSaveable { mutableIntStateOf(0) }
    val m = st?.find(editId)
    val look = when {
        m == null -> KeyframeLook.None
        m.keyHere -> KeyframeLook.KeyHere
        m.keyCount > 0 -> KeyframeLook.Animated
        else -> KeyframeLook.None
    }
    // Os keyframes do caminho aparecem na camada enquanto a máscara está aberta aqui.
    val maskId = m?.id
    DisposableEffect(store, store.primary, maskId, top) {
        val feather = store.timelineFocus?.any { it.property == TrackProperty.MASK_PARAM && it.effectIndex == maskId } == true
        if (!feather) store.timelineFocus = if (maskId != null && top != TAB_MATTE) listOf(TrackKey(TrackProperty.MASK_PATH, maskId, 0)) else null
        onDispose { store.timelineFocus = null }
    }
    val pathKeys = m?.let { mask ->
        store.primary?.let { id -> store.keyframes[id].orEmpty().filter { it.property == TrackProperty.MASK_PATH && it.effectIndex == mask.id } }
    }.orEmpty()
    val curveFrom = store.curveStart(pathKeys)
    val pen = top == TAB_POINTS && m != null
    // O modo de caneta usa o Editar pontos sobre o caminho desta máscara.
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
    val at = if (pen) EditPointsState.at(store) else null
    Row(Modifier.fillMaxSize()) {
        if (pen && m != null) {
            MaskPointsColumn(env, m, look)
            Box(Modifier.width(0.7.dp).fillMaxHeight().background(AureaColors.ContentStrongLine))
        } else LeftRail(
            onBack = env.onClose,
            keyframeLook = look,
            // O que anima na máscara é o CAMINHO: o losango marca ou tira a forma no cabeçote.
            onKeyframe = if (top == TAB_MASKS && m != null && m.closed && !drawing) ({ store.toggleMaskKey(m.id) }) else null,
            curveAnimated = pathKeys.isNotEmpty(),
            onCurve = if (top == TAB_MASKS && curveFrom != null) ({
                store.primary?.let { id ->
                    store.selectKeyframe(id, curveFrom)
                    env.onOpenPanel(EditorPanel.Curve)
                }
            }) else null,
        )
        Column(Modifier.weight(1f).fillMaxHeight()) {
            ParamTabs(listOf(stringResource(R.string.pn_mask_points), stringResource(R.string.panel_mascaras), stringResource(R.string.panel_recorte_outra_camada)), top, onSelect = { top = it })
            if (pen) {
                val masks = st?.masks.orEmpty()
                if (masks.size > 1) Box(Modifier.padding(horizontal = 6.dp)) { MaskChips(store, masks, m) }
                Trackpad(store, at, Modifier.weight(1f).fillMaxWidth())
            } else Column(Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()).padding(start = 4.dp, end = 10.dp, bottom = 16.dp)) {
                if (top == TAB_MATTE) TrackMatteTab(store) else MasksTab(env, st, m, drawing, showEditor = top == TAB_MASKS) { top = TAB_POINTS }
            }
        }
        if (pen) {
            Box(Modifier.width(0.7.dp).fillMaxHeight().background(AureaColors.ContentStrongLine))
            PointsStrip(store, at?.path, Modifier.width(60.dp).fillMaxHeight())
        }
    }
}

private const val TAB_POINTS = 0
private const val TAB_MASKS = 1
private const val TAB_MATTE = 2

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

@Composable
private fun MasksTab(env: PanelEnv, state: EditorStore.MaskState?, m: EditorStore.MaskPath?, drawing: Boolean, showEditor: Boolean, onCreated: () -> Unit) {
    val store = env.store
    val masks = state?.masks.orEmpty()
    Spacer(Modifier.height(6.dp))
    // Os três jeitos de criar uma máscara: sempre à vista, um toque cada.
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        ToolTile(stringResource(R.string.panel_desenhar_mao), on = drawing && m?.closed == false, icon = { drawMaskPenIcon(it) }) { store.startMaskDrawing(); onCreated() }
        ToolTile(stringResource(R.string.panel_retangulo), on = false, icon = { drawMaskShapeIcon(it, oval = false) }) { store.addMaskPreset(0); onCreated() }
        ToolTile(stringResource(R.string.panel_elipse), on = false, icon = { drawMaskShapeIcon(it, oval = true) }) { store.addMaskPreset(1); onCreated() }
    }
    if (masks.isEmpty()) {
        Spacer(Modifier.height(10.dp))
        KitHint(stringResource(R.string.panel_mascara_mostra_so_parte_camada_escolha))
        return
    }
    Spacer(Modifier.height(4.dp))
    MaskChips(store, masks, m)
    if (!showEditor) return
    if (m == null) {
        Spacer(Modifier.height(6.dp))
        KitHint(stringResource(R.string.panel_escolha_mascara_acima_editar_palco))
        return
    }
    MaskEditor(env, m, drawing)
}

/** A máscara escolhida, tudo numa tela só, na ordem de uso. */
@Composable
private fun MaskEditor(env: PanelEnv, m: EditorStore.MaskPath, drawing: Boolean) {
    val store = env.store
    val isVideo = store.layers.firstOrNull { it.id == store.primary }?.kind == LayerType.Video.kind
    KitTitle(stringResource(R.string.panel_caminho))
    if (drawing) {
        KitHint(stringResource(R.string.panel_toque_palco_pontos_arraste_curvar_toque))
        Spacer(Modifier.height(8.dp))
        ActionCard(stringResource(R.string.panel_fechar_caminho), pluralStringResource(R.plurals.pn_mask_points_so_far, m.count, m.count)) { store.closeMaskPath() }
        // Sem caminho fechado ainda não há modo/borda para ajustar.
        Spacer(Modifier.height(10.dp))
        ActionCard(stringResource(R.string.panel_apagar_mascara), null, danger = true) { store.deleteMask(m.id) }
        return
    }
    KitHint(stringResource(R.string.panel_arraste_pontos_palco_toque_num_ponto))
    val look = when {
        m.keyHere -> KeyframeLook.KeyHere
        m.keyCount > 0 -> KeyframeLook.Animated
        else -> KeyframeLook.None
    }
    PropertyCustomRow(stringResource(R.string.panel_caminho), selected = true, onSelect = { store.toggleMaskKey(m.id) }, keyframe = look) {
        Text(
            if (m.keyCount == 0) stringResource(R.string.panel_parado_toque_trilho_animar) else pluralStringResource(R.plurals.pn_keyframes_edit_at_playhead, m.keyCount, m.keyCount),
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)),
        )
    }

    val point = store.maskPoint
    if (point in 0 until m.count) {
        val curved = (2..5).any { m.points[point * 6 + it] != 0f }
        Spacer(Modifier.height(8.dp))
        ActionCard(stringResource(if (curved) R.string.panel_mask_point_corner else R.string.panel_mask_point_curve),
            stringResource(R.string.panel_mask_point_hint)) { store.toggleMaskPointCurve(m.id, point) }
    }

    KitTitle(stringResource(R.string.panel_mask_mode))
    val modes = listOf(0 to stringResource(R.string.panel_somar), 1 to stringResource(R.string.panel_subtrair), 2 to stringResource(R.string.panel_intersecao), 3 to stringResource(R.string.panel_diferenca), 4 to stringResource(R.string.panel_desligada))
    ChoiceChips(modes.map { it.second }, modes.indexOfFirst { it.first == m.op }, onSelect = { i ->
        store.setMaskProps(m.id, modes[i].first, m.inverted, m.feather, m.expansion, m.opacity)
    })
    ToggleLine(stringResource(R.string.panel_inverter_mostrar_lado_fora), m.inverted) { on ->
        store.setMaskProps(m.id, m.op, on, m.feather, m.expansion, m.opacity)
    }

    KitTitle(stringResource(R.string.panel_mask_edge))
    MaskRow(env, stringResource(R.string.panel_suavizar), m.id, 0, m.feather, 0.5f, 0f, 500f, "px", 0f)
    MaskRow(env, stringResource(R.string.panel_expandir), m.id, 1, m.expansion, 0.5f, -500f, 500f, "px", 0f)
    MaskRow(env, stringResource(R.string.panel_opacidade), m.id, 2, m.opacity * 100f, 0.5f, 0f, 100f, "%", 100f)

    if (isVideo) {
        KitTitle(stringResource(R.string.panel_mask_follow))
        if (store.maskTracking) {
            KitHint(stringResource(R.string.panel_rastreando))
        } else {
            KitHint(stringResource(R.string.panel_mascara_segue_esta_embaixo_dela_cabecote))
            Spacer(Modifier.height(8.dp))
            ActionCard(stringResource(R.string.panel_seguir_posicao), stringResource(R.string.panel_objetos_so_andam_pela_tela)) { store.trackMask(m.id, 0) }
            Spacer(Modifier.height(6.dp))
            ActionCard(stringResource(R.string.panel_seguir_posicao_tamanho_giro), stringResource(R.string.panel_objetos_aproximam_ou_giram)) { store.trackMask(m.id, 1) }
        }
    }
    Spacer(Modifier.height(14.dp))
    ActionCard(stringResource(R.string.panel_apagar_mascara), null, danger = true) { store.deleteMask(m.id) }
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

/** Suavizar / Expandir / Opacidade: um arrasto = um desfazer. */
@Composable
private fun MaskRow(env: PanelEnv, label: String, mask: Int, prop: Int, value: Float, step: Float, min: Float, max: Float, unit: String, default: Float) {
    val store = env.store
    val target = TrackKey(TrackProperty.MASK_PARAM, mask, prop)
    val keys = store.primaryKeys().filter { it.property == target.property && it.effectIndex == mask && it.paramIndex == prop }
    val local = store.detail?.localPlayhead
    val look = when { keys.any { it.time == local } -> KeyframeLook.KeyHere; keys.isNotEmpty() -> KeyframeLook.Animated; else -> KeyframeLook.None }
    fun write(v: Float) {
        store.timelineFocus = listOf(target)
        store.setMaskParam(mask, prop, if (prop == 2) v / 100f else v)
    }
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
        selected = store.timelineFocus == listOf(target),
        onSelect = { store.timelineFocus = listOf(target) },
        keyframe = look,
        onKeyframe = { store.timelineFocus = listOf(target); store.toggleMaskParamKey(mask, prop) },
        keyTag = "mask.$mask.key.$prop",
    )
    if (store.timelineFocus == listOf(target)) AnimationTrackActions(env, target, "mask.$mask.curve.$prop")
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
    KitTitle(stringResource(R.string.panel_1_recortar_pelo))
    KitHint(stringResource(R.string.pn_mask_target, me?.name.orEmpty()))
    val sourceLayer = rows.firstOrNull { it.id == matte }
    if (sourceLayer != null && mode != 0) {
        KitHint(stringResource(R.string.pn_mask_source, sourceLayer.name))
        MatteRelationDiagram(inverted = mode == 2 || mode == 4)
    }
    val pickBelowMsg = stringResource(R.string.pn_mask_pick_layer_below)
    val modes = listOf(0 to stringResource(R.string.panel_nao_recortar), 1 to stringResource(R.string.panel_pela_forma), 2 to stringResource(R.string.panel_pela_forma_invertido), 3 to stringResource(R.string.panel_pelo_brilho), 4 to stringResource(R.string.panel_pelo_brilho_invertido))
    ChoiceChips(modes.map { it.second }, modes.indexOfFirst { it.first == mode }, onSelect = { i ->
        val value = modes[i].first
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
    })
    KitTitle(stringResource(R.string.panel_2_qual_camada_recorta))
    if (candidates.isEmpty()) {
        KitHint(stringResource(R.string.panel_nao_ha_outra_camada_possa_recortar))
    } else {
        ChipRow {
            candidates.forEach { r ->
                KitChip(r.name.ifEmpty { stringResource(LayerType.of(r.kind).labelRes) }, matte == r.id) { store.setTrackMatte(r.id, if (mode == 0) 1 else mode) }
            }
        }
    }
    Spacer(Modifier.height(4.dp))
    KitHint(if (matte != 0L) stringResource(R.string.panel_camada_escolhida_recorta_esta_some_tela) else stringResource(R.string.panel_pela_forma_aparece_onde_outra_camada))
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
