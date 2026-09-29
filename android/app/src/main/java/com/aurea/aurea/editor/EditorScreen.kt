package com.aurea.aurea.editor

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBars
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsBottomHeight
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.layout.windowInsetsTopHeight
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.movableContentOf
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.listSaver
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.editor.panels.EffectsBrowserSheet
import com.aurea.aurea.editor.panels.PanelContent
import com.aurea.aurea.editor.timeline.Timeline
import com.aurea.aurea.editor.timeline.TimecodeStyle
import com.aurea.aurea.engine.KeyframeRow
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaNamePrompt
import com.aurea.aurea.ui.i18n.KeepLtr
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

// =============================================================================
// Estado de APRESENTAÇÃO da casca
// =============================================================================

/** Folhas e diálogos que a casca abre (um por vez). */
internal enum class ShellSheet { LayerMenu, RenameLayer, TimelineMenu, ProjectSettings, CopyPaste, GoToTime, SearchLayers, CommandSearch }

/**
 * O que está aberto na casca. Nada aqui é do projeto: o motor não sabe que
 * existe um painel aberto ou uma tela cheia. Valores do projeto (seleção,
 * transform, tempo) vêm SEMPRE do [EditorStore].
 */
@Stable
internal class EditorUi {
    var panel by mutableStateOf<EditorPanel?>(null)
    var adding by mutableStateOf(false)
    var addTab by mutableStateOf(AddTab.Shape)
    var fullscreen by mutableStateOf(false)
    var effectsBrowser by mutableStateOf(false)
    var exporting by mutableStateOf(false)
    var sheet by mutableStateOf<ShellSheet?>(null)

    /** Um dedo manipula algo no palco: o transporte vira a barra de informações. */
    var manipulating by mutableStateOf(false)

    /** Linha de encaixe ativa (px da composição; NaN = nenhuma). Lida só no desenho. */
    var snapX by mutableFloatStateOf(Float.NaN)
    var snapY by mutableFloatStateOf(Float.NaN)

    /** Alça pega (0 = giro, 1..3 = escala; −1 = nenhuma): cresce 5 → 6 dp. */
    var grabbedHandle by mutableIntStateOf(-1)

    companion object {
        // Sobrevive à rotação: painel, adicionar e tela cheia voltam como estavam.
        val Saver = listSaver<EditorUi, Int>(
            save = { listOf(it.panel?.ordinal ?: -1, if (it.adding) 1 else 0, if (it.fullscreen) 1 else 0, it.addTab.ordinal) },
            restore = { s ->
                EditorUi().apply {
                    panel = EditorPanel.entries.getOrNull(s[0])
                    adding = s[1] == 1
                    fullscreen = s[2] == 1
                    addTab = AddTab.entries.getOrNull(s[3]) ?: AddTab.Shape
                }
            },
        )
    }
}

/** Abrir painel pausa a reprodução e fecha o adicionar. */
internal fun openPanel(store: EditorStore, ui: EditorUi, panel: EditorPanel) {
    if (store.playing) store.pause()
    ui.adding = false
    ui.panel = panel
}

internal fun openAdd(store: EditorStore, ui: EditorUi, tab: AddTab = AddTab.Shape) {
    if (store.playing) store.pause()
    ui.panel = null
    ui.addTab = tab
    ui.adding = true
}

/** Abre uma folha pausando antes (toda folha da A.01 pausava o relógio). */
internal fun openSheet(store: EditorStore, ui: EditorUi, sheet: ShellSheet) {
    if (store.playing) store.pause()
    ui.sheet = sheet
}

/**
 * Voltar (sistema e ‹ das barras) — a ordem da A.01: fecha a coisa mais
 * interna, uma por toque. Teclado e folhas modais consomem o Voltar antes de
 * chegar aqui.
 */
internal fun shellBack(store: EditorStore, ui: EditorUi) {
    when {
        store.sceneEditor -> store.exitSceneEditor()
        ui.adding -> ui.adding = false
        ui.fullscreen -> ui.fullscreen = false
        ui.panel != null -> ui.panel = null
        store.selection.isNotEmpty() -> store.clearSelection()
        store.precompDepth > 0 -> store.closePrecomp()
        else -> store.closeProject()
    }
}

// =============================================================================
// A casca
// =============================================================================

/**
 * A casca do editor (Beta A.01): topo 44 · prévia · faixa 8 · transporte 46 ·
 * timeline · painel contextual, com as alturas de [EditorLayout]. Em
 * paisagem/tablet, o layout largo da A.01 (painel à direita).
 *
 * DESEMPENHO: a prévia é `movableContentOf` — a SurfaceView do motor nunca é
 * recriada ao trocar de layout, e nenhum estado de UI entra nela (o que muda
 * é lido no desenho do overlay). O relógio e o transform são lidos só nas
 * folhas que os mostram.
 */
@Composable
fun EditorScreen(store: EditorStore) {
    val ui = rememberSaveable(saver = EditorUi.Saver) { EditorUi() }
    StagePrefs.load(androidx.compose.ui.platform.LocalContext.current)
    val selectionSize by remember { derivedStateOf { store.selection.size } }
    val hasLayers by remember { derivedStateOf { store.layers.isNotEmpty() } }

    // Seleção vazia ou lote fecha o painel. Trocar de UMA camada para outra
    // (as setas ‹ › da timeline compacta) mantém o painel: o painel lê a
    // camada nova do store.
    LaunchedEffect(selectionSize) {
        if (selectionSize != 1) ui.panel = null
    }
    ImmersiveMode(ui.fullscreen)
    BackHandler { shellBack(store, ui) }
    // Voltou do seletor do sistema (mídia, arquivo): o quadro apresentado com
    // a janela escondida pode ter sido descartado — reapresenta ao voltar e
    // de novo quando a animação de volta termina.
    val lifecycleOwner = androidx.lifecycle.compose.LocalLifecycleOwner.current
    DisposableEffect(lifecycleOwner) {
        val handler = android.os.Handler(android.os.Looper.getMainLooper())
        val observer = androidx.lifecycle.LifecycleEventObserver { _, event ->
            if (event == androidx.lifecycle.Lifecycle.Event.ON_RESUME) {
                store.invalidatePreview()
                handler.postDelayed({ store.invalidatePreview() }, 350)
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
            handler.removeCallbacksAndMessages(null)
        }
    }

    // A barra de adicionar fica embaixo sem camada escolhida — e também com a
    // camada só "na mão" da timeline (segurada/arrastada sem abrir as opções):
    // a geometria não muda no meio do gesto. Escolhendo keyframes, a timeline
    // fica alta (sem barra).
    val timelineOnly = store.selection.isNotEmpty() && store.selection == store.timelineOnlySelection
    val addBarState = selectionSize == 0 || (timelineOnly && !store.keySelectMode)
    val content = when {
        ui.adding && addBarState -> SheetContent.AddBar
        ui.adding -> SheetContent.None
        // O painel da Aurea AI CRIA a camada: abrir sem nada selecionado é o
        // caso normal do projeto novo, então ele não passa pelo portão do
        // `selectionSize == 1` que vale para os painéis que EDITAM a camada.
        ui.panel == EditorPanel.AiVideo || ui.panel == EditorPanel.Captions -> SheetContent.Panel
        ui.panel == EditorPanel.Curve && selectionSize == 1 -> SheetContent.Curve
        ui.panel != null && selectionSize == 1 -> SheetContent.Panel
        timelineOnly -> if (store.keySelectMode) SheetContent.None else SheetContent.AddBar
        selectionSize >= 2 -> SheetContent.Batch
        selectionSize == 1 -> SheetContent.Dock
        else -> SheetContent.AddBar
    }
    // Keep the native surface across normal narrow/wide/fullscreen layouts.
    // A workspace transition must dispose its whole layout instead: moving the
    // AndroidView out of the removed scene Column leaves its trailing controls
    // attached to the Compose owner (including live, focusable numeric fields).
    val stage = remember(store, ui, store.sceneEditor) {
        movableContentOf { modifier: Modifier -> PreviewStage(store, ui, modifier) }
    }

    // A altura do palco que a pessoa escolheu arrastando a divisa (0 = automática),
    // guardada no aparelho.
    val previewPrefs = androidx.compose.ui.platform.LocalContext.current.let { context -> remember { context.getSharedPreferences(PREVIEW_PREFS, android.content.Context.MODE_PRIVATE) } }
    val previewPreference = remember { mutableFloatStateOf(previewPrefs.getFloat(PREVIEW_HEIGHT_KEY, 0f)) }
    Box(Modifier.fillMaxSize().background(AureaColors.EditorCanvas)) {
        Column(Modifier.fillMaxSize()) {
            // As barras do sistema são tratadas UMA vez, aqui: a de status no
            // fundo do editor e a de navegação no tom da barra de adicionar
            // (redesenho 2026-09-29: as duas emendam sem faixa preta).
            Spacer(Modifier.fillMaxWidth().windowInsetsTopHeight(WindowInsets.statusBars).background(AureaColors.EditorCanvas))
            BoxWithConstraints(
                Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Horizontal)),
            ) {
                val w = maxWidth.value
                val h = maxHeight.value
                val wide = !ui.fullscreen && ui.panel != EditorPanel.Curve && EditorLayout.isWide(w, h)
                val sheetWidth = EditorLayout.wideSheetWidth(w)
                val aspect = if (store.project.width > 0 && store.project.height > 0) store.project.width.toFloat() / store.project.height else 0f
                val m = EditorLayout.solve(h, content, ui.fullscreen, w, aspect, previewPreference.floatValue)
                if (store.sceneEditor) {
                    SceneLayoutWorkspace(store, ui, stage)
                } else if (wide) {
                    WideEditor(store, ui, content, h, sheetWidth, stage)
                } else {
                    NarrowEditor(store, ui, content, m, stage, EditorLayout.maxPreview(h),
                        onPreviewResize = { previewPreference.floatValue = it },
                        onPreviewCommit = { previewPrefs.edit().putFloat(PREVIEW_HEIGHT_KEY, previewPreference.floatValue).apply() },
                        onPreviewReset = {
                            previewPreference.floatValue = 0f
                            previewPrefs.edit().remove(PREVIEW_HEIGHT_KEY).apply()
                        })
                }

                // O "+" saiu: adicionar mora na barra fixa de baixo ([AddBar]).
                if (ui.adding && !store.sceneEditor) AddLayerOverlay(store, ui)
                // A.01: o círculo "Voltar ao editor" no canto (a HEAD perdeu — bug 1).
                if (ui.fullscreen) {
                    val backLabel = stringResource(R.string.editor_voltar_editor)
                    Box(
                        Modifier
                            .align(Alignment.TopEnd)
                            .padding(10.dp)
                            .size(40.dp)
                            .semantics { contentDescription = backLabel }
                            .background(ShellColors.FloatingDark, CircleShape)
                            .tocavel { ui.fullscreen = false },
                        contentAlignment = Alignment.Center,
                    ) {
                        CupertinoIcon(CupertinoGlyph.FullscreenExit, 24.dp, AureaColors.Text)
                    }
                }
            }
            Spacer(Modifier.fillMaxWidth().windowInsetsBottomHeight(WindowInsets.navigationBars).background(AureaColors.EditorBar))
        }
        BusyOverlay(store)
    }

    ShellSheets(store, ui)
    store.textContentRequest?.let { request ->
        TextContentDialog(request, onSave = { store.commitTextContent(request, it) }, onDismiss = store::dismissTextContentEditor)
    }
    if (ui.effectsBrowser) EffectsBrowserSheet(store) { ui.effectsBrowser = false }
    store.expressionTarget?.let { com.aurea.aurea.editor.panels.ExpressionSheet(store, it) }
    if (ui.exporting) ExportScreen(store) { ui.exporting = false }
}

@Composable
private fun NarrowEditor(
    store: EditorStore,
    ui: EditorUi,
    content: SheetContent,
    m: EditorMetrics,
    stage: @Composable (Modifier) -> Unit,
    maxPreview: Float,
    onPreviewResize: (Float) -> Unit,
    onPreviewCommit: () -> Unit,
    onPreviewReset: () -> Unit,
) {
    Column(Modifier.fillMaxSize().background(AureaColors.EditorCanvas)) {
        if (!ui.fullscreen) TopBarHost(store, ui)
        val previewH = if (ui.fullscreen) (m.preview - ShellDims.FullscreenTimeBar.value).coerceAtLeast(0f) else m.preview
        // O palco e a timeline NÃO espelham em árabe: o quadro 0 fica à
        // esquerda e o tempo anda para a direita em qualquer idioma.
        // Redesenho: a prévia ocupa a largura toda com 8 dp de margem dos lados.
        KeepLtr {
            stage(
                Modifier.fillMaxWidth()
                    .padding(horizontal = if (ui.fullscreen) 0.dp else ShellDims.PreviewMargin)
                    .height(previewH.dp),
            )
        }
        if (ui.fullscreen) {
            FullscreenTimeBar(store)
            TransportBar(store, ui)
        } else {
            // A divisa palco/transporte: arrastar na vertical no transporte (fora
            // dos botões) troca palco por timeline; toque duplo volta à altura
            // automática. A faixa de 8 dp saiu no redesenho (a divisa é o transporte).
            val preview by rememberUpdatedState(m.preview)
            val max by rememberUpdatedState(maxPreview)
            val resize by rememberUpdatedState(onPreviewResize)
            val commit by rememberUpdatedState(onPreviewCommit)
            val reset by rememberUpdatedState(onPreviewReset)
            Column(
                Modifier.fillMaxWidth().testTag("editor.previewDivider")
                    .pointerInput(Unit) {
                        var start = 0f
                        var travel = 0f
                        detectVerticalDragGestures(
                            onDragStart = { start = preview; travel = 0f },
                            onDragEnd = { commit() },
                            onDragCancel = { commit() },
                            onVerticalDrag = { change, dy ->
                                change.consume()
                                travel += dy / density
                                resize((start + travel).coerceIn(EditorLayout.PREVIEW_MIN, max))
                            },
                        )
                    }
                    .pointerInput(Unit) { detectTapGestures(onDoubleTap = { reset() }) },
            ) {
                TransportBar(store, ui)
            }
        }
        if (!ui.fullscreen) {
            // Camada escolhida (doca aberta): a timeline vira a fileira única dela,
            // como com painel aberto — as setas trocam de camada e tocar na barra
            // volta à timeline inteira. O ícone do tipo abre as trilhas da camada
            // (e a timeline fica inteira enquanto estão abertas); arrastar só rola.
            KeepLtr { TimelineHost(store, ui, Modifier.fillMaxWidth().height(m.timeline.dp), compactDock = content == SheetContent.Dock) }
            if (content != SheetContent.None) {
                ContextArea(store, ui, content, Modifier.fillMaxWidth().height(m.sheet.dp))
            }
        }
    }
}

@Composable
private fun WideEditor(
    store: EditorStore,
    ui: EditorUi,
    content: SheetContent,
    totalHeight: Float,
    sheetWidth: Float,
    stage: @Composable (Modifier) -> Unit,
) {
    Column(Modifier.fillMaxSize().background(AureaColors.EditorCanvas)) {
        TopBarHost(store, ui)
        Row(Modifier.weight(1f).fillMaxWidth()) {
            Column(Modifier.weight(1f).fillMaxHeight()) {
                // Palco e timeline em LTR mesmo em árabe (ver `KeepLtr`).
                KeepLtr {
                    stage(Modifier.fillMaxWidth().weight(1f).padding(horizontal = ShellDims.PreviewMargin))
                    TransportBar(store, ui)
                    TimelineHost(store, ui, Modifier.fillMaxWidth().height(EditorLayout.wideTimeline(totalHeight).dp))
                }
                // A barra de adicionar na base da coluna do palco.
                if (content == SheetContent.AddBar) AddBar(store, ui, Modifier.fillMaxWidth().height(ShellDims.AddBar))
            }
            // No largo a folha fica sempre à direita; sem nada escolhido ela
            // mostra a dica do palco.
            val c = if (content == SheetContent.None || content == SheetContent.AddBar) SheetContent.Hint else content
            ContextArea(store, ui, c, Modifier.width(sheetWidth.dp).fillMaxHeight())
        }
    }
}

/** Topo: lote, camada ou projeto — segue a seleção. */
@Composable
private fun TopBarHost(store: EditorStore, ui: EditorUi) {
    val size by remember { derivedStateOf { store.selection.size } }
    val primary by remember { derivedStateOf { store.primary } }
    val id = primary
    val section = ui.panel
    when {
        size >= 2 -> BatchTopBar(store, ui)
        // Seção da camada aberta (redesenho 2026-09-29): `‹` + título centrado.
        id != null && section != null -> SectionTopBar(store, section, onBack = { ui.panel = null })
        id != null -> LayerTopBar(store, ui, id)
        else -> ProjectTopBar(store, ui)
    }
}

private const val PREVIEW_PREFS = "aurea.editor.layout"
private const val PREVIEW_HEIGHT_KEY = "preview_height_dp"

@Composable
private fun TimelineHost(store: EditorStore, ui: EditorUi, modifier: Modifier, compactDock: Boolean = false) {
    // Modo "Selecionar keyframes": o painel fecha para a timeline voltar alta,
    // com as trilhas abertas e a barra de ações inteira.
    val selectingKeys = store.keySelectMode
    LaunchedEffect(selectingKeys) { if (selectingKeys) ui.panel = null }
    // Modo "Selecionar várias camadas": o painel fecha e a timeline fica inteira.
    val pickingLayers = store.layerSelectMode
    LaunchedEffect(pickingLayers) { if (pickingLayers) ui.panel = null }
    Timeline(
        store = store,
        compact = ui.panel != null,
        // Doca aberta: fileira única só sem trilhas abertas nem escolha de
        // keyframes; o ícone do tipo abre as trilhas (timeline inteira).
        compactDock = compactDock,
        // Redesenho 2026-09-29 (Efeitos.dc.html): uma camada escolhida = relógio
        // em caixa (e cabeçote em destaque); sem seleção, o sublinhado.
        timecodeStyle = if (store.selection.size == 1) TimecodeStyle.Box else TimecodeStyle.Underline,
        onEmptyTap = {
            // Tocar no vazio: com painel aberto só fecha o painel; adicionando,
            // fecha o adicionar; senão desseleciona.
            when {
                ui.adding -> ui.adding = false
                ui.panel != null -> ui.panel = null
                else -> store.clearSelection()
            }
        },
        modifier = modifier.clipToBounds(),
        onKeyframeTap = { layer, key -> onKeyframeTapped(store, ui, layer, key) },
        onTrackTap = { layer, property, _ ->
            if (store.primary != layer) store.select(layer)
            openPanel(store, ui, when (property) {
                30 -> EditorPanel.Effects
                31 -> EditorPanel.Effects
                32 -> EditorPanel.Audio
                33 -> EditorPanel.Text
                34 -> EditorPanel.Vector
                35 -> EditorPanel.Shape
                36 -> EditorPanel.Particles
                37 -> EditorPanel.Element3D
                42 -> EditorPanel.Element3D   // parte da forma 3D (TrackProperty::ShapePart)
                39 -> EditorPanel.Speed
                else -> EditorPanel.Transform
            })
        },
    )
}

/**
 * Losango tocado (a timeline já chamou `selectKeyframe`): sem painel aberto
 * abre o editor de curva — ou Efeitos, se o keyframe é de parâmetro de efeito.
 * Com outro painel aberto só navega (A.01).
 */
private fun onKeyframeTapped(store: EditorStore, ui: EditorUi, layer: Long, key: KeyframeRow) {
    if (store.primary != layer || store.selection.size != 1) store.select(layer)
    val current = ui.panel
    if (current != null && current != EditorPanel.Curve) return
    openPanel(store, ui, if (key.property == TrackProperty.EFFECT_PARAM) EditorPanel.Effects else EditorPanel.Curve)
}

/**
 * A zona do painel contextual (`ContextSheet` da A.01): borda superior de
 * 1 dp e, sem título, a faixa vazia de 12 dp. Com painel aberto, o próprio
 * painel desenha o cabeçalho "‹ Título" dentro da área.
 */
@Composable
private fun ContextArea(store: EditorStore, ui: EditorUi, content: SheetContent, modifier: Modifier) {
    if (content == SheetContent.AddBar) {
        AddBar(store, ui, modifier)
        return
    }
    val panel = ui.panel
    Column(
        modifier
            .background(AureaColors.EditorPanelHigh)
            .drawTopHairline(),
    ) {
        if ((content == SheetContent.Panel || content == SheetContent.Curve) && panel != null) {
            PanelContent(
                store = store,
                panel = panel,
                onClose = { ui.panel = null },
                onOpenPanel = { openPanel(store, ui, it) },
                onOpenEffectsBrowser = { ui.effectsBrowser = true },
                modifier = Modifier.fillMaxSize(),
            )
            return@Column
        }
        Spacer(Modifier.height(ShellDims.SheetHandle))
        SheetBody {
            when (content) {
                SheetContent.Adding -> AddLayerPanel(store, ui)
                SheetContent.Batch -> MultiSelectionPanel(store, ui)
                SheetContent.Dock -> {
                    val id by remember { derivedStateOf { store.primary } }
                    id?.let { LayerToolsDock(store, ui, it) }
                }
                else -> StageHint()
            }
        }
    }
}

@Composable
private fun ColumnScope.SheetBody(content: @Composable () -> Unit) {
    Box(Modifier.weight(1f).fillMaxWidth()) { content() }
}

/** Véu de trabalho (importando…): bloqueia o toque e diz o que está havendo. */
@Composable
private fun BusyOverlay(store: EditorStore) {
    val message = store.busyMessage ?: return
    Box(
        Modifier
            .fillMaxSize()
            .background(ShellColors.BusyVeil)
            .pointerInput(Unit) {
                awaitEachGesture {
                    val down = awaitFirstDown(requireUnconsumed = false)
                    down.consume()
                }
            },
        contentAlignment = Alignment.Center,
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            ActivityIndicator()
            Spacer(Modifier.height(12.dp))
            Text(message, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Text)))
        }
    }
}

@Composable
private fun ShellSheets(store: EditorStore, ui: EditorUi) {
    val dismiss = { ui.sheet = null }
    when (ui.sheet) {
        ShellSheet.LayerMenu -> LayerMenuSheet(store, ui, dismiss)
        ShellSheet.TimelineMenu -> TimelineMenuSheet(store, ui, dismiss)
        ShellSheet.ProjectSettings -> ProjectSettingsSheet(store, dismiss)
        ShellSheet.CopyPaste -> CopyPasteSheet(store, dismiss)
        ShellSheet.SearchLayers -> SearchLayersSheet(store, dismiss)
        ShellSheet.CommandSearch -> CommandSearchSheet(store, ui, dismiss)
        ShellSheet.GoToTime -> {
            val fps = store.project.fps
            val seconds = remember { "%.2f".format(java.util.Locale.ROOT, store.playhead / (if (fps > 0f) fps else 30f)) }
            GoToTimeDialog(seconds, fps, onSeek = { store.seek(it) }, onDismiss = dismiss)
        }
        ShellSheet.RenameLayer -> {
            val id = store.primary
            val name = store.layers.firstOrNull { it.id == id }?.name.orEmpty()
            if (id == null) {
                LaunchedEffect(Unit) { ui.sheet = null }
            } else {
                AureaNamePrompt(
                    title = stringResource(R.string.editor_nome_camada),
                    initial = name,
                    onConfirm = { store.renameLayer(id, it) },
                    onDismiss = dismiss,
                )
            }
        }
        null -> {}
    }
}

/** Tela cheia de verdade: some a barra de status e a de navegação (immersiveSticky). */
@Composable
private fun ImmersiveMode(fullscreen: Boolean) {
    val view = LocalView.current
    DisposableEffect(fullscreen) {
        val window = view.context.findActivity()?.window
        val controller = window?.let { WindowCompat.getInsetsController(it, view) }
        if (controller != null) {
            if (fullscreen) {
                controller.systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
                controller.hide(WindowInsetsCompat.Type.systemBars())
            } else {
                controller.show(WindowInsetsCompat.Type.systemBars())
            }
        }
        onDispose {
            // Saiu do editor em tela cheia: devolve as barras do sistema.
            if (fullscreen) controller?.show(WindowInsetsCompat.Type.systemBars())
        }
    }
}

private tailrec fun Context.findActivity(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.findActivity()
    else -> null
}

// =============================================================================
// A superfície do motor
// =============================================================================

/**
 * A superfície do preview. O vídeo vai do motor direto para ela (Vulkan);
 * nenhum pixel passa pelo Compose. Sem `update`: recompor quem a contém não
 * toca nela.
 */
@Composable
fun PreviewSurface(store: EditorStore, modifier: Modifier = Modifier) {
    AndroidView(
        factory = { ctx ->
            SurfaceView(ctx).apply {
                holder.addCallback(object : SurfaceHolder.Callback {
                    override fun surfaceCreated(holder: SurfaceHolder) {
                        val f = holder.surfaceFrame
                        store.attachSurface(holder.surface, f.width().coerceAtLeast(1), f.height().coerceAtLeast(1))
                    }

                    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                        store.resizeSurface(width, height)
                    }

                    override fun surfaceDestroyed(holder: SurfaceHolder) {
                        store.detachSurface()
                    }
                })
            }
        },
        modifier = modifier,
    )
}
