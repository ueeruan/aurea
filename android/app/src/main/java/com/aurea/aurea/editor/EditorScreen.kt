package com.aurea.aurea.editor

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
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
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material3.Icon
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
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.input.pointer.pointerInput
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
internal enum class ShellSheet { LayerMenu, RenameLayer, TimelineMenu, ProjectSettings, CopyPaste, GoToTime, SearchLayers }

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

    val content = when {
        ui.adding -> SheetContent.Adding
        // O painel da Aurea AI CRIA a camada: abrir sem nada selecionado é o
        // caso normal do projeto novo, então ele não passa pelo portão do
        // `selectionSize == 1` que vale para os painéis que EDITAM a camada.
        ui.panel == EditorPanel.AiVideo -> SheetContent.Panel
        ui.panel != null && selectionSize == 1 -> SheetContent.Panel
        selectionSize >= 2 -> SheetContent.Batch
        selectionSize == 1 -> SheetContent.Dock
        else -> SheetContent.None
    }
    val stage = remember(store, ui) {
        movableContentOf { modifier: Modifier -> PreviewStage(store, ui, modifier) }
    }

    Box(Modifier.fillMaxSize().background(AureaColors.EditorTopBar)) {
        Column(Modifier.fillMaxSize()) {
            // As barras do sistema são tratadas UMA vez, aqui: véu escuro na de
            // status (#070A0E, como nos prints) e preto na de navegação.
            Spacer(Modifier.fillMaxWidth().windowInsetsTopHeight(WindowInsets.statusBars).background(AureaColors.StatusBarVeil))
            BoxWithConstraints(
                Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Horizontal)),
            ) {
                val w = maxWidth.value
                val h = maxHeight.value
                val wide = !ui.fullscreen && EditorLayout.isWide(w, h)
                val sheetWidth = EditorLayout.wideSheetWidth(w)
                val m = EditorLayout.solve(h, content, ui.fullscreen)
                if (wide) {
                    WideEditor(store, ui, content, h, sheetWidth, stage)
                } else {
                    NarrowEditor(store, ui, content, m, stage)
                }

                // O "+": escondido em tela cheia, adicionando ou com painel aberto.
                if (!ui.fullscreen && !ui.adding && content != SheetContent.Panel) {
                    val bottom = if (wide || content == SheetContent.None) 0f else m.sheet
                    AddFab(
                        onClick = { openAdd(store, ui) },
                        modifier = Modifier
                            .align(Alignment.BottomEnd)
                            .padding(
                                end = ShellDims.FabMargin + (if (wide) sheetWidth.dp else 0.dp),
                                bottom = ShellDims.FabMargin + bottom.dp,
                            ),
                    )
                }
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
            Spacer(Modifier.fillMaxWidth().windowInsetsBottomHeight(WindowInsets.navigationBars).background(ShellColors.NavigationBar))
        }
        BusyOverlay(store)
    }

    ShellSheets(store, ui)
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
) {
    Column(Modifier.fillMaxSize().background(AureaColors.Background)) {
        if (!ui.fullscreen) TopBarHost(store, ui)
        val previewH = if (ui.fullscreen) (m.preview - ShellDims.FullscreenTimeBar.value).coerceAtLeast(0f) else m.preview
        // O palco e a timeline NÃO espelham em árabe: o quadro 0 fica à
        // esquerda e o tempo anda para a direita em qualquer idioma.
        KeepLtr { stage(Modifier.fillMaxWidth().height(previewH.dp)) }
        if (ui.fullscreen) {
            FullscreenTimeBar(store)
        } else {
            PreviewStrip(ui)
        }
        TransportBar(store, ui)
        if (!ui.fullscreen) {
            KeepLtr { TimelineHost(store, ui, Modifier.fillMaxWidth().height(m.timeline.dp)) }
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
    Column(Modifier.fillMaxSize().background(AureaColors.Background)) {
        TopBarHost(store, ui)
        Row(Modifier.weight(1f).fillMaxWidth()) {
            Column(Modifier.weight(1f).fillMaxHeight()) {
                // Palco e timeline em LTR mesmo em árabe (ver `KeepLtr`).
                KeepLtr {
                    stage(Modifier.fillMaxWidth().weight(1f))
                    PreviewStrip(ui)
                    TransportBar(store, ui)
                    TimelineHost(store, ui, Modifier.fillMaxWidth().height(EditorLayout.wideTimeline(totalHeight).dp))
                }
            }
            // No largo a folha fica sempre à direita; sem nada escolhido ela
            // mostra a dica do palco.
            val c = if (content == SheetContent.None) SheetContent.Hint else content
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
    when {
        size >= 2 -> BatchTopBar(store)
        id != null -> LayerTopBar(store, ui, id)
        else -> ProjectTopBar(store, ui)
    }
}

/** A faixa de 8 dp entre prévia e transporte (a tela cheia mora no transporte, um botão só). */
@Composable
private fun PreviewStrip(@Suppress("UNUSED_PARAMETER") ui: EditorUi) {
    Box(Modifier.fillMaxWidth().height(ShellDims.Strip).background(AureaColors.EditorPanelHigh))
}

@Composable
private fun TimelineHost(store: EditorStore, ui: EditorUi, modifier: Modifier) {
    Timeline(
        store = store,
        compact = ui.panel != null,
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
                30 -> EditorPanel.Speed
                31 -> EditorPanel.Effects
                32 -> EditorPanel.Audio
                33 -> EditorPanel.Text
                34 -> EditorPanel.Vector
                35 -> EditorPanel.Shape
                36 -> EditorPanel.Particles
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
    val panel = ui.panel
    Column(
        modifier
            .background(AureaColors.EditorPanelHigh)
            .drawTopHairline(),
    ) {
        if (content == SheetContent.Panel && panel != null) {
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

/** O "+" da A.01: 52 dp, fundo #1E2130, anel 2,2 `acao`, sombra 45 %. */
@Composable
private fun AddFab(onClick: () -> Unit, modifier: Modifier) {
    val label = stringResource(R.string.editor_adicionar_camada)
    Box(
        modifier
            .size(ShellDims.Fab)
            .shadow(6.dp, CircleShape, ambientColor = ShellColors.FabShadow, spotColor = ShellColors.FabShadow)
            .background(ShellColors.Fab, CircleShape)
            .border(2.2.dp, AureaColors.Action, CircleShape)
            .semantics { contentDescription = label }
            .tocavel(haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        // O "+" em destaque: `acao` (#245D8C) sobre #1E2130 dava 2,3:1 (bug 27).
        Icon(Icons.Filled.Add, contentDescription = null, tint = AureaColors.Accent, modifier = Modifier.size(32.dp))
    }
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
