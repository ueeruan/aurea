package com.aurea.aurea.editor

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.draganddrop.dragAndDropTarget
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.ui.input.pointer.onPointerEvent
import androidx.compose.ui.input.pointer.pointerHoverIcon
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.ui.draganddrop.DragAndDropEvent
import androidx.compose.ui.draganddrop.DragAndDropTarget
import androidx.compose.ui.draganddrop.DragData
import androidx.compose.ui.draganddrop.dragData
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.zIndex
import com.aurea.aurea.R
import kotlin.math.roundToInt
import com.aurea.aurea.editor.timeline.TimelineZoomRequests
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.i18n.KeepLtr
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

/**
 * O editor do Aurea PC, na organização de um editor de computador. Proporções
 * medidas no CapCut de PC 9.7 (build/reference/capcut-pc/04-editor-1.png),
 * só como referência de layout; cores, ícones e peças são as do Aurea:
 *
 *   ┌──────────────── barra do projeto (voltar · nome · Exportar) ────────────────┐
 *   │ ┌ biblioteca ┐ ┌──── reprodutor ────┐ ┌ detalhes / propriedades ┐   58 %    │
 *   │ └────────────┘ └────────────────────┘ └─────────────────────────┘            │
 *   │ ┌ ferramentas da timeline ─────────────────────────── zoom ┐               │
 *   │ │ timeline na largura toda                                  │   42 %       │
 *   └─┴──────────────────────────────────────────────────────────┴───────────────┘
 *
 * Cartões de cantos 8 com 6 de vão, no fundo do editor. Substitui o arquivo de
 * mesmo nome do Android (ver desktop/android-excludes.txt).
 */
@Composable
internal fun PlatformWideEditor(
    store: EditorStore,
    ui: EditorUi,
    content: SheetContent,
    totalHeight: Float,
    sheetWidth: Float,
    stage: @Composable (Modifier) -> Unit,
    @Suppress("UNUSED_PARAMETER") floatingText: Boolean = false,
) {
    val gap = 6.dp
    val density = androidx.compose.ui.platform.LocalDensity.current
    // Tamanhos que a pessoa escolhe arrastando as divisórias (guardados entre aberturas).
    val layout = PcPanelLayout
    androidx.compose.foundation.layout.BoxWithConstraints(Modifier.fillMaxSize().background(AureaColors.EditorCanvas)) {
        val totalW = maxWidth.value
        val totalH = maxHeight.value
        Column(Modifier.fillMaxSize()) {
            Box(Modifier.fillMaxWidth().zIndex(if (store.textEditLayer != null) 2f else 0f)) { TopBarHost(store, ui) }
            Row(Modifier.weight(layout.top).fillMaxWidth().padding(start = gap, end = gap, top = gap)) {
                PcCard(Modifier.width(layout.left.dp)) { PcLibrary(store, ui) }
                PcSplitter(vertical = true) { dx -> layout.left = (layout.left + dx / density.density).coerceIn(220f, totalW * 0.45f) }
                PcCard(Modifier.weight(1f)) {
                    PcCardHeader(stringResource(R.string.pc_player))
                    KeepLtr {
                        stage(Modifier.fillMaxWidth().weight(1f).padding(horizontal = 8.dp))
                        TransportBar(store, ui)
                    }
                }
                PcSplitter(vertical = true) { dx -> layout.right = (layout.right - dx / density.density).coerceIn(240f, totalW * 0.45f) }
                PcCard(Modifier.width(layout.right.dp)) {
                    // Como no AE: a direita é só dos Controles de efeitos; posição, 3D e o resto
                    // ficam na timeline, e as ferramentas da camada na janela flutuante.
                    when {
                        store.textEditLayer != null -> TextEditPanel(store, Modifier.fillMaxSize())
                        store.primary == null -> PcProjectDetails(store)
                        else -> PcEffectControls(store, ui)
                    }
                }
            }
            PcSplitter(vertical = false) { dy -> layout.top = (layout.top + dy / density.density / totalH).coerceIn(0.22f, 0.8f) }
            PcCard(Modifier.weight(1f - layout.top).fillMaxWidth().padding(start = gap, end = gap, bottom = gap)) {
                PcTimelineToolbar(store)
                Box(Modifier.fillMaxWidth().height(1.dp).background(AureaColors.Hairline))
                PcDropTarget(store, Modifier.fillMaxWidth().weight(1f)) {
                    KeepLtr {
                        if (PcLibraryState.aeTimeline) PcAeTimeline(store, ui, Modifier.fillMaxSize())
                        else TimelineHost(store, ui, Modifier.fillMaxSize())
                    }
                }
            }
        }
        // As ferramentas da camada e os painéis delas flutuam por cima do editor.
        PcPanelWindowHost(store, ui, totalW, totalH)
        // Lote M do AE: os cartões de Traçado automático, Criar nulos, Suavizar e Transformar pontos.
        PcMaskRigHost(store, ui, totalW, totalH)
        // Lote X do AE: a árvore do conteúdo da camada vetorial (PcShapeTree.kt).
        PcShapeTreeHost(store, totalW, totalH)
    }
}

/**
 * Os CONTROLES DE EFEITOS (o painel Efeitos do AE) da camada escolhida: o nome
 * da camada, a pilha de efeitos do Aurea (ligar/desligar, abrir, reordenar,
 * apagar, keyframes — o [com.aurea.aurea.editor.panels.EffectsPanel] de sempre)
 * e o "+" que abre a aba Efeitos da biblioteca. O ‹ da coluna do painel abre as
 * ferramentas da camada na janela flutuante.
 */
@Composable
private fun androidx.compose.foundation.layout.ColumnScope.PcEffectControls(store: EditorStore, ui: EditorUi) {
    val id = store.primary
    val name = store.layers.firstOrNull { it.id == id }?.name.orEmpty()
    val count = store.selection.size
    Row(Modifier.fillMaxWidth().height(44.dp).padding(start = 12.dp, end = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(stringResource(R.string.pc_effect_controls), color = AureaColors.Text, fontSize = 13.sp, fontWeight = FontWeight.Medium,
                maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text(if (count > 1) "$name  (+${count - 1})" else name, color = AureaColors.Subtle, fontSize = 11.sp,
                maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        ToolButton(CupertinoGlyph.SquareGrid2x2, stringResource(R.string.pc_layer_tools), true) { PcPanelWindow.showTools(ui) }
        ToolButton(CupertinoGlyph.Plus, stringResource(R.string.panel_adicionar_efeito), true) { PcLibraryState.effects = true }
    }
    Box(Modifier.fillMaxWidth().height(1.dp).background(AureaColors.Hairline))
    Box(Modifier.fillMaxWidth().weight(1f)) {
        com.aurea.aurea.editor.panels.PanelContent(
            store = store,
            panel = com.aurea.aurea.editor.panels.EditorPanel.Effects,
            onClose = { PcPanelWindow.showTools(ui) },
            onOpenPanel = { pcOpenPanel(store, ui, it) },
            onOpenEffectsBrowser = { PcLibraryState.effects = true },
            modifier = Modifier.fillMaxSize(),
        )
    }
}

/**
 * Divisória arrastável entre os painéis (6 dp, o vão entre os cartões): o
 * cursor de redimensionar e um fio aceso ao passar o mouse. Soltar grava.
 */
@Composable
private fun PcSplitter(vertical: Boolean, onDrag: (Float) -> Unit) {
    var hot by remember { mutableStateOf(false) }
    val cursor = java.awt.Cursor(if (vertical) java.awt.Cursor.E_RESIZE_CURSOR else java.awt.Cursor.N_RESIZE_CURSOR)
    Box(
        (if (vertical) Modifier.width(6.dp).fillMaxHeight() else Modifier.fillMaxWidth().height(6.dp))
            .pointerHoverIcon(androidx.compose.ui.input.pointer.PointerIcon(cursor))
            .onPointerEvent(androidx.compose.ui.input.pointer.PointerEventType.Enter) { hot = true }
            .onPointerEvent(androidx.compose.ui.input.pointer.PointerEventType.Exit) { hot = false }
            .pointerInput(vertical) {
                if (vertical) detectHorizontalDragGestures(onDragEnd = { PcPanelLayout.save() }) { c, d -> c.consume(); onDrag(d) }
                else detectVerticalDragGestures(onDragEnd = { PcPanelLayout.save() }) { c, d -> c.consume(); onDrag(d) }
            },
        contentAlignment = Alignment.Center,
    ) {
        if (hot) Box(if (vertical) Modifier.width(2.dp).fillMaxHeight() else Modifier.fillMaxWidth().height(2.dp)
            .background(AureaColors.Accent.copy(alpha = 0.7f)))
    }
}

/** Larguras (dp) da biblioteca e das propriedades e a fração de cima; guardadas em pc_layout. */
object PcPanelLayout {
    private val prefs by lazy { com.aurea.pc.PcApp.context.getSharedPreferences("pc_layout", android.content.Context.MODE_PRIVATE) }
    var left by mutableStateOf(prefs.getFloat("left", 360f))
    var right by mutableStateOf(prefs.getFloat("right", 360f))
    var top by mutableStateOf(prefs.getFloat("top", 0.56f))
    var curves by mutableStateOf(prefs.getFloat("curves", 320f))
    fun save() { prefs.edit().putFloat("left", left).putFloat("right", right).putFloat("top", top).putFloat("curves", curves).apply() }
}

/** Um cartão do layout: fundo do painel, cantos de 8 e fio fino. */
@Composable
private fun PcCard(modifier: Modifier, content: @Composable androidx.compose.foundation.layout.ColumnScope.() -> Unit) {
    val shape = RoundedCornerShape(8.dp)
    Column(
        modifier.fillMaxHeight().clip(shape).background(AureaColors.EditorPanel).border(1.dp, AureaColors.Hairline, shape),
        content = content,
    )
}

@Composable
private fun PcCardHeader(title: String, trailing: @Composable RowScope.() -> Unit = {}) {
    Row(Modifier.fillMaxWidth().height(36.dp).padding(horizontal = 12.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(title, color = AureaColors.Text, fontSize = 13.sp, fontWeight = FontWeight.Medium, modifier = Modifier.weight(1f),
            maxLines = 1, overflow = TextOverflow.Ellipsis)
        trailing()
    }
    Box(Modifier.fillMaxWidth().height(1.dp).background(AureaColors.Hairline))
}

/** Arrastar arquivos do Explorer para dentro: importa como o "+ Mídia". */
@OptIn(ExperimentalFoundationApi::class, ExperimentalComposeUiApi::class)
@Composable
private fun PcDropTarget(store: EditorStore, modifier: Modifier, content: @Composable () -> Unit) {
    var hovering by remember { mutableStateOf(false) }
    val target = remember(store) {
        object : DragAndDropTarget {
            override fun onEntered(event: DragAndDropEvent) { hovering = true }
            override fun onExited(event: DragAndDropEvent) { hovering = false }
            override fun onEnded(event: DragAndDropEvent) { hovering = false }
            override fun onDrop(event: DragAndDropEvent): Boolean {
                hovering = false
                val files = (event.dragData() as? DragData.FilesList)?.readFiles().orEmpty()
                val uris = files.map { android.net.Uri.parse(it) }.filter { it.scheme == "file" }
                if (uris.isEmpty()) return false
                store.importMediaBatch(uris)
                return true
            }
        }
    }
    Box(modifier.dragAndDropTarget(shouldStartDragAndDrop = { it.dragData() is DragData.FilesList }, target = target)) {
        content()
        if (hovering) Box(Modifier.fillMaxSize().background(AureaColors.Accent.copy(alpha = 0.12f)).border(2.dp, AureaColors.Accent, RoundedCornerShape(8.dp)))
    }
}

/**
 * A biblioteca: as abas do adicionar do Aurea em fileira (ícone + nome, como
 * nos editores de PC) e o conteúdo da aba embaixo, sem a gaveta do celular.
 * Mídia e Áudio abrem com a área de importar (clique ou arraste).
 */
@Composable
private fun androidx.compose.foundation.layout.ColumnScope.PcLibrary(store: EditorStore, ui: EditorUi) {
    val tabs = listOf(AddTab.Media, AddTab.Audio, AddTab.Text, AddTab.TextPresets, AddTab.Shape,
        AddTab.Element, AddTab.Model3D, AddTab.Draw, AddTab.Vector)
    Row(Modifier.fillMaxWidth().height(52.dp).padding(horizontal = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        val fx = PcLibraryState.effects
        tabs.forEach { tab ->
            val on = !fx && ui.addTab == tab
            LibraryTab(stringResource(tab.label), on, { AddBarIcon(tab, 18.dp, if (on) AureaColors.Accent else AureaColors.Muted) }) {
                PcLibraryState.effects = false
                ui.addTab = tab
            }
        }
        LibraryTab(stringResource(R.string.pc_library_effects), fx, { CupertinoIcon(CupertinoGlyph.Sparkles, 18.dp, if (fx) AureaColors.Accent else AureaColors.Muted) }) {
            PcLibraryState.effects = true
        }
    }
    Box(Modifier.fillMaxWidth().height(1.dp).background(AureaColors.Hairline))
    if (PcLibraryState.effects) {
        // A galeria de efeitos no painel (clique abre a prévia; o "adicionar" de lá aplica na camada escolhida).
        val hasAudio = store.detail?.hasAudio == true
        Box(Modifier.fillMaxWidth().weight(1f)) {
            com.aurea.aurea.effects.EffectAddSheet(
                store = store,
                layerHasAudio = hasAudio,
                onPick = { store.addEffect(it.typeId) },
                onTool = { tool -> com.aurea.aurea.editor.panels.openEffectTool(store, { openPanel(store, ui, it) }, tool) },
                onDismiss = { PcLibraryState.effects = false },
                inline = true,
            )
        }
        return
    }
    when (ui.addTab) {
        AddTab.Media, AddTab.Audio -> PcImportZone(store, audio = ui.addTab == AddTab.Audio, Modifier.fillMaxWidth().weight(1f).padding(10.dp))
        else -> AddLayerPanel(store, ui, Modifier.fillMaxWidth().weight(1f), chrome = false)
    }
}

@Composable
private fun RowScope.LibraryTab(label: String, on: Boolean, icon: @Composable () -> Unit, onClick: () -> Unit) {
    Column(
        Modifier.weight(1f).clip(RoundedCornerShape(6.dp)).tocavel(shrink = 1f, onClick = onClick).padding(vertical = 4.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(Modifier.size(22.dp), contentAlignment = Alignment.Center) { icon() }
        Text(label, color = if (on) AureaColors.Accent else AureaColors.Muted, fontSize = 10.sp, maxLines = 1,
            overflow = TextOverflow.Ellipsis, textAlign = TextAlign.Center)
    }
}

/** A área de importar: clique abre o seletor do Windows; arrastar arquivos também importa. */
@Composable
private fun PcImportZone(store: EditorStore, audio: Boolean, modifier: Modifier) {
    val import = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty()) store.importMediaBatch(uris)
    }
    PcDropTarget(store, modifier) {
        Column(
            Modifier.fillMaxSize().clip(RoundedCornerShape(8.dp)).background(AureaColors.EditorPanelHigh)
                .tocavel(shrink = 1f) { import.launch(if (audio) arrayOf("audio/*") else arrayOf("video/*", "image/*", "audio/*")) },
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(22.dp).clip(RoundedCornerShape(11.dp)).background(AureaColors.Brand), contentAlignment = Alignment.Center) {
                    CupertinoIcon(CupertinoGlyph.Plus, 12.dp, AureaColors.OnAction)
                }
                Spacer(Modifier.width(8.dp))
                Text(stringResource(R.string.pc_import_media), color = AureaColors.Text, fontSize = 15.sp, fontWeight = FontWeight.SemiBold)
            }
            Spacer(Modifier.height(6.dp))
            Text(stringResource(R.string.pc_drop_hint), color = AureaColors.Muted, fontSize = 12.sp)
        }
    }
}

/** Sem nada escolhido, a coluna da direita mostra o projeto (como os "Detalhes" dos editores de PC). */
@Composable
private fun androidx.compose.foundation.layout.ColumnScope.PcProjectDetails(store: EditorStore) {
    PcCardHeader(stringResource(R.string.pc_details))
    val p = store.project
    val seconds = if (p.fps > 0f) p.durationFrames / p.fps else 0f
    Column(Modifier.fillMaxWidth().weight(1f).verticalScroll(rememberScrollState()).padding(horizontal = 14.dp, vertical = 10.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)) {
        DetailRow(stringResource(R.string.pc_name), p.title)
        DetailRow(stringResource(R.string.pc_resolution), if (p.width > 0) "${p.width} × ${p.height}" else "—")
        DetailRow(stringResource(R.string.pc_fps), String.format(java.util.Locale.ROOT, "%.2f fps", p.fps))
        DetailRow(stringResource(R.string.pc_duration), String.format(java.util.Locale.ROOT, "%d:%02d.%02d",
            (seconds / 60).toInt(), (seconds % 60).toInt(), ((seconds * 100) % 100).toInt()))
        p.path?.let { DetailRow(stringResource(R.string.pc_location), it) }
    }
    Box(Modifier.fillMaxWidth().padding(14.dp)) {
        Text(stringResource(R.string.pc_select_hint), color = AureaColors.Subtle, fontSize = 12.sp)
    }
}

@Composable
private fun DetailRow(label: String, value: String) {
    Row {
        Text(label, color = AureaColors.Muted, fontSize = 12.sp, modifier = Modifier.width(130.dp))
        Text(value, color = AureaColors.Text, fontSize = 12.sp, modifier = Modifier.weight(1f))
    }
}

/**
 * A barra da timeline: dividir, excluir, duplicar, marcador à esquerda; zoom e
 * atalhos à direita. (Desfazer/refazer já ficam na barra do reprodutor.)
 */
@Composable
private fun PcTimelineToolbar(store: EditorStore) {
    Row(
        Modifier.fillMaxWidth().height(36.dp).padding(horizontal = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        val hasSel = store.selection.isNotEmpty()
        ToolButton(CupertinoGlyph.Scissors, stringResource(R.string.editor_dividir_cabecote) + "  (Ctrl+K)", hasSel) { store.splitAtPlayhead() }
        ToolButton(CupertinoGlyph.Trash, stringResource(R.string.pc_sc_delete) + "  (Delete)", hasSel) { store.deleteLayers() }
        ToolButton(CupertinoGlyph.PlusSquareOnSquare, stringResource(R.string.editor_duplicar) + "  (Ctrl+D)", hasSel) { store.duplicateLayers() }
        ToolButton(CupertinoGlyph.Flag, stringResource(R.string.pc_marker) + "  (M)", true) { store.toggleMarker() }
        MagnetButton(store)
        PreRenderButton(store)
        CurveEditorButton(store)
        Spacer(Modifier.weight(1f))
        ToolButton(CupertinoGlyph.Minus, stringResource(R.string.pc_zoom_out) + "  (−)", true) { TimelineZoomRequests.request(1f / 1.5f) }
        ToolButton(CupertinoGlyph.Plus, stringResource(R.string.pc_zoom_in) + "  (=)", true) { TimelineZoomRequests.request(1.5f) }
        Box(Modifier.padding(horizontal = 6.dp).width(1.dp).height(18.dp).background(AureaColors.Hairline))
        ToolButton(CupertinoGlyph.QuestionCircle, stringResource(R.string.pc_shortcuts) + "  (F1)", true) { com.aurea.pc.PcShortcuts.helpOpen = true }
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun ToolButton(glyph: Char, tooltip: String, enabled: Boolean, onClick: () -> Unit) {
    androidx.compose.foundation.TooltipArea(
        tooltip = {
            Text(tooltip, color = AureaColors.Text, fontSize = 12.sp,
                modifier = Modifier.background(AureaColors.SurfaceHigh, RoundedCornerShape(6.dp)).padding(horizontal = 8.dp, vertical = 4.dp))
        },
        delayMillis = 400,
    ) {
        Box(
            Modifier.size(30.dp).clip(RoundedCornerShape(6.dp)).then(if (enabled) Modifier.tocavel(shrink = 1f, onClick = onClick) else Modifier),
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(glyph, 17.dp, if (enabled) AureaColors.Text else Color(0x55FFFFFF))
        }
    }
}

/** A aba Efeitos da biblioteca (fora do enum de abas do adicionar). */
object PcLibraryState {
    var effects by mutableStateOf(false)
    /** A timeline no formato do After Effects com o visual do Aurea (PcAeTimeline.kt): ligada no PC (pedido do dono, 11/11). */
    var aeTimeline by mutableStateOf(true)
}

/**
 * O editor de gráficos do AE (Shift+F3): abre a janelinha de Curvas no keyframe
 * escolhido ou, sem um, no da camada principal no cabeçote; de novo fecha.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun CurveEditorButton(store: EditorStore) {
    val on = PcCurvePopup.open
    androidx.compose.foundation.TooltipArea(
        tooltip = {
            Text(stringResource(R.string.pc_curve_graph) + "  (Shift+F3)", color = AureaColors.Text, fontSize = 12.sp,
                modifier = Modifier.background(AureaColors.SurfaceHigh, RoundedCornerShape(6.dp)).padding(horizontal = 8.dp, vertical = 4.dp))
        },
        delayMillis = 400,
    ) {
        Box(
            Modifier.size(30.dp).clip(RoundedCornerShape(6.dp)).background(if (on) AureaColors.Accent.copy(alpha = 0.18f) else Color.Transparent)
                .tocavel(shrink = 1f) { if (PcCurvePopup.open) PcCurvePopup.close() else pcOpenCurveNear(store) },
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(CupertinoGlyph.Scribble, 17.dp, if (on) AureaColors.Accent else AureaColors.Text)
        }
    }
}

/** O ímã: ligado, mover, cortar e esticar encaixam no cabeçote e nas bordas das outras camadas. */
@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun MagnetButton(store: EditorStore) {
    val on = store.snapping
    androidx.compose.foundation.TooltipArea(
        tooltip = {
            Text(stringResource(R.string.i18n_snapping) + "  (S)", color = AureaColors.Text, fontSize = 12.sp,
                modifier = Modifier.background(AureaColors.SurfaceHigh, RoundedCornerShape(6.dp)).padding(horizontal = 8.dp, vertical = 4.dp))
        },
        delayMillis = 400,
    ) {
        Box(
            Modifier.size(30.dp).clip(RoundedCornerShape(6.dp)).background(if (on) AureaColors.Accent.copy(alpha = 0.18f) else Color.Transparent)
                .tocavel(shrink = 1f) { store.snapping = !store.snapping },
            contentAlignment = Alignment.Center,
        ) {
            val tint = if (on) AureaColors.Accent else AureaColors.Muted
            androidx.compose.foundation.Canvas(Modifier.size(16.dp)) {
                val w = size.width; val h = size.height; val sw = w * 0.2f
                // U do ímã: dois braços e a curva embaixo; as pontas claras.
                val path = androidx.compose.ui.graphics.Path().apply {
                    moveTo(w * 0.18f, h * 0.08f); lineTo(w * 0.18f, h * 0.55f)
                    arcTo(androidx.compose.ui.geometry.Rect(w * 0.18f, h * 0.22f, w * 0.82f, h * 0.88f), 180f, -180f, false)
                    lineTo(w * 0.82f, h * 0.08f)
                }
                drawPath(path, tint, style = androidx.compose.ui.graphics.drawscope.Stroke(sw))
                drawLine(Color.White.copy(alpha = 0.9f), androidx.compose.ui.geometry.Offset(w * 0.18f, h * 0.08f), androidx.compose.ui.geometry.Offset(w * 0.18f, h * 0.24f), sw)
                drawLine(Color.White.copy(alpha = 0.9f), androidx.compose.ui.geometry.Offset(w * 0.82f, h * 0.08f), androidx.compose.ui.geometry.Offset(w * 0.82f, h * 0.24f), sw)
            }
        }
    }
}

/**
 * PreRender à vista (no celular é o gesto de arrastar o play): clique renderiza
 * à frente do cabeçote e mostra o progresso; de novo cancela. Tecla 0 do
 * teclado numérico, como a prévia do AE.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun PreRenderButton(store: EditorStore) {
    val p = store.preRenderProgress
    val busy = store.preRenderPreparing || p.running
    val label = when {
        store.preRenderPreparing -> stringResource(R.string.prerender_preparing)
        p.running -> stringResource(R.string.prerender_progress, (p.fraction * 100).roundToInt())
        p.complete -> stringResource(R.string.prerender_ready)
        else -> stringResource(R.string.prerender_forward)
    }
    androidx.compose.foundation.TooltipArea(
        tooltip = {
            Text((if (busy) stringResource(R.string.prerender_cancel) else stringResource(R.string.prerender_forward)) + "  (Num 0)",
                color = AureaColors.Text, fontSize = 12.sp,
                modifier = Modifier.background(AureaColors.SurfaceHigh, RoundedCornerShape(6.dp)).padding(horizontal = 8.dp, vertical = 4.dp))
        },
        delayMillis = 400,
    ) {
        Row(
            Modifier.height(28.dp).clip(RoundedCornerShape(6.dp))
                .background(if (busy) AureaColors.Accent.copy(alpha = 0.18f) else Color.Transparent)
                .tocavel(shrink = 1f) { if (busy) store.cancelPreRender() else store.startPreRender(1) }
                .padding(horizontal = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            CupertinoIcon(if (busy) CupertinoGlyph.XmarkCircle else CupertinoGlyph.ForwardEnd, 15.dp, if (busy || p.complete) AureaColors.Accent else AureaColors.Muted)
            Spacer(Modifier.width(6.dp))
            Text(label, color = if (busy || p.complete) AureaColors.Accent else AureaColors.Muted, fontSize = 12.sp, maxLines = 1)
        }
    }
}
