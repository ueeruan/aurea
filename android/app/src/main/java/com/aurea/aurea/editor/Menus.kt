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
 * O menu da camada, ancorado no ⋮ da barra da camada (o layout pai). Só o
 * RECIPIENTE mudou (folha → popup da referência): itens e ordem continuam os do
 * painel da camada. Alternâncias (travar, ocultar, solo, ajuste, guia, 3D)
 * redesenham no lugar sem fechar. O seletor de "Substituir mídia" e o diálogo
 * de "Informações da mídia" moram aqui, vivos enquanto a camada está escolhida.
 */
@Composable
internal fun LayerMenu(store: EditorStore, ui: EditorUi, id: Long, expanded: Boolean, onDismiss: () -> Unit) {
    val replacePicker = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri: Uri? ->
        if (uri != null) store.replaceMedia(id, uri)
    }
    var showInfo by remember(id) { mutableStateOf(false) }
    if (showInfo) MediaInfoDialog(store, id) { showInfo = false }
    if (!expanded) return
    val index = store.layers.indexOfFirst { it.id == id }
    val row = store.layers.getOrNull(index)
    if (row == null) {
        androidx.compose.runtime.LaunchedEffect(Unit) { onDismiss() }
        return
    }
    val entries = layerMenuEntries(
        store, ui, row, index,
        onReplace = { replacePicker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo)) },
        onInfo = { showInfo = true },
    )
    AnchoredMenu(expanded = true, onDismiss = onDismiss, entries = entries, alignEnd = true, tag = "layer.menu.popup")
}

/** As linhas do ⋮ da camada (as mesmas da antiga folha, na mesma ordem, + "Camada 3D"). */
@Composable
private fun layerMenuEntries(
    store: EditorStore,
    ui: EditorUi,
    row: LayerRow,
    index: Int,
    onReplace: () -> Unit,
    onInfo: () -> Unit,
): List<MenuEntry> {
    val id = row.id
    val type = LayerType.of(row.kind)
    val visual = type != LayerType.Audio
    val media = type == LayerType.Video || type == LayerType.Image
    val t = store.playhead
    val inside = t > row.startFrame && t < row.endFrame
    val out = mutableListOf<MenuEntry>()
    out += MenuEntry.Header(stringResource(R.string.editor_camada))
    if (type == LayerType.Video || type == LayerType.Audio) {
        out += MenuEntry.Item(stringResource(R.string.editor_velocidade), glyph = CupertinoGlyph.Speedometer) {
            openPanel(store, ui, EditorPanel.Speed)
        }
    }
    // 0.3.3 recursos: entradas da doca da 0.3.2 que ficaram sem lugar no painel da camada.
    // Opções de texto (espaçamentos, caixa, fundo, caminho, sombra e trechos ricos).
    if (type == LayerType.Text) {
        out += MenuEntry.Item(stringResource(R.string.text_options), glyph = ShellGlyph.SliderHorizontalBelowRectangle,
            tag = "layer.menu.textOptions") { openPanel(store, ui, EditorPanel.Text) }
    }
    // Fantoche (o "Rig" da doca da imagem): o cartão do efeito já no modo de pinos.
    if (type == LayerType.Image) {
        out += MenuEntry.Item(stringResource(R.string.fx_name_puppet), glyph = CupertinoGlyph.PersonCropCircle,
            enabled = !row.locked, tag = "layer.menu.puppet") {
            store.select(id)
            openPuppetTool(store, ui)
        }
    }
    // Alternâncias: o rótulo troca na hora (o menu fica aberto).
    out += MenuEntry.Item(
        stringResource(if (row.locked) R.string.editor_desbloquear_camada else R.string.editor_bloquear_camada),
        glyph = if (row.locked) ShellGlyph.LockOpenFill else CupertinoGlyph.LockFill,
        sublabel = stringResource(if (row.locked) R.string.editor_volta_aceitar_movimento_edicao else R.string.editor_nao_aceita_movimento_corte_nem_edicao),
        keepOpen = true,
    ) { store.setLayerLocked(id, !row.locked) }
    out += MenuEntry.Item(
        stringResource(if (row.visible) R.string.editor_ocultar_camada else R.string.editor_mostrar_camada),
        glyph = if (row.visible) CupertinoGlyph.EyeSlash else CupertinoGlyph.Eye,
        keepOpen = true,
    ) { store.setLayerVisible(id, !row.visible) }
    // Omega timeline (área 03, D-1): as trilhas de keyframes da camada (antes pela pílula).
    out += MenuEntry.Item(
        stringResource(if (id in store.timelineExpandedLayers) R.string.om_tl_hide_tracks else R.string.om_tl_show_tracks),
        glyph = CupertinoGlyph.Rhombus,
    ) { store.toggleTimelineTracks(id) }
    out += MenuEntry.Item(
        stringResource(R.string.editor_solo),
        glyph = CupertinoGlyph.Speaker2,
        sublabel = stringResource(R.string.editor_alguma_camada_solo_previa_som_so),
        checked = row.solo,
        keepOpen = true,
    ) { store.setLayerSolo(id, !row.solo) }
    // Omega chrome (área 02, P0-3): "Camada 3D" saiu da barra da camada e mora aqui.
    if (visual) {
        val intrinsic = row.kind in 8..10
        val threeD = row.isThreeD || intrinsic
        out += MenuEntry.Item(
            stringResource(R.string.sh_add_tab_3d),
            glyph = CupertinoGlyph.Cube,
            checked = threeD,
            enabled = !row.locked && !intrinsic,
            keepOpen = true,
            tag = "layer.menu.threeD",
        ) { store.changeLayer3D(!threeD) }
    }
    // Linha Magnética: só em trecho com FONTE (vídeo, áudio ou pré-composição).
    if (media || type == LayerType.Audio || type == LayerType.Group) {
        out += MenuEntry.Item(
            stringResource(R.string.editor_linha_magnetica),
            glyph = CupertinoGlyph.Link,
            sublabel = stringResource(R.string.editor_linha_magnetica_detalhe),
            checked = row.magnetic,
            keepOpen = true,
        ) { store.setLayerMagneticTrack(id, !row.magnetic) }
    }
    if (visual) {
        out += MenuEntry.Item(
            stringResource(R.string.editor_camada_ajuste),
            glyph = CupertinoGlyph.SliderHorizontal3,
            sublabel = stringResource(R.string.editor_efeitos_desta_camada_valem_todas_baixo),
            checked = row.adjustment,
            keepOpen = true,
        ) { store.setLayerAdjustment(id, !row.adjustment) }
        if (row.adjustment) {
            // Camadas afetadas: todas abaixo → só a logo abaixo → só as escolhidas.
            val scope = store.adjustmentScope
            out += MenuEntry.Item(
                stringResource(R.string.la_affected) + " · " + stringResource(
                    when (scope) {
                        1 -> R.string.la_affected_below
                        2 -> R.string.la_affected_chosen
                        else -> R.string.la_affected_all
                    },
                ),
                glyph = CupertinoGlyph.SliderHorizontal3,
                sublabel = stringResource(if (scope == 2) R.string.la_affected_chosen_hint else R.string.la_affected_hint),
                keepOpen = true,
            ) { store.changeAdjustmentScope((scope + 1) % 3) }
            if (scope == 2) {
                val below = store.layers.drop(index + 1).filter { LayerType.of(it.kind) != LayerType.Audio }
                if (below.isEmpty()) {
                    out += MenuEntry.Item(stringResource(R.string.la_affected_none_below), glyph = CupertinoGlyph.SliderHorizontal3, enabled = false)
                }
                below.forEach { b ->
                    out += MenuEntry.Item(b.name, glyph = CupertinoGlyph.SliderHorizontal3, checked = b.id in store.adjustmentTargets, keepOpen = true) {
                        store.toggleAdjustmentTarget(b.id)
                    }
                }
            }
        }
        out += MenuEntry.Item(
            stringResource(R.string.editor_guia_nao_exporta),
            glyph = CupertinoGlyph.Grid,
            sublabel = stringResource(R.string.editor_aparece_aqui_editor_fica_fora_video),
            checked = row.guide,
            keepOpen = true,
        ) { store.setLayerGuide(id, !row.guide) }
    }
    out += MenuEntry.Item(stringResource(R.string.editor_duplicar), glyph = CupertinoGlyph.PlusSquareOnSquare) { store.duplicateLayers(listOf(id)) }
    out += MenuEntry.Item(stringResource(R.string.editor_puxar_cabecote), glyph = CupertinoGlyph.ArrowDownToLine, enabled = !row.locked) {
        if (store.playing) store.pause()
        store.moveToPlayhead(id)
    }
    out += MenuEntry.Item(stringResource(R.string.editor_copiar_camada), glyph = CupertinoGlyph.DocOnDoc) { store.copyLayers(listOf(id)) }
    out += MenuEntry.Item(stringResource(R.string.editor_colar_camada_cabecote), glyph = CupertinoGlyph.DocOnClipboard, enabled = store.clipboard and 1 != 0) {
        store.pasteLayers()
    }
    out += MenuEntry.Item(stringResource(R.string.editor_copiar_estilo), glyph = CupertinoGlyph.Paintbrush) {
        store.select(id)
        store.copyStyle()
    }
    out += MenuEntry.Item(stringResource(R.string.editor_colar_estilo), glyph = ShellGlyph.PaintbrushFill, enabled = store.clipboard and 2 != 0) {
        store.pasteStyle(listOf(id))
    }
    out += MenuEntry.Item(stringResource(R.string.editor_trazer_frente), glyph = CupertinoGlyph.ArrowUpToLine, enabled = index > 0) {
        store.reorderLayer(id, index - 1)
    }
    out += MenuEntry.Item(stringResource(R.string.editor_enviar_tras), glyph = CupertinoGlyph.ArrowDownToLine, enabled = index < store.layers.size - 1) {
        store.reorderLayer(id, index + 1)
    }

    // Etiqueta: "sem" e as doze cores, numa fileira de ícones.
    out += MenuEntry.Header(stringResource(R.string.editor_etiqueta))
    val labelName = stringResource(R.string.editor_etiqueta)
    val none = stringResource(R.string.editor_nenhum)
    out += MenuEntry.IconRow(
        cells = listOf(MenuIconCell(none, glyph = ShellGlyph.Nosign) { store.setLayerLabel(id, 0) }) +
            ShellColors.LabelPalette.mapIndexed { i, color ->
                MenuIconCell("$labelName ${i + 1}", swatch = color) { store.setLayerLabel(id, i + 1) }
            },
        selectedIndex = row.label,
        keepOpen = true,
        tag = "layer.menu.labels",
    )

    if (visual) {
        out += MenuEntry.Header(stringResource(R.string.editor_grupo))
        if (row.clipToBelow || row.clipCandidate) {
            out += MenuEntry.Item(
                stringResource(if (row.clipToBelow) R.string.editor_liberar_mascara_recorte else R.string.editor_criar_mascara_recorte),
                glyph = CupertinoGlyph.ArrowDownRightSquare,
                sublabel = stringResource(R.string.editor_mascara_recorte_detalhe),
                tag = "layer.clipMask",
            ) { store.setLayerClipToBelow(id, !row.clipToBelow) }
        }
        if (type != LayerType.Group) {
            out += MenuEntry.Item(stringResource(R.string.editor_converter_grupo), glyph = CupertinoGlyph.RectangleStack) { store.precompose(listOf(id)) }
        } else {
            out += MenuEntry.Item(stringResource(R.string.editor_entrar_grupo), glyph = CupertinoGlyph.ArrowDownRightSquare) { store.openPrecomp(id) }
            out += MenuEntry.Item(stringResource(R.string.editor_desagrupar), glyph = ShellGlyph.SquareSplit2x2) { store.ungroupPrecomp(id) }
            out += MenuEntry.Item(
                stringResource(R.string.la_group_camera),
                glyph = CupertinoGlyph.Camera,
                sublabel = stringResource(R.string.la_group_camera_hint),
                checked = store.groupCameraPassThrough == 1,
                keepOpen = true,
            ) { store.changeGroupCameraPassThrough(store.groupCameraPassThrough != 1) }
        }
        if (store.layerAcceptsLights >= 0) {
            out += MenuEntry.Item(
                stringResource(R.string.la_accepts_lights),
                glyph = CupertinoGlyph.Lightbulb,
                sublabel = stringResource(R.string.la_accepts_lights_hint),
                checked = store.layerAcceptsLights == 1,
                keepOpen = true,
            ) { store.changeLayerAcceptsLights(store.layerAcceptsLights != 1) }
        }
        val addToGroup = stringResource(R.string.la_add_to_group)
        store.layers.filter { it.id != id && LayerType.of(it.kind) == LayerType.Group }.forEach { g ->
            out += MenuEntry.Item("$addToGroup · ${g.name}", glyph = CupertinoGlyph.RectangleStack) { store.addToGroup(g.id, listOf(id)) }
        }
        if (store.precompDepth > 0) {
            out += MenuEntry.Item(stringResource(R.string.la_remove_from_group), glyph = CupertinoGlyph.ArrowUturnLeft) { store.removeFromGroup(id) }
        }
    }
    if (media || type == LayerType.Audio) {
        out += MenuEntry.Header(stringResource(R.string.sh_menu_media))
        if (media) {
            out += MenuEntry.Item(
                stringResource(R.string.layer_replace_media),
                glyph = CupertinoGlyph.Arrow2Squarepath,
                sublabel = stringResource(R.string.layer_replace_media_detail),
                enabled = !row.locked,
            ) { onReplace() }
        }
        out += MenuEntry.Item(stringResource(R.string.media_info_title), glyph = CupertinoGlyph.InfoCircle, tag = "layer.menu.mediaInfo") { onInfo() }
        if (type == LayerType.Video) {
            out += MenuEntry.Item(
                stringResource(R.string.editor_extrair_audio),
                glyph = CupertinoGlyph.MusicNote2,
                sublabel = stringResource(R.string.editor_som_vira_camada_propria_video_fica),
            ) { store.extractAudio(id) }
        }
    }

    out += MenuEntry.Header(stringResource(R.string.editor_movimento))
    out += MenuEntry.Item(
        stringResource(R.string.editor_desfoque_movimento),
        glyph = CupertinoGlyph.MotionBlur,
        sublabel = stringResource(R.string.editor_borra_direcao_movimento_obturador_nas_configuracoes),
        checked = store.detail?.motionBlur == true,
        tag = "layer.menu.motionBlur",
    ) { store.setLayerMotionBlur(id, !(store.detail?.motionBlur ?: false)) }
    if (type == LayerType.Video) {
        out += MenuEntry.Item(
            stringResource(R.string.editor_desfoque_movimento_video),
            glyph = CupertinoGlyph.MotionBlur,
            sublabel = stringResource(R.string.editor_borra_mexe_dentro_video_pelos_vetores),
            checked = store.detail?.vectorBlur == true,
        ) { store.setVectorBlur(id, !(store.detail?.vectorBlur ?: false)) }
    }
    // 0.3.1: preset .ffx nesta camada / script .jsx com ela em comp.selectedLayers.
    out += MenuEntry.Item(
        stringResource(R.string.ae_import_menu),
        glyph = CupertinoGlyph.SquareStack3dUp,
        sublabel = stringResource(R.string.ae_import_layer_detail),
        enabled = !row.locked,
        tag = "layer.aeImport",
    ) { store.requestAeImport() }

    if (type == LayerType.Video || type == LayerType.Audio) {
        out += MenuEntry.Header(stringResource(R.string.editor_tempo))
        out += MenuEntry.Item(stringResource(R.string.i18n_clip_modes), glyph = CupertinoGlyph.Scissors) { openPanel(store, ui, EditorPanel.ClipEdit) }
    }
    if (type == LayerType.Video) {
        out += MenuEntry.Item(stringResource(R.string.sh_menu_freeze_frame), glyph = ShellGlyph.Snow, enabled = inside) { store.freezeFrame(id) }
        out += MenuEntry.Header(stringResource(R.string.editor_rastreio))
        out += MenuEntry.Item(
            stringResource(R.string.dock2_tracking_tools),
            glyph = ShellGlyph.Viewfinder,
            sublabel = stringResource(R.string.dock2_tracking_tools_detail),
        ) {
            store.select(id)
            store.cameraTrackerVisible = false
            openPanel(store, ui, EditorPanel.Tracking)
        }
        out += MenuEntry.Item(
            stringResource(R.string.editor_rastrear_ponto),
            glyph = ShellGlyph.Viewfinder,
            sublabel = stringResource(R.string.editor_cria_nulo_segue_ponto_ligue_outras),
        ) {
            store.select(id)
            openPanel(store, ui, EditorPanel.Tracking)
            store.beginPointPick(false)
        }
        out += MenuEntry.Item(
            stringResource(R.string.editor_estabilizar_pelo_ponto),
            glyph = ShellGlyph.Viewfinder,
            sublabel = stringResource(R.string.editor_move_video_ponto_ficar_parado_tela),
        ) {
            store.select(id)
            openPanel(store, ui, EditorPanel.Tracking)
            store.beginPointPick(true)
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
    }
}
