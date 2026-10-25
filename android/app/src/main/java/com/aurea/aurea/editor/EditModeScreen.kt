package com.aurea.aurea.editor

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
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
import androidx.compose.foundation.shape.RoundedCornerShape
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
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

// =============================================================================
//  A INTERFACE DE EDIÇÃO (0.2.5) — a segunda interface do AUREA, escolhida por
//  projeto (a Composição continua a principal e o padrão).
//
//  Montagem de vídeo à moda dos editores de celular (estrutura especificada a
//  partir de engenharia reversa do CapCut 19.8.0, build/reference/capcut-19.8.0/
//  SPEC.md — nada do app de referência entra aqui): topo com ✕, resolução e
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
internal class EditDockItem(val id: String, val glyph: Char, val label: Int, val action: () -> Unit)

@Composable
internal fun EditModeEditor(store: EditorStore, ui: EditorUi, stage: @Composable (Modifier) -> Unit) {
    var level by remember { mutableStateOf(EditDockLevel.Root) }
    // O corte cujas transições estão abertas (saída, entrada).
    var cut by remember { mutableStateOf<Pair<Long, Long>?>(null) }
    val primary by remember { derivedStateOf { store.primary } }
    val selected = store.layers.firstOrNull { it.id == primary }
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
    androidx.activity.compose.BackHandler(enabled = level != EditDockLevel.Root && ui.panel == null && store.selection.isEmpty()) {
        level = EditDockLevel.Root
    }
    BoxWithConstraints(Modifier.fillMaxSize().background(EditColors.Canvas)) {
        val total = maxHeight
        Column(Modifier.fillMaxSize()) {
            if (!ui.fullscreen) EditTopBar(store, ui)
            val previewH = if (ui.fullscreen) total - 52.dp else (total * 0.40f).coerceIn(160.dp, 560.dp)
            KeepLtr { stage(Modifier.fillMaxWidth().height(previewH).background(EditColors.Preview)) }
            EditPlayBar(store, ui)
            if (!ui.fullscreen) {
                val panel = ui.panel
                if (panel != null) {
                    // Painel aberto: a linha do tempo encolhe e o painel da Composição sobe.
                    KeepLtr { EditTimeline(store, Modifier.fillMaxWidth().height(112.dp), compact = true, onAddMedia = actions.addMedia, onAddAudio = { level = EditDockLevel.Audio }, onTransition = { a, b -> cut = a to b }) }
                    Box(Modifier.fillMaxWidth().weight(1f).background(AureaColors.EditorPanel)) {
                        // Na Edição a barra de cima não mostra o título da seção: o painel
                        // traz o próprio cabeçalho "‹ Título" (o ✕ de cima fecha o projeto).
                        PanelContent(store, panel, onClose = { ui.panel = null }, onOpenPanel = { openPanel(store, ui, it) },
                            onOpenEffectsBrowser = { ui.effectsBrowser = true }, alwaysHeader = true)
                    }
                } else {
                    KeepLtr { EditTimeline(store, Modifier.fillMaxWidth().weight(1f), compact = false, onAddMedia = actions.addMedia, onAddAudio = { level = EditDockLevel.Audio }, onTransition = { a, b -> cut = a to b }) }
                    EditDock(level, actions.itemsFor(level, selected)) {
                        if (level in ClipLevels) store.clearSelection()
                        level = EditDockLevel.Root
                    }
                }
            }
        }
    }
    cut?.let { (a, b) -> CutTransitionSheet(store, a, b) { cut = null } }
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

/** Cores próprias da Edição: fundo preto como nos editores de vídeo, Dock um tom acima. */
internal object EditColors {
    val Canvas = androidx.compose.ui.graphics.Color(0xFF000000)
    val Preview = androidx.compose.ui.graphics.Color(0xFF0B0B0D)
    val Bar = androidx.compose.ui.graphics.Color(0xFF121214)
    val DockLevel = androidx.compose.ui.graphics.Color(0xFF1B1B1F)
    val Ruler = androidx.compose.ui.graphics.Color(0xFF8A8A92)
}

// -----------------------------------------------------------------------------
// Topo: ✕ · (espaço) · resolução · ⋯ · Exportar
// -----------------------------------------------------------------------------

@Composable
private fun EditTopBar(store: EditorStore, ui: EditorUi) {
    var menu by remember { mutableStateOf(false) }
    val close = stringResource(R.string.common_close)
    val more = stringResource(R.string.edit_more)
    Row(
        Modifier.fillMaxWidth().height(52.dp).background(EditColors.Canvas).padding(horizontal = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier.size(44.dp).testTag("edit.close").semantics { contentDescription = close; role = Role.Button }
                .tocavel(shrink = 1f) { store.closeProject() },
            contentAlignment = Alignment.Center,
        ) { CupertinoIcon(CupertinoGlyph.Xmark, 22.dp, AureaColors.Text) }
        Spacer(Modifier.weight(1f))
        // A resolução do projeto (o lado menor + "P"), que abre os ajustes do projeto.
        val p = store.project
        val short = minOf(p.width, p.height)
        Text(
            if (short > 0) "${short}P" else "—",
            style = AureaType.of(13f, FontWeight.W700),
            modifier = Modifier.clip(RoundedCornerShape(8.dp)).background(EditColors.DockLevel)
                .testTag("edit.resolution")
                .tocavel(shrink = 1f) { openSheet(store, ui, ShellSheet.ProjectSettings) }
                .padding(horizontal = 10.dp, vertical = 6.dp),
        )
        Box {
            Box(
                Modifier.size(44.dp).testTag("edit.more").semantics { contentDescription = more; role = Role.Button }
                    .tocavel(shrink = 1f) { menu = true },
                contentAlignment = Alignment.Center,
            ) { CupertinoIcon(CupertinoGlyph.Ellipsis, 20.dp, AureaColors.Text) }
            DropdownMenu(expanded = menu, onDismissRequest = { menu = false }) {
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
                    text = { Text(stringResource(R.string.editor_plano_fundo)) },
                    onClick = { menu = false; openSheet(store, ui, ShellSheet.ProjectSettings) },
                )
                // 0.3.1: o mesmo "Importar do After Effects" da Composição (o seletor mora
                // no AureaApp); vale a camada escolhida, ou o preset cria uma.
                DropdownMenuItem(
                    text = { Text(stringResource(R.string.ae_import_menu)) },
                    onClick = { menu = false; store.requestAeImport() },
                    modifier = Modifier.testTag("edit.aeImport"),
                )
            }
        }
        Spacer(Modifier.width(4.dp))
        Text(
            stringResource(R.string.edit_export),
            style = AureaType.of(14f, FontWeight.W700, color = AureaColors.Background),
            modifier = Modifier.clip(RoundedCornerShape(8.dp)).background(AureaColors.Accent)
                .testTag("edit.export").semantics { role = Role.Button }
                .tocavel(shrink = 0.96f) { if (store.playing) store.pause(); ui.exporting = true }
                .padding(horizontal = 14.dp, vertical = 8.dp),
        )
    }
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
    Box(Modifier.fillMaxWidth().height(48.dp).background(EditColors.Canvas).padding(horizontal = 12.dp)) {
        KeepLtr {
            Text(
                "${editClock(store.playhead, fps)} / ${editClock(store.project.durationFrames, fps)}",
                style = AureaType.of(12.5f, FontWeight.W500, color = AureaColors.Text),
                modifier = Modifier.align(Alignment.CenterStart).testTag("edit.clock"),
            )
        }
        Box(
            Modifier.align(Alignment.Center).size(44.dp).testTag("edit.play")
                .semantics { contentDescription = play; role = Role.Button }
                .tocavel(shrink = 0.9f) { store.togglePlayback() },
            contentAlignment = Alignment.Center,
        ) { CupertinoIcon(if (store.playing) CupertinoGlyph.PauseFill else CupertinoGlyph.PlayFill, 24.dp, AureaColors.Text) }
        Row(Modifier.align(Alignment.CenterEnd), verticalAlignment = Alignment.CenterVertically) {
            PlayBarIcon(CupertinoGlyph.ArrowUturnLeft, undo, store.project.canUndo, "edit.undo") { store.undo() }
            PlayBarIcon(CupertinoGlyph.ArrowUturnRight, redo, store.project.canRedo, "edit.redo") { store.redo() }
            PlayBarIcon(if (ui.fullscreen) CupertinoGlyph.FullscreenExit else CupertinoGlyph.Fullscreen, full, true, "edit.fullscreen") {
                ui.fullscreen = !ui.fullscreen
            }
        }
    }
}

@Composable
private fun PlayBarIcon(glyph: Char, label: String, enabled: Boolean, tag: String, onClick: () -> Unit) {
    Box(
        Modifier.size(40.dp).testTag(tag).semantics { contentDescription = label; role = Role.Button }
            .tocavel(enabled = enabled, shrink = 0.9f, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { CupertinoIcon(glyph, 20.dp, if (enabled) AureaColors.Text else AureaColors.Muted.copy(alpha = 0.4f)) }
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
    Row(
        Modifier.fillMaxWidth().height(76.dp)
            .background(if (level == EditDockLevel.Root) EditColors.Bar else EditColors.DockLevel)
            .testTag("edit.dock"),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (level != EditDockLevel.Root) {
            Box(
                Modifier.padding(start = 10.dp).width(32.dp).height(58.dp).clip(RoundedCornerShape(8.dp))
                    .background(EditColors.Bar).testTag("edit.dock.back")
                    .semantics { contentDescription = back; role = Role.Button }
                    .tocavel(shrink = 0.92f, onClick = onBack),
                contentAlignment = Alignment.Center,
            ) { CupertinoIcon(CupertinoGlyph.ChevronBack, 18.dp, AureaColors.Text) }
        }
        LazyRow(
            Modifier.weight(1f).fillMaxHeight().testTag("edit.dock.items"),
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
        Modifier.widthIn(min = 64.dp).height(64.dp).testTag("edit.dock." + item.id)
            .semantics(mergeDescendants = true) { contentDescription = label; role = Role.Button }
            .tocavel(shrink = 0.92f, haptic = true, onClick = item.action)
            .padding(horizontal = 6.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        CupertinoIcon(item.glyph, 24.dp, AureaColors.Text)
        Spacer(Modifier.height(6.dp))
        Text(label, style = AureaType.of(11f, color = AureaColors.Text), maxLines = 1, overflow = TextOverflow.Ellipsis,
            textAlign = TextAlign.Center)
    }
}

/** As ações do Dock e os seletores de mídia (que precisam viver numa composição). */
internal class EditActions(
    val addMedia: () -> Unit,
    val itemsFor: (EditDockLevel, LayerRow?) -> List<EditDockItem>,
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
                    EditDockItem("edit", CupertinoGlyph.Scissors, R.string.edit_dock_edit) {
                        val hit = clipUnderPlayhead()
                        if (hit != null) store.select(hit.id) else store.showToast(noClip)
                    },
                    EditDockItem("audio", CupertinoGlyph.MusicNote, R.string.edit_dock_audio) { setLevel(EditDockLevel.Audio) },
                    EditDockItem("text", CupertinoGlyph.Textformat, R.string.edit_dock_text) { setLevel(EditDockLevel.Text) },
                    EditDockItem("stickers", ShellGlyph.SquareOnCircle, R.string.edit_dock_stickers) { openAdd(store, ui, AddTab.Shape) },
                    EditDockItem("overlay", CupertinoGlyph.SquareOnSquare, R.string.edit_dock_overlay) {
                        if (store.playing) store.pause(); overlay.launch(visual)
                    },
                    EditDockItem("sequence", CupertinoGlyph.SquareStack3dUp, R.string.seq_dock) { sequence() },
                    EditDockItem("effects", CupertinoGlyph.Sparkles, R.string.edit_dock_effects) { globalEffect() },
                    EditDockItem("captions", CupertinoGlyph.CaptionsBubble, R.string.edit_dock_captions) { panel(EditorPanel.Captions) },
                    EditDockItem("filters", CupertinoGlyph.ColorFilter, R.string.edit_dock_filters) { globalColor(EditorPanel.Filters) },
                    EditDockItem("adjust", CupertinoGlyph.SliderHorizontal3, R.string.edit_dock_adjust) { globalColor(EditorPanel.Adjust) },
                    EditDockItem("ratio", CupertinoGlyph.Crop, R.string.edit_dock_ratio) { openSheet(store, ui, ShellSheet.ProjectSettings) },
                    EditDockItem("background", CupertinoGlyph.Paintbrush, R.string.edit_dock_background) { openSheet(store, ui, ShellSheet.ProjectSettings) },
                )
                EditDockLevel.Audio -> listOf(
                    EditDockItem("music", CupertinoGlyph.MusicNote2, R.string.edit_audio_music) {
                        if (store.playing) store.pause(); music.launch(arrayOf("audio/*"))
                    },
                    EditDockItem("voice", CupertinoGlyph.Mic, R.string.edit_audio_voice) { if (store.playing) store.pause(); ui.voice = true },
                    EditDockItem("extract", CupertinoGlyph.Waveform, R.string.edit_clip_extract_audio) {
                        val hit = clipUnderPlayhead()
                        if (hit != null) store.extractAudio(hit.id) else store.showToast(noClip)
                    },
                )
                EditDockLevel.Text -> listOf(
                    EditDockItem("addText", CupertinoGlyph.Textformat, R.string.edit_text_add) { openAdd(store, ui, AddTab.Text) },
                    EditDockItem("autoCaptions", CupertinoGlyph.CaptionsBubble, R.string.edit_text_auto_captions) { panel(EditorPanel.Captions) },
                    EditDockItem("templates", CupertinoGlyph.WandStars, R.string.edit_text_templates) { openAdd(store, ui, AddTab.TextPresets) },
                )
                EditDockLevel.VideoClip -> buildList {
                    val video = clip?.kind == 1
                    val main = clip != null && isMainTrack(store, clip)
                    add(EditDockItem("split", CupertinoGlyph.Scissors, R.string.edit_clip_split) { store.splitAtPlayhead() })
                    add(EditDockItem("speed", CupertinoGlyph.Speedometer, R.string.edit_clip_speed) { panel(EditorPanel.Speed) })
                    add(EditDockItem("animations", CupertinoGlyph.WandStars, R.string.edit_clip_animations) { panel(EditorPanel.Presets) })
                    add(EditDockItem("effects", CupertinoGlyph.Sparkles, R.string.edit_clip_effects) { effects() })
                    add(EditDockItem("filters", CupertinoGlyph.ColorFilter, R.string.edit_dock_filters) { clipColor(EditorPanel.Filters) })
                    add(EditDockItem("adjust", CupertinoGlyph.SliderHorizontal3, R.string.edit_dock_adjust) { clipColor(EditorPanel.Adjust) })
                    add(EditDockItem("delete", CupertinoGlyph.Trash, R.string.edit_clip_delete) { store.deleteLayers() })
                    add(EditDockItem("mask", ShellGlyph.SquareSplit2x2, R.string.edit_clip_mask) { panel(EditorPanel.Mask) })
                    if (video) add(EditDockItem("volume", CupertinoGlyph.Speaker2, R.string.edit_clip_volume) { panel(EditorPanel.Audio) })
                    add(EditDockItem("transform", CupertinoGlyph.Move, R.string.edit_clip_transform) { panel(EditorPanel.Transform) })
                    add(EditDockItem("opacity", CupertinoGlyph.CircleLefthalfFill, R.string.edit_clip_opacity) { panel(EditorPanel.Appearance) })
                    add(EditDockItem("duplicate", CupertinoGlyph.PlusSquareOnSquare, R.string.edit_clip_duplicate) { store.duplicateLayers() })
                    if (clip != null && clip.kind != 12) add(EditDockItem("replace", CupertinoGlyph.Arrow2Squarepath, R.string.edit_clip_replace) {
                        replaceTarget = clip.id; if (store.playing) store.pause(); replace.launch(visual)
                    })
                    if (video) add(EditDockItem("freeze", ShellGlyph.Snow, R.string.edit_clip_freeze) { store.freezeFrame() })
                    if (video) add(EditDockItem("extract", CupertinoGlyph.Waveform, R.string.edit_clip_extract_audio) { store.extractAudio() })
                    if (video) add(EditDockItem("stabilize", ShellGlyph.Viewfinder, R.string.edit_clip_stabilize) { panel(EditorPanel.Tracking) })
                    if (clip != null) {
                        if (main) add(EditDockItem("toOverlay", CupertinoGlyph.SquareOnSquare, R.string.edit_clip_to_overlay) { store.moveClipToOverlay(clip.id) })
                        else add(EditDockItem("toMain", CupertinoGlyph.RectangleStack, R.string.edit_clip_to_main) { store.moveClipToMainTrack(clip.id) })
                    }
                }
                EditDockLevel.TextClip -> listOf(
                    EditDockItem("editText", CupertinoGlyph.Pencil, R.string.edit_clip_edit_text) { panel(EditorPanel.Text) },
                    EditDockItem("font", CupertinoGlyph.Textformat, R.string.edit_clip_font) { panel(EditorPanel.Font) },
                    EditDockItem("animations", CupertinoGlyph.WandStars, R.string.edit_clip_animations) { panel(EditorPanel.Presets) },
                    EditDockItem("effects", CupertinoGlyph.Sparkles, R.string.edit_clip_effects) { effects() },
                    EditDockItem("transform", CupertinoGlyph.Move, R.string.edit_clip_transform) { panel(EditorPanel.Transform) },
                    EditDockItem("split", CupertinoGlyph.Scissors, R.string.edit_clip_split) { store.splitAtPlayhead() },
                    EditDockItem("duplicate", CupertinoGlyph.PlusSquareOnSquare, R.string.edit_clip_duplicate) { store.duplicateLayers() },
                    EditDockItem("delete", CupertinoGlyph.Trash, R.string.edit_clip_delete) { store.deleteLayers() },
                )
                EditDockLevel.AudioClip -> listOf(
                    EditDockItem("volume", CupertinoGlyph.Speaker2, R.string.edit_clip_volume) { panel(EditorPanel.Audio) },
                    EditDockItem("speed", CupertinoGlyph.Speedometer, R.string.edit_clip_speed) { panel(EditorPanel.Speed) },
                    EditDockItem("effects", CupertinoGlyph.Sparkles, R.string.edit_clip_effects) { effects() },
                    EditDockItem("split", CupertinoGlyph.Scissors, R.string.edit_clip_split) { store.splitAtPlayhead() },
                    EditDockItem("duplicate", CupertinoGlyph.PlusSquareOnSquare, R.string.edit_clip_duplicate) { store.duplicateLayers() },
                    EditDockItem("delete", CupertinoGlyph.Trash, R.string.edit_clip_delete) { store.deleteLayers() },
                )
                EditDockLevel.OtherClip -> listOf(
                    EditDockItem("effects", CupertinoGlyph.Sparkles, R.string.edit_clip_effects) { effects() },
                    EditDockItem("filters", CupertinoGlyph.ColorFilter, R.string.edit_dock_filters) { clipColor(EditorPanel.Filters) },
                    EditDockItem("adjust", CupertinoGlyph.SliderHorizontal3, R.string.edit_dock_adjust) { clipColor(EditorPanel.Adjust) },
                    EditDockItem("transform", CupertinoGlyph.Move, R.string.edit_clip_transform) { panel(EditorPanel.Transform) },
                    EditDockItem("opacity", CupertinoGlyph.CircleLefthalfFill, R.string.edit_clip_opacity) { panel(EditorPanel.Appearance) },
                    EditDockItem("mask", ShellGlyph.SquareSplit2x2, R.string.edit_clip_mask) { panel(EditorPanel.Mask) },
                    EditDockItem("split", CupertinoGlyph.Scissors, R.string.edit_clip_split) { store.splitAtPlayhead() },
                    EditDockItem("duplicate", CupertinoGlyph.PlusSquareOnSquare, R.string.edit_clip_duplicate) { store.duplicateLayers() },
                    EditDockItem("delete", CupertinoGlyph.Trash, R.string.edit_clip_delete) { store.deleteLayers() },
                )
            }
        }
        EditActions(addMedia, itemsFor)
    }
}

/** O corte está na trilha principal (a linha do trecho de mídia mais fundo)? */
internal fun isMainTrack(store: EditorStore, clip: LayerRow): Boolean {
    val anchor = store.layers.firstOrNull { it.id == store.mainTrackAnchor } ?: return false
    return clip.id == anchor.id || (anchor.trackId != 0 && clip.trackId == anchor.trackId)
}
