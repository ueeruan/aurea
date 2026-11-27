package com.aurea.aurea.editor

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Popup
import androidx.compose.ui.window.PopupProperties
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.editor.panels.PanelRequests
import com.aurea.aurea.editor.panels.TransformTab
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AnchoredMenu
import com.aurea.aurea.ui.ds.AnchoredMenuDefaults
import com.aurea.aurea.ui.ds.AureaSurfaceTone
import com.aurea.aurea.ui.ds.MenuColors
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.ds.MenuIconCell
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel

// =============================================================================
// ⋮ da camada — menu ancorado (Omega chrome, área 02 P1-1)
// =============================================================================

/**
 * O menu da camada, ancorado no ⋮ da barra da camada (o layout pai), só com
 * ações sobre a camada (Funções, 13/11/2026: [layerMenuActions]). Alternâncias
 * (travar, ocultar, solo)
 * redesenham no lugar sem fechar. O seletor de "Substituir mídia" e o diálogo
 * de "Informações da mídia" moram aqui, vivos enquanto a camada está escolhida.
 */
@Composable
internal fun LayerMenu(store: EditorStore, ui: EditorUi, id: Long, expanded: Boolean, onDismiss: () -> Unit) {
    val replacePicker = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri: Uri? ->
        if (uri != null) store.replaceMedia(id, uri)
    }
    val improvePicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) store.replaceMedia(id, uri, verifyUpgrade = true)
    }
    var showInfo by remember(id) { mutableStateOf(false) }
    if (showInfo) MediaInfoDialog(store, id) { showInfo = false }
    if (!expanded) return
    val index = store.layers.indexOfFirst { it.id == id }
    val row = store.layers.getOrNull(index) ?: store.editingLayers.firstOrNull { it.id == id }
    if (row == null) {
        androidx.compose.runtime.LaunchedEffect(Unit) { onDismiss() }
        return
    }
    // Os controles que saíram da barra também atendem filhos escolhidos inline.
    // As ações de composição mantêm o escopo da lista em que já funcionavam.
    val actions = if (index >= 0) layerMenuEntries(
        store, ui, row, index,
        onReplace = { replacePicker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo)) },
        onImproveSource = {
            if (store.playing) store.pause()
            improvePicker.launch(arrayOf("video/*"))
        },
        onInfo = { showInfo = true },
    ) else emptyList()
    val entries = layerSecondaryEntries(store, ui, row) + actions +
        com.aurea.aurea.effects.pluginLayerMenuEntries(store, includeToolbar = true, includeLayerMenu = index >= 0)
    if (entries.isEmpty()) {
        androidx.compose.runtime.LaunchedEffect(Unit) { onDismiss() }
        return
    }
    AnchoredMenu(expanded = true, onDismiss = onDismiss, entries = entries, alignEnd = true, tag = "layer.menu.popup")
}

@Composable
private fun layerSecondaryEntries(store: EditorStore, ui: EditorUi, row: LayerRow): List<MenuEntry> {
    val id = row.id
    val out = mutableListOf<MenuEntry>()
    if (row.kind != LayerType.Audio.kind) {
        val intrinsic = row.kind in 8..10
        val threeD = row.isThreeD || intrinsic
        val motionBlur = store.detail?.takeIf { it.id == id }?.motionBlur == true
        out += MenuEntry.Item(
            stringResource(R.string.sh_add_tab_3d),
            glyph = CupertinoGlyph.Cube,
            checked = threeD,
            enabled = !row.locked && !intrinsic,
            keepOpen = true,
            tag = "layer.menu.3d",
        ) { store.changeLayer3D(!threeD) }
        out += MenuEntry.Item(
            stringResource(R.string.editor_desfoque_movimento),
            glyph = CupertinoGlyph.MotionBlur,
            enabled = !row.locked,
            tag = "layer.menu.motionBlur",
        ) {
            PanelRequests.transformTab = TransformTab.Desfoque
            openPanel(store, ui, EditorPanel.Transform)
        }
        out += MenuEntry.Item(
            stringResource(R.string.fn_motion_blur_toggle),
            glyph = CupertinoGlyph.MotionBlur,
            checked = motionBlur,
            enabled = !row.locked,
            keepOpen = true,
            tag = "trial.layer.motionBlur.toggle",
        ) { store.setLayerMotionBlur(id, !motionBlur) }
    }
    return out
}

/** A ordem das ações da composição é definida por [layerMenuActions]. */
@Composable
private fun layerMenuEntries(
    store: EditorStore,
    ui: EditorUi,
    row: LayerRow,
    index: Int,
    onReplace: () -> Unit,
    onImproveSource: () -> Unit,
    onInfo: () -> Unit,
): List<MenuEntry> {
    val id = row.id
    val type = LayerType.of(row.kind)
    val convertFailed = stringResource(R.string.om_convert_outline_failed_035)   // bugs 0.3.4 (bug 11)
    val actions = layerMenuActions(LayerMenuInput(
        kind = row.kind,
        clipMask = row.clipToBelow || row.clipCandidate,
        canConvertOutline = type == LayerType.Shape && store.canConvertToOutline(id),
    ))
    val labelName = stringResource(R.string.editor_etiqueta)
    val none = stringResource(R.string.editor_nenhum)
    val flipH = stringResource(R.string.layer_flip_horizontal)
    val flipV = stringResource(R.string.layer_flip_vertical)
    val groupHeader = stringResource(R.string.editor_grupo)
    val convertToGroup = stringResource(R.string.editor_converter_grupo)
    val addToGroup = stringResource(R.string.la_add_to_group)
    val removeFromGroup = stringResource(R.string.la_remove_from_group)
    val mediaHeader = stringResource(R.string.sh_menu_media)
    val out = mutableListOf<MenuEntry>()
    out += MenuEntry.Header(stringResource(R.string.editor_camada))
    for (action in actions) {
        when (action) {
            // Alternâncias: o rótulo troca na hora (o menu fica aberto).
            LayerMenuAction.Lock -> out += MenuEntry.Item(
                stringResource(if (row.locked) R.string.editor_desbloquear_camada else R.string.editor_bloquear_camada),
                glyph = if (row.locked) ShellGlyph.LockOpenFill else CupertinoGlyph.LockFill,
                sublabel = stringResource(if (row.locked) R.string.editor_volta_aceitar_movimento_edicao else R.string.editor_nao_aceita_movimento_corte_nem_edicao),
                keepOpen = true,
            ) { store.setLayerLocked(id, !row.locked) }
            LayerMenuAction.Hide -> out += MenuEntry.Item(
                stringResource(if (row.visible) R.string.editor_ocultar_camada else R.string.editor_mostrar_camada),
                glyph = if (row.visible) CupertinoGlyph.EyeSlash else CupertinoGlyph.Eye,
                keepOpen = true,
            ) { store.setLayerVisible(id, !row.visible) }
            LayerMenuAction.Solo -> out += MenuEntry.Item(
                stringResource(R.string.editor_solo),
                glyph = CupertinoGlyph.Speaker2,
                sublabel = stringResource(R.string.editor_alguma_camada_solo_previa_som_so),
                checked = row.solo,
                keepOpen = true,
            ) { store.setLayerSolo(id, !row.solo) }
            // Omega timeline (área 03, D-1): as trilhas de keyframes da camada (antes pela pílula).
            LayerMenuAction.KeyframeTracks -> out += MenuEntry.Item(
                stringResource(if (id in store.timelineExpandedLayers) R.string.om_tl_hide_tracks else R.string.om_tl_show_tracks),
                glyph = CupertinoGlyph.Rhombus,
            ) { store.toggleTimelineTracks(id) }
            LayerMenuAction.Duplicate -> out += MenuEntry.Item(stringResource(R.string.editor_duplicar), glyph = CupertinoGlyph.PlusSquareOnSquare) {
                store.duplicateLayers(listOf(id))
            }
            LayerMenuAction.PullToPlayhead -> out += MenuEntry.Item(stringResource(R.string.editor_puxar_cabecote), glyph = CupertinoGlyph.ArrowDownToLine, enabled = !row.locked) {
                if (store.playing) store.pause()
                store.moveToPlayhead(id)
            }
            LayerMenuAction.BringForward -> out += MenuEntry.Item(stringResource(R.string.editor_trazer_frente), glyph = CupertinoGlyph.ArrowUpToLine, enabled = index > 0) {
                store.reorderLayer(id, index - 1)
            }
            LayerMenuAction.SendBackward -> out += MenuEntry.Item(stringResource(R.string.editor_enviar_tras), glyph = CupertinoGlyph.ArrowDownToLine, enabled = index < store.layers.size - 1) {
                store.reorderLayer(id, index + 1)
            }
            // Etiqueta: "sem" e as doze cores, numa fileira de ícones.
            LayerMenuAction.Labels -> {
                out += MenuEntry.Header(labelName)
                out += MenuEntry.IconRow(
                    cells = listOf(MenuIconCell(none, glyph = ShellGlyph.Nosign) { store.setLayerLabel(id, 0) }) +
                        ShellColors.LabelPalette.mapIndexed { i, color ->
                            MenuIconCell("$labelName ${i + 1}", swatch = color) { store.setLayerLabel(id, i + 1) }
                        },
                    selectedIndex = row.label,
                    keepOpen = true,
                    tag = "layer.menu.labels",
                )
            }
            // 0.4.6: espelhar (o "Flip" da referência); H e V numa fileira só.
            LayerMenuAction.Flip -> {
                out += MenuEntry.Header(groupHeader)
                out += MenuEntry.IconRow(
                    cells = listOf(
                        MenuIconCell(flipH, glyph = CupertinoGlyph.ArrowLeftRight, tag = "layer.flip.h") { store.flipLayer(id, vertical = false) },
                        MenuIconCell(flipV, glyph = CupertinoGlyph.ArrowUpArrowDown, tag = "layer.flip.v") { store.flipLayer(id, vertical = true) },
                    ),
                    tag = "layer.menu.flip",
                )
            }
            LayerMenuAction.ClipMask -> out += MenuEntry.Item(
                stringResource(if (row.clipToBelow) R.string.editor_liberar_mascara_recorte else R.string.editor_criar_mascara_recorte),
                glyph = CupertinoGlyph.ArrowDownRightSquare,
                sublabel = stringResource(R.string.editor_mascara_recorte_detalhe),
                tag = "layer.clipMask",
            ) { store.setLayerClipToBelow(id, !row.clipToBelow) }
            // Bugs 0.3.4 (bug 11): "Converter em contorno" — a forma viva vira vetor
            // (mesma silhueta, cor e traço) e abre o Editar pontos.
            LayerMenuAction.ConvertOutline -> out += MenuEntry.Item(
                stringResource(R.string.om_convert_outline_035),
                glyph = CupertinoGlyph.PencilOutline,
                sublabel = stringResource(R.string.om_convert_outline_detail_035),
                enabled = !row.locked,
                tag = "layer.menu.convertOutline",
            ) {
                if (store.primary != id) store.select(id)
                if (store.convertShapeToOutline(id)) openPanel(store, ui, EditorPanel.EditPoints)
                else store.showToast(convertFailed)
            }
            LayerMenuAction.EnterGroup -> {
                out += MenuEntry.Item(stringResource(R.string.editor_entrar_grupo), glyph = CupertinoGlyph.ArrowDownRightSquare) {
                    store.openPrecomp(id)
                }
                out += MenuEntry.Item(stringResource(R.string.prerender_group), glyph = CupertinoGlyph.Play,
                    tag = "layer.menu.group.prerender") { store.startPreRender(1, groupLayer = id) }
            }
            LayerMenuAction.Ungroup -> out += MenuEntry.Item(stringResource(R.string.editor_desagrupar), glyph = ShellGlyph.SquareSplit2x2) {
                store.ungroupPrecomp(id)
            }
            // "Grupo ›": converter, pôr num grupo que já existe aqui, tirar do grupo aberto.
            LayerMenuAction.GroupMenu -> {
                val sub = buildList<MenuEntry> {
                    if (type != LayerType.Group) add(MenuEntry.Item(convertToGroup, glyph = CupertinoGlyph.RectangleStack, tag = "layer.menu.group.convert") {
                        store.precompose(listOf(id))
                    })
                    store.layers.filter { it.id != id && LayerType.of(it.kind) == LayerType.Group }.forEach { g ->
                        add(MenuEntry.Item("$addToGroup · ${g.name}", glyph = CupertinoGlyph.RectangleStack) { store.addToGroup(g.id, listOf(id)) })
                    }
                    if (store.precompDepth > 0) add(MenuEntry.Item(removeFromGroup, glyph = CupertinoGlyph.ArrowUturnLeft, tag = "layer.menu.group.remove") {
                        store.removeFromGroup(id)
                    })
                }
                if (sub.isNotEmpty()) out += MenuEntry.Item(
                    if (type == LayerType.Group) addToGroup else convertToGroup,
                    glyph = CupertinoGlyph.RectangleStack,
                    submenu = sub,
                    tag = "layer.menu.group",
                )
            }
            LayerMenuAction.ReplaceMedia -> {
                out += MenuEntry.Header(mediaHeader)
                val replace = MenuEntry.Item(
                    stringResource(R.string.layer_replace_media),
                    glyph = CupertinoGlyph.Arrow2Squarepath,
                    sublabel = stringResource(R.string.layer_replace_media_detail),
                    enabled = !row.locked,
                ) { onReplace() }
                if (type == LayerType.Video) {
                    val better = MenuEntry.Item(
                        stringResource(R.string.layer_improve_source),
                        glyph = CupertinoGlyph.Sparkles,
                        sublabel = stringResource(R.string.layer_improve_source_detail),
                        enabled = !row.locked,
                        tag = "layer.menu.improveSource",
                    ) { onImproveSource() }
                    out += MenuEntry.Item(stringResource(R.string.layer_replace_media), glyph = CupertinoGlyph.Arrow2Squarepath,
                        submenu = listOf(better, replace), enabled = !row.locked)
                } else out += replace
            }
            // 0.4.5: Remover fundo — o efeito na camada e, no vídeo, a análise do clipe.
            LayerMenuAction.RemoveBackground -> out += MenuEntry.Item(
                stringResource(R.string.bg_remove_action),
                glyph = CupertinoGlyph.PersonCropCircle,
                sublabel = stringResource(R.string.bg_remove_action_detail),
                enabled = !row.locked,
                tag = "layer.menu.bgRemove",
            ) { if (store.removeBackground(id)) openPanel(store, ui, EditorPanel.Effects) }
            LayerMenuAction.MediaInfo -> {
                if (type == LayerType.Audio) out += MenuEntry.Header(mediaHeader)
                out += MenuEntry.Item(stringResource(R.string.media_info_title), glyph = CupertinoGlyph.InfoCircle, tag = "layer.menu.mediaInfo") { onInfo() }
            }
            LayerMenuAction.ExtractAudio -> out += MenuEntry.Item(
                stringResource(R.string.editor_extrair_audio),
                glyph = CupertinoGlyph.MusicNote2,
                sublabel = stringResource(R.string.editor_som_vira_camada_propria_video_fica),
            ) { store.extractAudio(id) }
        }
    }
    return out
}

// =============================================================================
// Selecionar uma camada
// =============================================================================

/**
 * Linhas do "Selecionar uma camada": sem busca, TODAS as camadas na ordem da
 * timeline (a da frente primeiro); com busca, os acertos do motor (nome ou
 * texto, sem maiúscula nem acento) na mesma ordem.
 */
internal fun layerPickerRows(layers: List<LayerRow>, query: String, hits: List<Long>): List<LayerRow> {
    if (query.isBlank()) return layers
    val byId = layers.associateBy { it.id }
    return hits.mapNotNull { byId[it] }
}

/** A busca só aparece quando a lista não cabe de uma olhada. */
internal const val LAYER_PICKER_SEARCH_FROM = 7

/**
 * Omega chrome (área 02, P1-1): "Selecionar uma camada…" como popup ancorado
 * (no Copiar e colar do transporte), com a busca no topo quando há 7 camadas
 * ou mais. Tocar numa escolhe SÓ ela e fecha.
 */
@Composable
internal fun LayerPickerPopup(store: EditorStore, onDismiss: () -> Unit) {
    val density = LocalDensity.current
    val provider = remember(density) {
        with(density) { AnchorPopupPosition(AnchoredMenuDefaults.ScreenMargin.roundToPx(), AnchoredMenuDefaults.AnchorGap.roundToPx()) }
    }
    val colors = MenuColors.of(AureaSurfaceTone.Light)
    var query by remember { mutableStateOf("") }
    val hits = remember(query, store.layers) { store.searchLayers(query) }
    val rows = layerPickerRows(store.layers, query, hits)
    Popup(popupPositionProvider = provider, onDismissRequest = onDismiss, properties = PopupProperties(focusable = true)) {
        Column(
            Modifier
                .width(AnchoredMenuDefaults.Width)
                .heightIn(max = 420.dp)
                .clip(RoundedCornerShape(AnchoredMenuDefaults.Radius))
                .background(colors.background)
                .testTag("layerPicker.popup")
                .padding(vertical = AnchoredMenuDefaults.VerticalPadding),
        ) {
            Text(
                stringResource(R.string.editor_selecionar_uma_camada),
                style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = colors.muted)),
                modifier = Modifier.padding(start = 14.dp, end = 14.dp, top = 6.dp, bottom = 4.dp),
            )
            if (store.layers.size >= LAYER_PICKER_SEARCH_FROM || query.isNotEmpty()) {
                Box(
                    Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 10.dp, vertical = 4.dp)
                        .heightIn(min = 36.dp)
                        .clip(RoundedCornerShape(6.dp))
                        .background(colors.pressed)
                        .padding(horizontal = 10.dp, vertical = 9.dp),
                ) {
                    BasicTextField(
                        value = query,
                        onValueChange = { query = it },
                        singleLine = true,
                        textStyle = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = colors.text)),
                        cursorBrush = SolidColor(colors.accent),
                        modifier = Modifier.fillMaxWidth().testTag("layerPicker.search"),
                    )
                    if (query.isEmpty()) {
                        Text(stringResource(R.string.editor_nome_ou_texto_camada), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = colors.muted)))
                    }
                }
            }
            if (query.isNotBlank() && rows.isEmpty()) {
                Text(
                    stringResource(R.string.sh_menu_no_layer_matches, query.trim()),
                    modifier = Modifier.padding(horizontal = 14.dp, vertical = 10.dp),
                    style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = colors.muted)),
                )
            }
            Column(Modifier.verticalScroll(rememberScrollState())) {
                rows.forEach { row ->
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .height(48.dp)
                            .testTag("layerPicker.row.${row.id}")
                            .background(if (row.selected) colors.pressed else androidx.compose.ui.graphics.Color.Transparent)
                            .semantics {
                                contentDescription = row.name
                                selected = row.selected
                            }
                            .tocavel(shrink = 1f, haptic = true) {
                                onDismiss()
                                store.select(row.id)
                            }
                            .padding(horizontal = 10.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        LayerThumb(store, row, 32.dp)
                        Spacer(Modifier.width(10.dp))
                        Text(
                            row.name.ifBlank { stringResource(R.string.editor_camada) },
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                            modifier = Modifier.weight(1f),
                            style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = colors.text)),
                        )
                        val label = row.label
                        if (label in 1..ShellColors.LabelPalette.size) {
                            Spacer(Modifier.width(8.dp))
                            Box(Modifier.size(10.dp).background(ShellColors.LabelPalette[label - 1], CircleShape))
                        }
                        if (row.selected) {
                            Spacer(Modifier.width(8.dp))
                            CupertinoIcon(CupertinoGlyph.CheckmarkAlt, 16.dp, colors.accent)
                        }
                    }
                }
            }
        }
    }
}

/**
 * SELECIONAR UMA CAMADA em folha: o caminho da busca de ferramentas (sem botão
 * por perto para ancorar). Tocar numa escolhe SÓ ela e fecha a folha.
 */
@Composable
internal fun SearchLayersSheet(store: EditorStore, onDismiss: () -> Unit) {
    var query by remember { mutableStateOf("") }
    val hits = remember(query, store.layers) { store.searchLayers(query) }
    val rows = layerPickerRows(store.layers, query, hits)
    ShellMenuSheet(onDismiss, maxHeightFraction = 0.7f) {
        MenuSection(stringResource(R.string.editor_selecionar_uma_camada))
        if (store.layers.size >= LAYER_PICKER_SEARCH_FROM || query.isNotEmpty()) {
            Box(
                Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp)
                    .heightIn(min = 40.dp).clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip)
                    .padding(horizontal = 12.dp, vertical = 10.dp),
            ) {
                BasicTextField(
                    value = query,
                    onValueChange = { query = it },
                    singleLine = true,
                    textStyle = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text)),
                    cursorBrush = SolidColor(AureaColors.Accent),
                    modifier = Modifier.fillMaxWidth(),
                )
                if (query.isEmpty()) Text(stringResource(R.string.editor_nome_ou_texto_camada), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Muted)))
            }
        }
        if (query.isNotBlank() && rows.isEmpty()) {
            Text(
                stringResource(R.string.sh_menu_no_layer_matches, query.trim()),
                modifier = Modifier.padding(horizontal = 20.dp, vertical = 12.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)),
            )
        }
        rows.forEach { row ->
            Row(
                Modifier.fillMaxWidth().height(56.dp)
                    .tocavel(onClick = { store.select(row.id); onDismiss() })
                    .semantics { contentDescription = row.name }
                    .padding(horizontal = 16.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                LayerThumb(store, row)
                Spacer(Modifier.width(12.dp))
                Text(
                    row.name.ifBlank { stringResource(R.string.editor_camada) },
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f),
                    style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = if (row.selected) AureaColors.Accent else AureaColors.Text)),
                )
                val label = row.label
                if (label in 1..ShellColors.LabelPalette.size) {
                    Spacer(Modifier.width(8.dp))
                    Box(Modifier.size(10.dp).background(ShellColors.LabelPalette[label - 1], CircleShape))
                }
                if (row.selected) {
                    Spacer(Modifier.width(8.dp))
                    CupertinoIcon(CupertinoGlyph.CheckmarkAlt, 16.dp, AureaColors.Accent)
                }
            }
        }
    }
}

/** Group commands use the shared engine's stacking order, regardless of tap order. */
@Composable
internal fun GroupSelectionSheet(store: EditorStore, onDismiss: () -> Unit) {
    fun action(block: () -> Unit): () -> Unit = { onDismiss(); block() }
    ShellMenuSheet(onDismiss) {
        MenuSection(stringResource(R.string.editor_grupo))
        MenuItemRow(CupertinoGlyph.RectangleStack, stringResource(R.string.editor_agrupar_camadas_escolhidas),
            if (store.selection.isNotEmpty()) action { store.precompose() } else null)
        MenuItemRow(CupertinoGlyph.Crop, stringResource(R.string.editor_group_mask),
            if (store.selection.size >= 2) action { store.groupAndMask(false) } else null,
            detail = stringResource(R.string.editor_group_mask_hint))
        MenuItemRow(CupertinoGlyph.Crop, stringResource(R.string.editor_group_exclude),
            if (store.selection.size >= 2) action { store.groupAndMask(true) } else null,
            detail = stringResource(R.string.editor_group_exclude_hint))
        // Bug #6: desagrupar também mora aqui (com algum grupo na seleção).
        val groups = store.layers.filter { it.id in store.selection && LayerType.of(it.kind) == LayerType.Group }.map { it.id }
        MenuItemRow(ShellGlyph.SquareSplit2x2, stringResource(R.string.editor_desagrupar),
            if (groups.isNotEmpty()) action { store.ungroupPrecomps(groups) } else null)
    }
}
