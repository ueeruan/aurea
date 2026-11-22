package com.aurea.aurea.editor

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.zIndex
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.editor.panels.PanelContent
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.UI_MODE_COMPOSITION
import com.aurea.aurea.ui.i18n.KeepLtr
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaShape
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel

// =============================================================================
//  A INTERFACE DE EDIÇÃO — a segunda interface do AUREA, escolhida por
//  projeto (a Composição continua a principal e o padrão).
//
//  Montagem de vídeo à moda dos editores de celular (estrutura especificada a
//  partir da referência observada via REA em 15/11/2026,
//  build/reference/capcut-ui-20261115/): topo com ✕, resolução e
//  Exportar; a prévia; a barra de reprodução; a linha do tempo com o cabeçote
//  fixo no meio e a TRILHA PRINCIPAL magnética; e o Dock embaixo, em níveis —
//  o nível de cima tem as ferramentas do projeto e um corte escolhido troca o
//  Dock pelas ações dele, com ‹ (32 × 58 dp, margem de 10 dp) para voltar.
//
//  As regras de montagem são do motor (linha magnética, ripple, reordenar,
//  aparar, dividir); esta tela só as aciona. Os painéis são os da Composição.
// =============================================================================

/** Os níveis do Dock. [Root] = nada escolhido; os `*Clip` seguem o tipo do corte. */
internal enum class EditDockLevel { Root, Audio, Text, VideoClip, TextClip, AudioClip, OtherClip }

/** Um botão do Dock: ícone, rótulo e o que ele faz. [id] vira `edit.dock.<id>`. */
internal class EditDockItem(val id: String, val icon: EditIcon, val label: Int, val action: () -> Unit)

@Composable
internal fun EditModeEditor(store: EditorStore, ui: EditorUi, stage: @Composable (Modifier) -> Unit) {
    var level by remember { mutableStateOf(EditDockLevel.Root) }
    LaunchedEffect(Unit) { ViewModes.columnOpen = false }
    // O corte cujas transições estão abertas (saída, entrada).
    var cut by remember { mutableStateOf<Pair<Long, Long>?>(null) }
    val primary by remember { derivedStateOf { store.primary } }
    val selected = store.editingLayers.firstOrNull { it.id == primary }
    // O corte escolhido manda no Dock; sem corte, volta ao nível de cima (ou
    // fica no submenu que a pessoa abriu, como Áudio ou Texto).
    LaunchedEffect(primary, selected?.kind) {
        level = when {
            selected == null -> if (level in ClipLevels) EditDockLevel.Root else level
            selected.kind == 4 -> EditDockLevel.TextClip
            selected.kind == 3 -> EditDockLevel.AudioClip
            selected.kind == 1 || selected.kind == 2 || selected.kind == 12 -> EditDockLevel.VideoClip
            else -> EditDockLevel.OtherClip
        }
    }
    val actions = rememberEditActions(store, ui) { level = it }
    // Voltar do sistema num submenu do Dock (Áudio, Texto) volta ao nível de cima;
    // painel, seleção e o resto seguem a ordem da casca (`shellBack`).
    androidx.activity.compose.BackHandler(enabled = level != EditDockLevel.Root && ui.panel == null && store.editingSelection.isEmpty()) {
        level = EditDockLevel.Root
    }
    BoxWithConstraints(Modifier.fillMaxSize().background(EditColors.Canvas)) {
        val total = maxHeight
        val wide = maxWidth >= 600.dp && maxWidth > maxHeight && !ui.fullscreen && store.textEditLayer == null
        val texting = store.textEditLayer != null && !ui.fullscreen
        val panel = ui.panel
        val inspector: @Composable () -> Unit = {
            if (panel != null) {
                PanelContent(store, panel, onClose = { ui.panel = null }, onOpenPanel = { openPanel(store, ui, it) },
                    onOpenEffectsBrowser = { ui.effectsBrowser = true }, alwaysHeader = true)
            } else {
                Column(Modifier.fillMaxSize(), verticalArrangement = Arrangement.Bottom) {
                    EditDock(level, actions.itemsFor(level, selected)) {
                        if (level in ClipLevels) store.clearSelection()
                        level = EditDockLevel.Root
                    }
                }
            }
        }
        Column(Modifier.fillMaxSize()) {
            if (texting) Box(Modifier.fillMaxWidth().height(52.dp).zIndex(2f)) { TextStyleBar(store) }
            else if (!ui.fullscreen) EditTopBar(store, ui)
            if (wide) {
                Row(Modifier.fillMaxWidth().weight(1f)) {
                    Column(Modifier.weight(1f).fillMaxHeight().background(EditColors.Preview)) {
                        KeepLtr {
                            Box(Modifier.fillMaxWidth().weight(1f).padding(horizontal = 8.dp)
                                .clip(RoundedCornerShape(12.dp)).background(EditColors.Preview)) {
                                stage(Modifier.fillMaxSize().testTag("edit.preview"))
                            }
                        }
                        EditPlayBar(store, ui)
                    }
                    Column(Modifier.weight(1f).fillMaxHeight().background(EditColors.Canvas)) {
                        KeepLtr { EditTimeline(store, Modifier.fillMaxWidth().weight(1f), compact = panel != null,
                            onAddMedia = actions.addMedia, onTransition = { a, b -> cut = a to b }, onAddAudio = actions.addAudio, onAddText = actions.addText) }
                        if (panel != null) {
                            Box(Modifier.fillMaxWidth().height((total * .58f).coerceAtMost(320.dp)).testTag("edit.inspector")) { inspector() }
                        } else EditDock(level, actions.itemsFor(level, selected)) {
                            if (level in ClipLevels) store.clearSelection()
                            level = EditDockLevel.Root
                        }
                    }
                }
            } else {
            // The native surface remains in this slot as selection and panels change.
            KeepLtr {
                Box(Modifier.fillMaxWidth().weight(1f).padding(horizontal = 8.dp)
                    .clip(RoundedCornerShape(12.dp)).background(EditColors.Preview)) {
                    stage(Modifier.fillMaxSize().testTag("edit.preview"))
                }
            }
            if (texting) {
                TextEditPanel(store, Modifier.fillMaxWidth().weight(1f))
            } else {
            EditPlayBar(store, ui)
            if (!ui.fullscreen) {
                if (panel != null) {
                    val panelH = (total * .36f).coerceIn(160.dp, 330.dp)
                    KeepLtr { EditTimeline(store, Modifier.fillMaxWidth().height(96.dp), compact = true, onAddMedia = actions.addMedia, onTransition = { a, b -> cut = a to b }, onAddAudio = actions.addAudio, onAddText = actions.addText) }
                    Box(Modifier.fillMaxWidth().height(panelH).background(AureaColors.EditorPanel)) {
                        inspector()
                    }
                } else {
                    KeepLtr { EditTimeline(store, Modifier.fillMaxWidth().height((total * .30f).coerceIn(134.dp, 260.dp)), compact = false, onAddMedia = actions.addMedia, onTransition = { a, b -> cut = a to b }, onAddAudio = actions.addAudio, onAddText = actions.addText) }
                    EditDock(level, actions.itemsFor(level, selected)) {
                        if (level in ClipLevels) store.clearSelection()
                        level = EditDockLevel.Root
                    }
                }
            }
            }
            }
        }
    }
    cut?.let { (a, b) -> CutTransitionSheet(store, a, b) { cut = null } }
    // Omega conteúdo (área 06, P2-15): "Fonte" do Dock abre o navegador de fontes.
    if (FontBrowserRequest.open) {
        FontBrowserDialog(store, onPick = { f ->
            store.applyFontAnywhere(f)
            FontPrefs.recordUse(f)
            FontBrowserRequest.open = false
        }, onDismiss = { FontBrowserRequest.open = false })
    }
}

/** Os tipos de transição do motor (0 nenhuma … 5 girar), na ordem dos botões. */
private val CutTransitions = listOf(
    R.string.edit_transition_none, R.string.edit_transition_dissolve, R.string.edit_transition_slide_up,
    R.string.edit_transition_slide_left, R.string.edit_transition_zoom, R.string.edit_transition_spin,
)

/** As transições de UM corte da trilha principal (meio segundo de cada lado). */
@Composable
private fun CutTransitionSheet(store: EditorStore, outgoing: Long, incoming: Long, onDismiss: () -> Unit) {
    var current by remember(incoming) { mutableStateOf(store.cutTransition(incoming)) }
    com.aurea.aurea.ui.ds.AureaModalSheet(onDismiss = onDismiss) {
        Column(Modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, bottom = 24.dp)) {
            Text(stringResource(R.string.edit_transition), style = AureaType.of(16f, FontWeight.W700),
                modifier = Modifier.padding(bottom = 12.dp).semantics { heading() })
            LazyRow(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                items(CutTransitions.size) { type ->
                    val active = type == current
                    val label = stringResource(CutTransitions[type])
                    Box(
                        Modifier.widthIn(min = 76.dp).height(56.dp).clip(RoundedCornerShape(12.dp))
                            .background(if (active) AureaColors.AccentDim else AureaColors.SurfaceHigh)
                            .testTag("edit.transition.$type")
                            .semantics { contentDescription = label; role = Role.RadioButton; this.selected = active }
                            .tocavel(shrink = 0.95f, haptic = true) {
                                if (store.setCutTransition(outgoing, incoming, type)) current = type
                            }
                            .padding(horizontal = 12.dp),
                        contentAlignment = Alignment.Center,
                    ) {
                        Text(label, style = AureaType.of(13f, FontWeight.W600, color = if (active) AureaColors.Accent else AureaColors.Text),
                            textAlign = TextAlign.Center, maxLines = 2)
                    }
                }
            }
        }
    }
}

private val ClipLevels = setOf(EditDockLevel.VideoClip, EditDockLevel.TextClip, EditDockLevel.AudioClip, EditDockLevel.OtherClip)

/** Editing chrome follows the same live Aurea palette as the rest of the app. */
internal object EditColors {
    val Canvas get() = AureaColors.Background
    val Preview get() = AureaColors.EditorCanvas
    val Bar get() = AureaColors.EditorTopBar
    val DockLevel get() = AureaColors.EditorRowPill
    val Ruler get() = AureaColors.Muted
}

// -----------------------------------------------------------------------------
// Aurea project header: back, title, resolution, secondary actions and export.
// -----------------------------------------------------------------------------

@Composable
private fun EditTopBar(store: EditorStore, ui: EditorUi) {
    var menu by remember { mutableStateOf(false) }
    var renaming by remember { mutableStateOf(false) }
    var name by remember { mutableStateOf(androidx.compose.ui.text.input.TextFieldValue("")) }
    val close = stringResource(R.string.common_close)
    val more = stringResource(R.string.edit_more)
    val export = stringResource(R.string.edit_export)
    Row(
        Modifier.fillMaxWidth().height(50.dp).background(EditColors.Bar).padding(horizontal = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier.size(44.dp).testTag("edit.close").semantics { contentDescription = close; role = Role.Button }
                .tocavel(shrink = 1f) { store.closeProject() },
            contentAlignment = Alignment.Center,
        ) { EditToolIcon(EditIcon.Back, AureaColors.Text, 22.dp) }
        Text(store.project.title, style = AureaType.of(13f, FontWeight.W600, color = AureaColors.Text),
            maxLines = 1, overflow = TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f).padding(horizontal = 4.dp).testTag("edit.title"))
        // A resolução do projeto (o lado menor + "P"), que abre os ajustes do projeto.
        val p = store.project
        val short = minOf(p.width, p.height)
        Box(
            modifier = Modifier.height(44.dp).widthIn(min = 58.dp)
                .testTag("edit.resolution")
                .semantics { role = Role.Button; contentDescription = "${if (short > 0) "${short}P" else ""} $more" }
                .tocavel(shrink = 1f) { openSheet(store, ui, ShellSheet.ProjectSettings) },
            contentAlignment = Alignment.Center,
        ) {
            Text(if (short > 0) "${short}P" else "—", style = AureaType.of(12f, FontWeight.W600),
                modifier = Modifier.clip(RoundedCornerShape(10.dp)).background(EditColors.DockLevel)
                    .padding(horizontal = 8.dp, vertical = 5.dp), maxLines = 1)
        }
        Box {
            Box(
                Modifier.size(44.dp).testTag("edit.more").semantics { contentDescription = more; role = Role.Button }
                    .tocavel(shrink = 1f) { menu = true },
                contentAlignment = Alignment.Center,
            ) { EditToolIcon(EditIcon.More, AureaColors.Text, 20.dp) }
            DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.fxl_view)) },
                    onClick = { menu = false; ViewModes.columnOpen = !ViewModes.columnOpen },
                    modifier = Modifier.testTag("edit.previewControls"),
                )
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.common_rename)) },
                    onClick = {
                        menu = false
                        name = androidx.compose.ui.text.input.TextFieldValue(store.project.title)
                        renaming = true
                    },
                    modifier = Modifier.testTag("edit.rename"),
                )
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.ui_mode_switch_to_composition)) },
                    onClick = { menu = false; store.setUiMode(UI_MODE_COMPOSITION) },
                    modifier = Modifier.testTag("edit.toComposition"),
                )
                DropdownMenuItem(
                    text = { Text(stringResource(if (store.editMode) R.string.edit_magnet_on else R.string.edit_magnet_off)) },
                    onClick = { menu = false; store.toggleEditMode() },
                )
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.editor_ajustes_projeto)) },
                    onClick = { menu = false; openSheet(store, ui, ShellSheet.ProjectSettings) },
                )
                DropdownMenuItem(
                    text = { Text(stringResource(if (store.autoKeyTransforms) R.string.i18n_autokey_on else R.string.i18n_autokey_off)) },
                    onClick = { menu = false; store.autoKeyTransforms = !store.autoKeyTransforms },
                    modifier = Modifier.testTag("edit.autokey"),
                )
                // 0.3.1: o mesmo "Importar do After Effects" da Composição (o seletor mora
                // no AureaApp); vale a camada escolhida, ou o preset cria uma.
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.ae_import_menu)) },
                    onClick = { menu = false; store.requestAeImport() },
                    modifier = Modifier.testTag("edit.aeImport"),
                )
                val pluginEntries = com.aurea.aurea.effects.pluginEditorMenuEntries(store)
                pluginEntries.filterIsInstance<com.aurea.aurea.ui.ds.MenuEntry.Item>().forEach { entry ->
                    DropdownMenuItem(
                        text = { Text(entry.label) },
                        enabled = entry.enabled,
                        modifier = entry.tag?.let { Modifier.testTag(it) } ?: Modifier,
                        onClick = { menu = false; if (store.playing) store.pause(); entry.onClick() },
                    )
                }
            }
        }
        Spacer(Modifier.width(4.dp))
        Box(
            Modifier.size(44.dp)
                .testTag("edit.export").semantics { contentDescription = export; role = Role.Button }
                .tocavel(shrink = 0.96f) { if (store.playing) store.pause(); ui.exporting = true },
            contentAlignment = Alignment.Center,
        ) { EditToolIcon(EditIcon.Export, AureaColors.Accent, 23.dp) }
    }
    if (renaming) com.aurea.aurea.ui.ds.AureaAlert(
        title = stringResource(R.string.common_rename),
        confirmLabel = stringResource(R.string.common_save),
        onConfirm = { name.text.trim().takeIf { it.isNotEmpty() }?.let(store::renameProject); renaming = false },
        onDismiss = { renaming = false },
        extra = {
            com.aurea.aurea.ui.ds.AureaDialogField(value = name, onValueChange = { name = it }, tag = "edit.rename.field")
        },
    )
}

// -----------------------------------------------------------------------------
// Barra de reprodução: tempo · ▶ · desfazer · refazer · tela cheia
// -----------------------------------------------------------------------------

@Composable
private fun EditPlayBar(store: EditorStore, ui: EditorUi) {
    val fps = store.project.fps.takeIf { it > 0f } ?: 30f
    val play = stringResource(if (store.playing) R.string.editor_pausar else R.string.editor_reproduzir)
    val undo = stringResource(R.string.common_undo)
    val redo = stringResource(R.string.common_redo)
    val full = stringResource(R.string.editor_tela_cheia)
    val fontScale = androidx.compose.ui.platform.LocalDensity.current.fontScale.coerceIn(1f, 2f)
    Box(Modifier.fillMaxWidth().height((40f + (fontScale - 1f) * 16f).dp).background(AureaColors.EditorBar).padding(horizontal = 8.dp)) {
        KeepLtr {
            Box(Modifier.align(Alignment.CenterStart).testTag("edit.clock").clip(RoundedCornerShape(8.dp))
                .background(AureaColors.EditorRowPill).padding(horizontal = 8.dp, vertical = 4.dp)) {
                if (fontScale > 1.25f) Column {
                    Text(editClock(store.playhead, fps), style = AureaType.of(11f, FontWeight.W500, color = AureaColors.Text))
                    Text("/ ${editClock(store.project.durationFrames, fps)}", style = AureaType.of(9f, color = AureaColors.Muted))
                } else Text(
                    "${editClock(store.playhead, fps)} / ${editClock(store.project.durationFrames, fps)}",
                    style = AureaType.of(11f, FontWeight.W500, color = AureaColors.Text),
                )
            }
        }
        Box(
            Modifier.align(Alignment.Center).size(40.dp).background(AureaColors.AccentDim, CircleShape).testTag("edit.play")
                .semantics { contentDescription = play; role = Role.Button }
                .tocavel(shrink = 0.9f) { store.togglePlayback() },
            contentAlignment = Alignment.Center,
        ) { EditToolIcon(if (store.playing) EditIcon.Pause else EditIcon.Play, AureaColors.Accent, 24.dp) }
        Row(Modifier.align(Alignment.CenterEnd), verticalAlignment = Alignment.CenterVertically) {
            PlayBarIcon(EditIcon.Undo, undo, store.project.canUndo, "edit.undo") { store.undo() }
            PlayBarIcon(EditIcon.Redo, redo, store.project.canRedo, "edit.redo") { store.redo() }
            PlayBarIcon(if (ui.fullscreen) EditIcon.FullscreenExit else EditIcon.Fullscreen, full, true, "edit.fullscreen") {
                ui.fullscreen = !ui.fullscreen
            }
        }
    }
}

@Composable
private fun PlayBarIcon(icon: EditIcon, label: String, enabled: Boolean, tag: String, onClick: () -> Unit) {
    Box(
        Modifier.size(30.dp, 40.dp).testTag(tag).semantics { contentDescription = label; role = Role.Button }
            .tocavel(enabled = enabled, shrink = 0.9f, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { EditToolIcon(icon, if (enabled) AureaColors.Text else AureaColors.Muted.copy(alpha = 0.4f), 20.dp) }
}

/** "mm:ss" do quadro (o relógio da barra de reprodução). */
internal fun editClock(frame: Int, fps: Float): String {
    val seconds = (frame.coerceAtLeast(0) / fps.coerceAtLeast(1f)).toInt()
    return "%02d:%02d".format(java.util.Locale.ROOT, seconds / 60, seconds % 60)
}

// -----------------------------------------------------------------------------
// O Dock em níveis
// -----------------------------------------------------------------------------

@Composable
private fun EditDock(level: EditDockLevel, items: List<EditDockItem>, onBack: () -> Unit) {
    val back = stringResource(R.string.edit_back)
    val scroll = rememberLazyListState()
    LaunchedEffect(level) { scroll.scrollToItem(0) }
    Row(
        Modifier.fillMaxWidth().height((80f + (androidx.compose.ui.platform.LocalDensity.current.fontScale.coerceIn(1f, 2f) - 1f) * 30f).dp)
            .testTag("edit.dock").padding(8.dp).clip(RoundedCornerShape(22.dp))
            .background(AureaColors.EditorRowPill).border(1.dp, AureaColors.EditorBarLine, RoundedCornerShape(22.dp)).padding(4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (level != EditDockLevel.Root) {
            Box(
                Modifier.padding(start = 10.dp).width(32.dp).height(58.dp)
                    .testTag("edit.dock.back")
                    .semantics { contentDescription = back; role = Role.Button }
                    .tocavel(shrink = 0.92f, onClick = onBack),
                contentAlignment = Alignment.Center,
            ) { EditToolIcon(EditIcon.Back, AureaColors.Text, 18.dp) }
        }
        LazyRow(
            Modifier.weight(1f).fillMaxHeight().testTag("edit.dock.items"),
            state = scroll,
            contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            items(items, key = { it.id }) { item -> EditDockButton(item) }
        }
    }
}

@Composable
private fun EditDockButton(item: EditDockItem) {
    val label = stringResource(item.label)
    Column(
        Modifier.widthIn(min = 62.dp).fillMaxHeight().testTag("edit.dock." + item.id)
            .semantics(mergeDescendants = true) { contentDescription = label; role = Role.Button }
            .tocavel(shrink = 0.92f, haptic = true, onClick = item.action)
            .padding(horizontal = 6.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        EditToolIcon(item.icon, AureaColors.Text, 24.dp)
        Spacer(Modifier.height(6.dp))
        Text(label, style = AureaType.of(12f, FontWeight.W500, color = AureaColors.Text), maxLines = 1, overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center)
    }
}

/** As ações do Dock e os seletores de mídia (que precisam viver numa composição). */
internal class EditActions(
    val addMedia: () -> Unit,
    val itemsFor: (EditDockLevel, LayerRow?) -> List<EditDockItem>,
    val addAudio: (() -> Unit)? = null,
    val addText: (() -> Unit)? = null,
)

@Composable
private fun rememberEditActions(store: EditorStore, ui: EditorUi, setLevel: (EditDockLevel) -> Unit): EditActions {
    // "+" da trilha principal: fotos e vídeos entram no fim dela (regra do motor no modo Edição).
    val media = rememberLauncherForActivityResult(ActivityResultContracts.PickMultipleVisualMedia()) { uris: List<Uri> ->
        if (uris.isNotEmpty()) store.importMediaBatch(uris)
    }
    // "Camada": a mídia vai para uma linha própria, solta, no cabeçote.
    val overlay = rememberLauncherForActivityResult(ActivityResultContracts.PickMultipleVisualMedia()) { uris: List<Uri> ->
        if (uris.isNotEmpty()) store.importMediaBatch(uris, asOverlay = true)
    }
    val music = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) store.importAudio(uri)
    }
    var replaceTarget by remember { mutableStateOf(0L) }
    val replace = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri: Uri? ->
        if (uri != null && replaceTarget != 0L) store.replaceMedia(replaceTarget, uri)
    }
    val visual = PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo)
    // 0.3.0: sequência de imagens (render do Blender com alfa) numa linha própria, no
    // cabeçote, como a "Camada": é para compor por cima do vídeo.
    val sequence = rememberImageSequencePicker(store, asOverlay = true)
    val noClip = stringResource(R.string.edit_no_clip_here)
    return remember(store, ui, noClip) {
        val addMedia = { if (store.playing) store.pause(); media.launch(visual) }
        fun panel(p: EditorPanel) = openPanel(store, ui, p)
        // 0.2.7: Efeitos abre a PILHA da camada (EffectsPanel): os cartões com os
        // parâmetros de cada efeito aplicado e o "+ Adicionar efeito". Antes abria
        // só o navegador de adicionar, e o efeito escolhido não tinha onde ser
        // ajustado sem trocar para a Composição. Camada sem efeito abre direto na
        // lista de adicionar e volta aos cartões depois da escolha.
        fun effects() { if (store.playing) store.pause(); panel(EditorPanel.Effects) }
        fun clipUnderPlayhead(): LayerRow? {
            val anchor = store.layers.firstOrNull { it.id == store.mainTrackAnchor } ?: return null
            return store.layers.firstOrNull {
                (it.id == anchor.id || (anchor.trackId != 0 && it.trackId == anchor.trackId)) &&
                    store.playhead >= it.startFrame && store.playhead < it.endFrame
            }
        }
        /** A camada de ajuste do projeto sob o cabeçote (reaproveitada) ou uma nova. */
        fun globalAdjustment() {
            val existing = store.layers.lastOrNull {
                it.adjustment && store.playhead >= it.startFrame && store.playhead < it.endFrame
            }
            if (existing != null) store.select(existing.id) else store.addAdjustmentLayer()
        }
        fun globalEffect() {
            // 0.3.0: Efeitos, Filtros e Ajustar têm cada um a sua tela (antes os três
            // abriam o mesmo editor). Efeito do projeto: a camada de ajuste por cima
            // e o NAVEGADOR de efeitos direto; escolhido o efeito, a pilha abre.
            val empty = store.layers.firstOrNull {
                it.adjustment && it.effectCount == 0 && store.playhead >= it.startFrame && store.playhead < it.endFrame
            }
            if (empty != null) store.select(empty.id) else store.addAdjustmentLayer()
            if (store.playing) store.pause()
            ui.effectsBrowser = true
        }
        /** Filtros / Ajustar do projeto: a mesma camada de ajuste, na tela própria. */
        fun globalColor(p: EditorPanel) { if (store.playing) store.pause(); globalAdjustment(); panel(p) }
        fun clipColor(p: EditorPanel) { if (store.playing) store.pause(); panel(p) }
        val itemsFor: (EditDockLevel, LayerRow?) -> List<EditDockItem> = { level, clip ->
            when (level) {
                EditDockLevel.Root -> listOf(
                    EditDockItem("edit", EditIcon.Razor, R.string.edit_dock_edit) {
                        val hit = clipUnderPlayhead()
                        if (hit != null) store.select(hit.id) else store.showToast(noClip)
                    },
                    EditDockItem("audio", EditIcon.Waveform, R.string.edit_dock_audio) { setLevel(EditDockLevel.Audio) },
                    EditDockItem("text", EditIcon.Text, R.string.edit_dock_text) { setLevel(EditDockLevel.Text) },
                    EditDockItem("effects", EditIcon.Effects, R.string.edit_dock_effects) { globalEffect() },
                    EditDockItem("overlay", EditIcon.Overlay, R.string.edit_dock_overlay) {
                        if (store.playing) store.pause(); overlay.launch(visual)
                    },
                    EditDockItem("captions", EditIcon.Captions, R.string.edit_dock_captions) { panel(EditorPanel.Captions) },
                    EditDockItem("filters", EditIcon.Filters, R.string.edit_dock_filters) { globalColor(EditorPanel.Filters) },
                    EditDockItem("adjust", EditIcon.Adjust, R.string.edit_dock_adjust) { globalColor(EditorPanel.Adjust) },
                    EditDockItem("ratio", EditIcon.Crop, R.string.edit_dock_ratio) { openSheet(store, ui, ShellSheet.ProjectSettings) },
                    EditDockItem("background", EditIcon.Background, R.string.edit_dock_background) { openSheet(store, ui, ShellSheet.ProjectSettings) },
                    EditDockItem("stickers", EditIcon.Shapes, R.string.edit_dock_stickers) { openAdd(store, ui, AddTab.Shape) },
                    EditDockItem("sequence", EditIcon.Sequence, R.string.seq_dock) { sequence() },
                    EditDockItem("templates", EditIcon.Presets, R.string.edit_text_templates) { openAdd(store, ui, AddTab.TextPresets) },
                )
                EditDockLevel.Audio -> listOf(
                    EditDockItem("music", EditIcon.Music, R.string.edit_audio_music) {
                        if (store.playing) store.pause(); music.launch(arrayOf("audio/*"))
                    },
                    EditDockItem("voice", EditIcon.Voice, R.string.edit_audio_voice) { if (store.playing) store.pause(); ui.voice = true },
                    EditDockItem("extract", EditIcon.Extract, R.string.edit_clip_extract_audio) {
                        val hit = clipUnderPlayhead()
                        if (hit != null) store.extractAudio(hit.id) else store.showToast(noClip)
                    },
                )
                EditDockLevel.Text -> listOf(
                    EditDockItem("addText", EditIcon.Text, R.string.edit_text_add) { openAdd(store, ui, AddTab.Text) },
                    EditDockItem("autoCaptions", EditIcon.Captions, R.string.edit_text_auto_captions) { panel(EditorPanel.Captions) },
                    EditDockItem("templates", EditIcon.Presets, R.string.edit_text_templates) { openAdd(store, ui, AddTab.TextPresets) },
                )
                EditDockLevel.VideoClip -> buildList {
                    val video = clip?.kind == 1
                    val main = clip != null && isMainTrack(store, clip)
                    add(EditDockItem("split", EditIcon.Razor, R.string.edit_clip_split) { store.splitAtPlayhead() })
                    add(EditDockItem("speed", EditIcon.Speed, R.string.edit_clip_speed) { panel(EditorPanel.Speed) })
                    add(EditDockItem("animations", EditIcon.Presets, R.string.edit_clip_animations) { panel(EditorPanel.Presets) })
                    add(EditDockItem("effects", EditIcon.Effects, R.string.edit_clip_effects) { effects() })
                    add(EditDockItem("delete", EditIcon.Delete, R.string.edit_clip_delete) { store.deleteLayers() })
                    add(EditDockItem("mask", EditIcon.Mask, R.string.edit_clip_mask) { panel(EditorPanel.Mask) })
                    add(EditDockItem("adjust", EditIcon.Adjust, R.string.edit_dock_adjust) { clipColor(EditorPanel.Adjust) })
                    add(EditDockItem("filters", EditIcon.Filters, R.string.edit_dock_filters) { clipColor(EditorPanel.Filters) })
                    if (video) add(EditDockItem("volume", EditIcon.Speaker, R.string.edit_clip_volume) { panel(EditorPanel.Audio) })
                    add(EditDockItem("transform", EditIcon.Transform, R.string.edit_clip_transform) { panel(EditorPanel.Transform) })
                    add(EditDockItem("opacity", EditIcon.Opacity, R.string.edit_clip_opacity) { panel(EditorPanel.Appearance) })
                    add(EditDockItem("duplicate", EditIcon.Duplicate, R.string.edit_clip_duplicate) { store.duplicateLayers() })
                    if (clip != null && clip.kind != 12) add(EditDockItem("replace", EditIcon.Replace, R.string.edit_clip_replace) {
                        replaceTarget = clip.id; if (store.playing) store.pause(); replace.launch(visual)
                    })
                    // 0.4.5: Remover fundo (vídeo/imagem): efeito + análise do clipe.
                    if (clip != null && (clip.kind == 1 || clip.kind == 2)) add(EditDockItem("bgRemove", EditIcon.Cutout, R.string.bg_remove_action) {
                        if (store.removeBackground(clip.id)) panel(EditorPanel.Effects)
                    })
                    if (video) add(EditDockItem("freeze", EditIcon.Freeze, R.string.edit_clip_freeze) { store.freezeFrame() })
                    if (video) add(EditDockItem("extract", EditIcon.Extract, R.string.edit_clip_extract_audio) { store.extractAudio() })
                    if (video) add(EditDockItem("stabilize", EditIcon.Track, R.string.edit_clip_stabilize) { panel(EditorPanel.Tracking) })
                    if (clip != null) {
                        if (main) add(EditDockItem("toOverlay", EditIcon.Overlay, R.string.edit_clip_to_overlay) { store.moveClipToOverlay(clip.id) })
                        else add(EditDockItem("toMain", EditIcon.MainTrack, R.string.edit_clip_to_main) { store.moveClipToMainTrack(clip.id) })
                    }
                }
                EditDockLevel.TextClip -> listOf(
                    // Omega conteúdo (área 06, P2-15): "Editar texto" abre o MODO DE TEXTO
                    // (dá para mudar as palavras) e "Fonte" o navegador de fontes.
                    EditDockItem("editText", EditIcon.Pencil, R.string.edit_clip_edit_text) { store.openTextEdit() },
                    EditDockItem("font", EditIcon.Font, R.string.edit_clip_font) { FontBrowserRequest.open = true },
                    EditDockItem("animations", EditIcon.Presets, R.string.edit_clip_animations) { panel(EditorPanel.Presets) },
                    EditDockItem("effects", EditIcon.Effects, R.string.edit_clip_effects) { effects() },
                    EditDockItem("transform", EditIcon.Transform, R.string.edit_clip_transform) { panel(EditorPanel.Transform) },
                    EditDockItem("split", EditIcon.Razor, R.string.edit_clip_split) { store.splitAtPlayhead() },
                    EditDockItem("duplicate", EditIcon.Duplicate, R.string.edit_clip_duplicate) { store.duplicateLayers() },
                    EditDockItem("delete", EditIcon.Delete, R.string.edit_clip_delete) { store.deleteLayers() },
                )
                EditDockLevel.AudioClip -> listOf(
                    EditDockItem("volume", EditIcon.Speaker, R.string.edit_clip_volume) { panel(EditorPanel.Audio) },
                    EditDockItem("speed", EditIcon.Speed, R.string.edit_clip_speed) { panel(EditorPanel.Speed) },
                    EditDockItem("effects", EditIcon.Effects, R.string.edit_clip_effects) { effects() },
                    EditDockItem("split", EditIcon.Razor, R.string.edit_clip_split) { store.splitAtPlayhead() },
                    EditDockItem("duplicate", EditIcon.Duplicate, R.string.edit_clip_duplicate) { store.duplicateLayers() },
                    EditDockItem("delete", EditIcon.Delete, R.string.edit_clip_delete) { store.deleteLayers() },
                )
                EditDockLevel.OtherClip -> buildList {
                    // Omega conteúdo (área 06, P2-15): forma → Editar forma; vetor →
                    // Editar pontos; desenho → o editor de desenho.
                    if (clip?.kind == com.aurea.aurea.ui.theme.LayerType.Shape.kind) {
                        when {
                            store.isDrawingLayer -> add(EditDockItem("editDrawing", EditIcon.Draw, R.string.om_draw_title) { panel(EditorPanel.Drawing) })
                            store.isVectorLayer -> add(EditDockItem("editPoints", EditIcon.Vector, R.string.om_pts_title) { panel(EditorPanel.EditPoints) })
                            else -> add(EditDockItem("editShape", EditIcon.ShapeEdit, R.string.sh_dock_edit_shape) { panel(EditorPanel.ShapeEdit) })
                        }
                    }
                    addAll(listOf(
                    EditDockItem("effects", EditIcon.Effects, R.string.edit_clip_effects) { effects() },
                    EditDockItem("filters", EditIcon.Filters, R.string.edit_dock_filters) { clipColor(EditorPanel.Filters) },
                    EditDockItem("adjust", EditIcon.Adjust, R.string.edit_dock_adjust) { clipColor(EditorPanel.Adjust) },
                    EditDockItem("transform", EditIcon.Transform, R.string.edit_clip_transform) { panel(EditorPanel.Transform) },
                    EditDockItem("opacity", EditIcon.Opacity, R.string.edit_clip_opacity) { panel(EditorPanel.Appearance) },
                    EditDockItem("mask", EditIcon.Mask, R.string.edit_clip_mask) { panel(EditorPanel.Mask) },
                    EditDockItem("split", EditIcon.Razor, R.string.edit_clip_split) { store.splitAtPlayhead() },
                    EditDockItem("duplicate", EditIcon.Duplicate, R.string.edit_clip_duplicate) { store.duplicateLayers() },
                    EditDockItem("delete", EditIcon.Delete, R.string.edit_clip_delete) { store.deleteLayers() },
                    ))
                }
            }
        }
        EditActions(addMedia, itemsFor, { setLevel(EditDockLevel.Audio) }, { openAdd(store, ui, AddTab.Text) })
    }
}

/** O corte está na trilha principal (a linha do trecho de mídia mais fundo)? */
internal fun isMainTrack(store: EditorStore, clip: LayerRow): Boolean {
    val anchor = store.layers.firstOrNull { it.id == store.mainTrackAnchor } ?: return false
    return clip.id == anchor.id || (anchor.trackId != 0 && clip.trackId == anchor.trackId)
}
