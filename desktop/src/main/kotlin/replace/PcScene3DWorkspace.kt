package com.aurea.aurea.editor

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.TooltipArea
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectVerticalDragGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.input.pointer.PointerIcon
import androidx.compose.ui.input.pointer.PointerInputChange
import androidx.compose.ui.input.pointer.isAltPressed
import androidx.compose.ui.input.pointer.isCtrlPressed
import androidx.compose.ui.input.pointer.isPrimaryPressed
import androidx.compose.ui.input.pointer.isSecondaryPressed
import androidx.compose.ui.input.pointer.isShiftPressed
import androidx.compose.ui.input.pointer.isTertiaryPressed
import androidx.compose.ui.input.pointer.pointerHoverIcon
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.GIZMO_MOVE
import com.aurea.aurea.state.GIZMO_ROTATE
import com.aurea.aurea.state.GIZMO_SCALE
import com.aurea.aurea.state.SCENE_TOOL_SELECT
import com.aurea.aurea.state.Shape3DCatalog
import com.aurea.aurea.ui.ds.AnchoredMenu
import com.aurea.aurea.ui.ds.AureaSurfaceTone
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.i18n.KeepLtr
import com.aurea.aurea.ui.platform.PlatformFeatures
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.cos
import kotlin.math.exp
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * Liga a Cena 3D do PC (chamado pelo Main.kt, depois de
 * `PlatformFeatures.desktopScene3d = true`). No celular nada disto existe.
 */
fun installPcScene3D() {
    if (!PlatformFeatures.desktopScene3d) return
    SceneWorkspaceHost.workspace = { store, ui, stage -> PcScene3DWorkspace(store, ui, stage) }
    SceneWorkspaceHost.pointer = object : ScenePointerHost {
        override suspend fun AwaitPointerEventScope.press(store: EditorStore, m: StageMapper, down: PointerInputChange): Boolean =
            pcScenePress(store, m, down)
        override suspend fun AwaitPointerEventScope.gesture(store: EditorStore, m: StageMapper, down: PointerInputChange) =
            pcSceneGesture(store, m, down)
    }
}

/**
 * A área de trabalho da Cena 3D no Aurea PC, na ORGANIZAÇÃO do Blender e com as
 * peças do Aurea (cartões, cores, ícones, menus e os painéis de propriedade de
 * sempre):
 *
 *   ┌ cabeçalho: modo · Visualizar/Selecionar/Adicionar/Objeto · Global · pivô · ímã · sombreamento ┐
 *   │ ┌ T ┐                                              ┌ gizmo ┐ │ ┌ Outliner ─────────┐
 *   │ │   │              viewport 3D                     │ N     │ │ └───────────────────┘
 *   │ └───┘                                              └───────┘ │ ┌ Propriedades ─────┐
 *   ├─────────────── transporte ───────────────────────────────────┤ │ abas em ícones    │
 *   └ timeline do editor (a mesma do PC) na largura toda ──────────┴─┴───────────────────┘
 */
@Composable
internal fun PcScene3DWorkspace(store: EditorStore, ui: EditorUi, stage: @Composable (Modifier) -> Unit) {
    DisposableEffect(store) {
        PcScene3D.enter(store)
        onDispose { PcScene3D.leave(store) }
    }
    // Shift+D: a cópia chegou (a seleção trocou) e já sai andando.
    LaunchedEffect(store.selection) {
        if (PcScene3D.pendingGrab) {
            val fresh = System.currentTimeMillis() - PcScene3D.pendingGrabAt < 1500L
            if (!fresh) PcScene3D.pendingGrab = false
            else if (store.selection.isNotEmpty()) {
                PcScene3D.pendingGrab = false
                PcScene3D.startModal(store, PcScene3D.OP_MOVE)
            }
        }
    }
    val importModel = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty()) store.importModel(uris)
    }
    LaunchedEffect(PcScene3D.importRequest) {
        if (PcScene3D.importRequest > 0) {
            PcScene3D.importRequest = 0     // um pedido, um seletor (a cena pode ser reaberta depois)
            importModel.launch(arrayOf("model/gltf-binary", "model/gltf+json", "model/obj", "model/mtl",
                "application/x-fbx", "application/zip", "text/plain", "image/*", "application/octet-stream", "*/*"))
        }
    }
    val density = LocalDensity.current
    val gap = 6.dp
    BoxWithConstraints(Modifier.fillMaxSize().background(AureaColors.EditorCanvas)) {
        val totalW = maxWidth.value
        val totalH = maxHeight.value
        Column(Modifier.fillMaxSize()) {
            Row(Modifier.weight(PcSceneLayout.top).fillMaxWidth().padding(start = gap, end = gap, top = gap)) {
                SceneCard(Modifier.weight(1f).fillMaxHeight()) {
                    SceneHeader(store)
                    Box(Modifier.fillMaxWidth().height(1.dp).background(AureaColors.Hairline))
                    SceneViewport(store, stage, Modifier.fillMaxWidth().weight(1f))
                    KeepLtr { TransportBar(store, ui) }
                }
                SceneSplitter(vertical = true) { dx -> PcSceneLayout.right = (PcSceneLayout.right - dx / density.density).coerceIn(280f, totalW * 0.45f) }
                Column(Modifier.width(PcSceneLayout.right.dp).fillMaxHeight()) {
                    SceneCard(Modifier.weight(PcSceneLayout.outliner).fillMaxWidth()) { PcSceneOutliner(store) }
                    SceneSplitter(vertical = false) { dy ->
                        PcSceneLayout.outliner = (PcSceneLayout.outliner + dy / density.density / (totalH * PcSceneLayout.top)).coerceIn(0.15f, 0.7f)
                    }
                    SceneCard(Modifier.weight(1f - PcSceneLayout.outliner).fillMaxWidth()) { PcSceneProperties(store, ui) }
                }
            }
            SceneSplitter(vertical = false) { dy -> PcSceneLayout.top = (PcSceneLayout.top + dy / density.density / totalH).coerceIn(0.35f, 0.85f) }
            // A timeline do editor do PC, para dar chave nas camadas 3D sem sair da cena.
            SceneCard(Modifier.weight(1f - PcSceneLayout.top).fillMaxWidth().padding(start = gap, end = gap, bottom = gap)) {
                KeepLtr { PcAeTimeline(store, ui, Modifier.fillMaxSize()) }
            }
        }
        // Painéis abertos pelos menus (curvas à parte) continuam na janela flutuante do PC.
        PcPanelWindowHost(store, ui, totalW, totalH)
        // Motion Capture: o cartão flutuante (Objeto ▸ Motion Capture… ou a aba Rig).
        PcMocapRigHook()
        PcMocapCardHost(store, totalW, totalH)
        if (PcScene3D.search) CommandSearchSheet(store, ui) { PcScene3D.search = false }
    }
}

/** Tamanhos que a pessoa arrasta nesta área de trabalho (só enquanto o app vive). */
internal object PcSceneLayout {
    var top by mutableStateOf(0.66f)
    var right by mutableStateOf(380f)
    var outliner by mutableStateOf(0.34f)
}

@Composable
internal fun SceneCard(modifier: Modifier, content: @Composable ColumnScope.() -> Unit) {
    val shape = RoundedCornerShape(8.dp)
    Column(modifier.clip(shape).background(AureaColors.EditorPanel).border(1.dp, AureaColors.Hairline, shape), content = content)
}

@OptIn(ExperimentalComposeUiApi::class)
@Composable
private fun SceneSplitter(vertical: Boolean, onDrag: (Float) -> Unit) {
    val cursor = java.awt.Cursor(if (vertical) java.awt.Cursor.E_RESIZE_CURSOR else java.awt.Cursor.N_RESIZE_CURSOR)
    Box((if (vertical) Modifier.width(6.dp).fillMaxHeight() else Modifier.fillMaxWidth().height(6.dp))
        .pointerHoverIcon(PointerIcon(cursor))
        .pointerInput(vertical) {
            if (vertical) detectHorizontalDragGestures { c, d -> c.consume(); onDrag(d) }
            else detectVerticalDragGestures { c, d -> c.consume(); onDrag(d) }
        })
}

// =============================================================================
// Cabeçalho da viewport
// =============================================================================

@Composable
private fun SceneHeader(store: EditorStore) {
    val pose = rigPoseActive(store)
    val selected = store.layers.firstOrNull { it.id == store.primary }
    val canPose = selected?.kind == LayerType.Model3D.kind && store.poseBones != null
    KeepLtr {
        Row(Modifier.fillMaxWidth().height(40.dp).horizontalScroll(rememberScrollState()).padding(horizontal = 6.dp),
            verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            SceneHeaderIcon(CupertinoGlyph.ChevronLeft, stringResource(R.string.editor_voltar_editor), false) { store.exitSceneEditor() }
            CupertinoIcon(CupertinoGlyph.Cube, 17.dp, AureaColors.Accent)
            Text(stringResource(R.string.s3d_scene_title), color = AureaColors.Text, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, maxLines = 1)
            HeaderDivider()
            // Modo (Objeto / Pose; Edição ainda não existe no motor).
            SceneHeaderPill(stringResource(if (pose) R.string.s3d_mode_pose else R.string.s3d_mode_object),
                if (pose) CupertinoGlyph.PersonCropCircle else CupertinoGlyph.CubeFill, "pc3d.mode", listOf(
                    MenuEntry.Item(stringResource(R.string.s3d_mode_object), glyph = CupertinoGlyph.CubeFill, checked = !pose) { if (store.rigPoseMode) store.setRigPose(false) },
                    MenuEntry.Item(stringResource(R.string.s3d_mode_pose), glyph = CupertinoGlyph.PersonCropCircle, sublabel = "Ctrl+Tab", enabled = canPose, checked = pose) {
                        if (!store.rigPoseMode) store.setRigPose(true)
                    },
                    MenuEntry.Item(stringResource(R.string.pc3d_mode_edit), glyph = CupertinoGlyph.Pencil, sublabel = "Tab", enabled = false) {},
                    MenuEntry.Item(stringResource(R.string.pc3d_mode_sculpt), glyph = CupertinoGlyph.Paintbrush, enabled = false) {},
                ))
            HeaderMenu(stringResource(R.string.s3d_menu_view), "pc3d.view", viewEntries(store))
            HeaderMenu(stringResource(R.string.s3d_menu_select), "pc3d.select", selectEntries(store))
            HeaderMenu(stringResource(R.string.panel_adicionar).removePrefix("+ "), "pc3d.add", sceneAddEntries(store))
            HeaderMenu(stringResource(if (pose) R.string.s3d_menu_pose else R.string.s3d_menu_object), "pc3d.object",
                if (pose) poseEntries(store) else sceneObjectEntries(store))
            HeaderDivider()
            // Orientação da transformação, pivô e ímã.
            SceneHeaderPill(stringResource(if (store.gizmoLocalSpace) R.string.s3d_space_local else R.string.s3d_space_global),
                CupertinoGlyph.Move, "pc3d.orientation", orientationEntries(store))
            SceneHeaderPill(stringResource(pivotLabel(PcScene3D.pivot)), CupertinoGlyph.SmallcircleCircle, "pc3d.pivot", pivotEntries())
            SceneHeaderIcon(CupertinoGlyph.Link, stringResource(R.string.i18n_snapping) + "  (Shift+Tab · Ctrl)", PcScene3D.snap) { PcScene3D.snap = !PcScene3D.snap }
            SceneHeaderPill(stepLabel(PcScene3D.snapStep), null, "pc3d.snapstep", listOf(1f, 5f, 10f, 25f, 50f, 100f, 250f).map { step ->
                MenuEntry.Item(stepLabel(step), checked = PcScene3D.snapStep == step) { PcScene3D.snapStep = step }
            } + MenuEntry.Divider + MenuEntry.Item(stringResource(R.string.pc3d_snap_hint), enabled = false) {})
            HeaderDivider()
            // Sobreposições e sombreamento.
            SceneHeaderPill(stringResource(R.string.s3d_overlays), CupertinoGlyph.Eye, "pc3d.overlays", overlayEntries(store))
            ShadingButtons(store)
            HeaderDivider()
            SceneHeaderIcon(CupertinoGlyph.ArrowUturnLeft, stringResource(R.string.editor_desfazer) + "  (Ctrl+Z)", false) { store.undo() }
            SceneHeaderIcon(CupertinoGlyph.ArrowUturnRight, stringResource(R.string.editor_refazer) + "  (Ctrl+Shift+Z)", false) { store.redo() }
            SceneHeaderIcon(CupertinoGlyph.Search, stringResource(R.string.pc3d_search) + "  (F3)", false) { PcScene3D.search = true }
        }
    }
}

private fun pivotLabel(pivot: Int) = when (pivot) {
    PcScene3D.PIVOT_INDIVIDUAL -> R.string.pc3d_pivot_individual
    PcScene3D.PIVOT_ACTIVE -> R.string.pc3d_pivot_active
    PcScene3D.PIVOT_CURSOR -> R.string.pc3d_pivot_cursor
    else -> R.string.pc3d_pivot_median
}

private fun stepLabel(step: Float) = "${step.roundToInt()} px"

@Composable
private fun HeaderDivider() = Box(Modifier.padding(horizontal = 2.dp).width(1.dp).height(18.dp).background(AureaColors.Hairline))

@OptIn(ExperimentalFoundationApi::class)
@Composable
internal fun SceneTooltip(text: String, content: @Composable () -> Unit) {
    TooltipArea(tooltip = {
        Text(text, color = AureaColors.Text, fontSize = 12.sp,
            modifier = Modifier.background(AureaColors.SurfaceHigh, RoundedCornerShape(6.dp)).padding(horizontal = 8.dp, vertical = 4.dp))
    }, delayMillis = 400) { content() }
}

@Composable
internal fun SceneHeaderIcon(glyph: Char, tooltip: String, on: Boolean, onClick: () -> Unit) {
    SceneTooltip(tooltip) {
        Box(Modifier.size(30.dp).clip(RoundedCornerShape(7.dp)).background(if (on) AureaColors.Accent.copy(alpha = 0.2f) else Color.Transparent)
            .semantics { contentDescription = tooltip }.tocavel(shrink = 1f, onClick = onClick), contentAlignment = Alignment.Center) {
            CupertinoIcon(glyph, 16.dp, if (on) AureaColors.Accent else AureaColors.Text)
        }
    }
}

/** Um rótulo com seta que abre um menu do Aurea (a "pílula" da Cena 3D do celular, na altura do PC). */
@Composable
internal fun SceneHeaderPill(label: String, glyph: Char?, tag: String, entries: List<MenuEntry>) {
    var open by remember { mutableStateOf(false) }
    Box {
        Row(Modifier.height(28.dp).clip(RoundedCornerShape(7.dp)).background(AureaColors.EditorRowPill).tocavel(shrink = 1f) { open = !open }
            .padding(horizontal = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            glyph?.let { CupertinoIcon(it, 13.dp, AureaColors.Text); Spacer(Modifier.width(6.dp)) }
            Text(label, color = AureaColors.Text, fontSize = 12.sp, maxLines = 1)
            Spacer(Modifier.width(6.dp))
            CupertinoIcon(CupertinoGlyph.ChevronDown, 9.dp, AureaColors.Muted)
        }
        AnchoredMenu(open, { open = false }, entries = entries, tone = AureaSurfaceTone.Editor, width = 260.dp, tag = "$tag.list")
    }
}

@Composable
private fun HeaderMenu(label: String, tag: String, entries: List<MenuEntry>) {
    var open by remember { mutableStateOf(false) }
    Box {
        Box(Modifier.height(28.dp).clip(RoundedCornerShape(7.dp)).background(if (open) AureaColors.EditorRowPill else Color.Transparent)
            .tocavel(shrink = 1f) { open = !open }.padding(horizontal = 9.dp), contentAlignment = Alignment.Center) {
            Text(label, color = AureaColors.Text, fontSize = 12.5.sp, maxLines = 1)
        }
        AnchoredMenu(open, { open = false }, entries = entries, tone = AureaSurfaceTone.Editor, width = 270.dp, tag = "$tag.list")
    }
}

/** Arame · Sólido · Prévia de material · Renderizado: quatro bolinhas, como no Blender, nas cores do Aurea. */
@Composable
private fun ShadingButtons(store: EditorStore) {
    val labels = listOf(R.string.pc3d_shading_wire, R.string.pc3d_shading_solid, R.string.pc3d_shading_material, R.string.pc3d_shading_rendered)
    Row(Modifier.height(28.dp).clip(RoundedCornerShape(7.dp)).background(AureaColors.EditorRowPill).padding(horizontal = 2.dp),
        verticalAlignment = Alignment.CenterVertically) {
        labels.forEachIndexed { mode, label ->
            val on = PcScene3D.shading == mode
            val supported = mode >= PcScene3D.SHADING_MATERIAL
            val name = stringResource(label)
            SceneTooltip(if (supported) "$name  (Z)" else name + " — " + stringResource(R.string.pc3d_shading_need_engine)) {
                Box(Modifier.size(26.dp, 24.dp).clip(RoundedCornerShape(6.dp)).background(if (on) AureaColors.Accent.copy(alpha = 0.25f) else Color.Transparent)
                    .semantics { contentDescription = name }.tocavel(shrink = 1f) { PcScene3D.chooseShading(store, mode) }, contentAlignment = Alignment.Center) {
                    val ink = if (on) AureaColors.Accent else if (supported) AureaColors.Text else AureaColors.Muted.copy(alpha = 0.55f)
                    Canvas(Modifier.size(14.dp)) {
                        val r = size.minDimension / 2f; val c = Offset(size.width / 2f, size.height / 2f)
                        when (mode) {
                            PcScene3D.SHADING_WIRE -> {
                                drawCircle(ink, r - 1f, c, style = Stroke(1.3.dp.toPx()))
                                drawOval(ink, Offset(c.x - r * 0.45f, c.y - r + 1f), Size(r * 0.9f, r * 2f - 2f), style = Stroke(1.dp.toPx()))
                                drawLine(ink, Offset(c.x - r + 1f, c.y), Offset(c.x + r - 1f, c.y), 1.dp.toPx())
                            }
                            PcScene3D.SHADING_SOLID -> drawCircle(ink, r - 1f, c)
                            PcScene3D.SHADING_MATERIAL -> {
                                drawCircle(ink.copy(alpha = 0.45f), r - 1f, c)
                                drawArc(ink, 90f, 180f, true, Offset(c.x - r + 1f, c.y - r + 1f), Size(r * 2f - 2f, r * 2f - 2f))
                            }
                            else -> {
                                drawCircle(ink, r - 1f, c)
                                drawCircle(Color.White.copy(alpha = 0.85f), r * 0.28f, Offset(c.x - r * 0.3f, c.y - r * 0.32f))
                            }
                        }
                    }
                }
            }
        }
    }
}

// =============================================================================
// Menus
// =============================================================================

@Composable
private fun viewEntries(store: EditorStore): List<MenuEntry> = listOf(
    MenuEntry.Item(stringResource(R.string.s3d_view_frame), glyph = CupertinoGlyph.Fullscreen, sublabel = "Home") { PcScene3D.frameAll(store) },
    MenuEntry.Item(stringResource(R.string.pc3d_frame_selected), sublabel = "Num .", enabled = store.selection.isNotEmpty()) { PcScene3D.frameSelected(store) },
    MenuEntry.Divider,
    MenuEntry.Item(stringResource(R.string.s3d_view_camera), glyph = CupertinoGlyph.Videocam, sublabel = "Num 0", checked = store.sceneCameraView) { store.toggleSceneCameraView() },
    MenuEntry.Item(stringResource(R.string.pc3d_align_camera), sublabel = "Ctrl+Alt+Num 0") { PcScene3D.alignCameraToView(store) },
    MenuEntry.Item(stringResource(R.string.pc3d_orthographic), sublabel = "Num 5", checked = store.sceneOrtho) { PcScene3D.toggleOrtho(store) },
    MenuEntry.Divider,
    MenuEntry.Item(stringResource(R.string.om_view_front), sublabel = "Num 1") { PcScene3D.axisView(store, 0f, 0f) },
    MenuEntry.Item(stringResource(R.string.om_view_back), sublabel = "Ctrl+Num 1") { PcScene3D.axisView(store, 180f, 0f) },
    MenuEntry.Item(stringResource(R.string.om_view_right), sublabel = "Num 3") { PcScene3D.axisView(store, 90f, 0f) },
    MenuEntry.Item(stringResource(R.string.om_view_left), sublabel = "Ctrl+Num 3") { PcScene3D.axisView(store, -90f, 0f) },
    MenuEntry.Item(stringResource(R.string.om_view_top), sublabel = "Num 7") { PcScene3D.axisView(store, 0f, 89.5f) },
    MenuEntry.Item(stringResource(R.string.om_view_bottom), sublabel = "Ctrl+Num 7") { PcScene3D.axisView(store, 0f, -89.5f) },
    MenuEntry.Divider,
    MenuEntry.Item(stringResource(R.string.pc3d_tool_shelf), sublabel = "T", checked = PcScene3D.toolShelf) { PcScene3D.toolShelf = !PcScene3D.toolShelf },
    MenuEntry.Item(stringResource(R.string.pc3d_sidebar), sublabel = "N", checked = PcScene3D.sidebar) { PcScene3D.sidebar = !PcScene3D.sidebar },
    MenuEntry.Item(stringResource(R.string.pc_shortcuts), glyph = CupertinoGlyph.QuestionCircle, sublabel = "F1") { com.aurea.pc.PcShortcuts.helpOpen = true },
)

@Composable
private fun selectEntries(store: EditorStore): List<MenuEntry> = buildList {
    add(MenuEntry.Item(stringResource(R.string.s3d_select_all), sublabel = "A") { PcScene3D.selectAll(store) })
    add(MenuEntry.Item(stringResource(R.string.s3d_select_none), sublabel = "Alt+A", enabled = store.selection.isNotEmpty()) { store.clearSelection() })
    add(MenuEntry.Item(stringResource(R.string.pc3d_select_invert), sublabel = "Ctrl+I") { PcScene3D.invert(store) })
    add(MenuEntry.Item(stringResource(R.string.pc3d_select_box), sublabel = "W") { store.chooseSceneTool(SCENE_TOOL_SELECT) })
    val objects = PcScene3D.objects(store)
    if (objects.isNotEmpty()) add(MenuEntry.Divider)
    objects.forEach { o -> add(MenuEntry.Item(o.name, glyph = LayerType.of(o.kind).glyph, checked = o.id in store.selection) { PcScene3D.click(store, o.id, false) }) }
}

@Composable
internal fun sceneAddEntries(store: EditorStore): List<MenuEntry> {
    val meshes = (0 until Shape3DCatalog.COUNT).map { k ->
        MenuEntry.Item(stringResource(Shape3DCatalog.names[k]), glyph = CupertinoGlyph.CubeFill) { store.addShape3D(k); PcSceneOps.added(store) }
    }
    // Tipos do motor (Engine::add_light): 0 paralela (o "Sol"), 1 pontual, 2 spot, 3 ambiente, 4 Flare 3D.
    val lights = listOf(
        MenuEntry.Item(stringResource(R.string.scene_light_point), glyph = CupertinoGlyph.Lightbulb) { store.addLight(1); PcSceneOps.added(store) },
        MenuEntry.Item(stringResource(R.string.pc3d_light_sun), glyph = CupertinoGlyph.Lightbulb) { store.addLight(0); PcSceneOps.added(store) },
        MenuEntry.Item(stringResource(R.string.scene_light_spot), glyph = CupertinoGlyph.Lightbulb) { store.addLight(2); PcSceneOps.added(store) },
        MenuEntry.Item(stringResource(R.string.scene_light_ambient), glyph = CupertinoGlyph.Lightbulb) { store.addLight(3); PcSceneOps.added(store) },
        MenuEntry.Item(stringResource(R.string.scene_flare3d), glyph = CupertinoGlyph.Sparkles) { store.addLight(EditorStore.LIGHT_FLARE); PcSceneOps.added(store) },
    )
    return listOf(
        MenuEntry.Item(stringResource(R.string.s3d_section_mesh), glyph = CupertinoGlyph.CubeFill, submenu = meshes, chevron = true) {},
        MenuEntry.Item(stringResource(R.string.sh_add_text_3d), glyph = CupertinoGlyph.Textformat) { store.addText3D(); PcSceneOps.added(store) },
        MenuEntry.Item(stringResource(R.string.pc3d_add_light), glyph = CupertinoGlyph.Lightbulb, submenu = lights, chevron = true) {},
        MenuEntry.Item(stringResource(R.string.panel_camera_3d), glyph = CupertinoGlyph.CameraFill) { store.addCamera(); PcSceneOps.added(store) },
        MenuEntry.Item(stringResource(R.string.s3d_empty), glyph = CupertinoGlyph.SmallcircleCircle) { store.addNull(true); PcSceneOps.added(store) },
        MenuEntry.Item(stringResource(R.string.scene_floor_reflective), glyph = CupertinoGlyph.Sparkles) { store.addReflectiveFloor() },
        MenuEntry.Divider,
        MenuEntry.Item(stringResource(R.string.pc3d_import_model), glyph = CupertinoGlyph.ArrowDownToLine, sublabel = stringResource(R.string.s3d_import_formats)) {
            PcScene3D.importRequest++
        },
    )
}

@Composable
internal fun sceneKeyEntries(store: EditorStore): List<MenuEntry> = listOf(
    MenuEntry.Header(stringResource(R.string.s3d_insert_key_desc)),
    MenuEntry.Item(stringResource(R.string.s3d_position), glyph = CupertinoGlyph.SuitDiamond) { PcScene3D.insertKey(store, true, false, false) },
    MenuEntry.Item(stringResource(R.string.s3d_rotation), glyph = CupertinoGlyph.SuitDiamond) { PcScene3D.insertKey(store, false, true, false) },
    MenuEntry.Item(stringResource(R.string.s3d_scale), glyph = CupertinoGlyph.SuitDiamond) { PcScene3D.insertKey(store, false, false, true) },
    MenuEntry.Item(stringResource(R.string.pc3d_key_locrotscale), glyph = CupertinoGlyph.SuitDiamondFill) { PcScene3D.insertKey(store, true, true, true) },
    MenuEntry.Divider,
    MenuEntry.Item(stringResource(if (store.autoKeyTransforms) R.string.s3d_autokey_on else R.string.s3d_autokey_off),
        checked = store.autoKeyTransforms, keepOpen = true) { store.autoKeyTransforms = !store.autoKeyTransforms },
)

@Composable
private fun parentEntries(store: EditorStore): List<MenuEntry> = buildList {
    val id = store.primary
    add(MenuEntry.Item(stringResource(R.string.pc3d_parent_to_active), sublabel = "Ctrl+P", enabled = store.selection.size >= 2) { PcScene3D.parentToActive(store) })
    add(MenuEntry.Item(stringResource(R.string.pc3d_parent_clear), sublabel = "Alt+P", enabled = id != null) { PcScene3D.clearParent(store) })
    if (id != null) {
        val candidates = store.parentCandidates(id).filter { it.isThreeD }
        if (candidates.isNotEmpty()) add(MenuEntry.Divider)
        val current = store.detail?.takeIf { it.id == id }?.parentId ?: 0L
        candidates.take(24).forEach { c ->
            add(MenuEntry.Item(c.name, glyph = LayerType.of(c.kind).glyph, checked = c.id == current) { store.setParentMany(store.selection.toList(), c.id) })
        }
    }
}

@Composable
internal fun sceneObjectEntries(store: EditorStore): List<MenuEntry> {
    val has = store.selection.isNotEmpty()
    val clear = listOf(
        MenuEntry.Item(stringResource(R.string.s3d_position), sublabel = "Alt+G") { PcScene3D.clearTransform(store, PcScene3D.OP_MOVE) },
        MenuEntry.Item(stringResource(R.string.s3d_rotation), sublabel = "Alt+R") { PcScene3D.clearTransform(store, PcScene3D.OP_ROTATE) },
        MenuEntry.Item(stringResource(R.string.s3d_scale), sublabel = "Alt+S") { PcScene3D.clearTransform(store, PcScene3D.OP_SCALE) },
    )
    return listOf(
        MenuEntry.Item(stringResource(R.string.gizmo_tool_move), glyph = CupertinoGlyph.Move, sublabel = "G", enabled = has) { PcScene3D.startModal(store, PcScene3D.OP_MOVE) },
        MenuEntry.Item(stringResource(R.string.gizmo_tool_rotate), glyph = CupertinoGlyph.ArrowCounterclockwise, sublabel = "R", enabled = has) { PcScene3D.startModal(store, PcScene3D.OP_ROTATE) },
        MenuEntry.Item(stringResource(R.string.gizmo_tool_scale), glyph = CupertinoGlyph.ArrowDownRightSquare, sublabel = "S", enabled = has) { PcScene3D.startModal(store, PcScene3D.OP_SCALE) },
        MenuEntry.Item(stringResource(R.string.pc3d_clear), submenu = clear, chevron = true, enabled = has) {},
        MenuEntry.Divider,
        MenuEntry.Item(stringResource(R.string.s3d_duplicate), glyph = CupertinoGlyph.DocOnDoc, sublabel = "Shift+D", enabled = has) { PcScene3D.duplicateAndGrab(store) },
        MenuEntry.Item(stringResource(R.string.pc3d_parent), glyph = CupertinoGlyph.Link, submenu = parentEntries(store), chevron = true, enabled = has) {},
        MenuEntry.Item(stringResource(R.string.pc3d_keyframe), glyph = CupertinoGlyph.SuitDiamond, sublabel = "I", submenu = sceneKeyEntries(store), chevron = true, enabled = has) {},
        MenuEntry.Item(stringResource(R.string.pc3d_mirror), sublabel = "Ctrl+M", submenu = mirrorEntries(store), chevron = true, enabled = has) {},
        MenuEntry.Item(stringResource(R.string.pc3d_collection_move), glyph = CupertinoGlyph.Folder, sublabel = "M", submenu = collectionEntries(store), chevron = true, enabled = has) {},
        MenuEntry.Item(stringResource(R.string.pc3d_cursor), sublabel = "Shift+S", submenu = snapEntries(store), chevron = true) {},
        MenuEntry.Divider,
        MenuEntry.Item(stringResource(R.string.pc_mocap_menu), glyph = CupertinoGlyph.Person3Fill) { PcMocap.open = true },
        MenuEntry.Divider,
        MenuEntry.Item(stringResource(R.string.pc3d_hide), glyph = CupertinoGlyph.EyeSlash, sublabel = "H", enabled = has) { PcScene3D.hide(store, false) },
        MenuEntry.Item(stringResource(R.string.pc3d_reveal), glyph = CupertinoGlyph.Eye, sublabel = "Alt+H") { PcScene3D.reveal(store) },
        MenuEntry.Divider,
        MenuEntry.Item(stringResource(R.string.pc_sc_delete), glyph = CupertinoGlyph.Trash, sublabel = "X · Delete", enabled = has, destructive = true) { PcScene3D.delete(store) },
    )
}

@Composable
private fun poseEntries(store: EditorStore): List<MenuEntry> = listOf(
    MenuEntry.Item(stringResource(R.string.rig_key_bone), glyph = CupertinoGlyph.SuitDiamond, enabled = store.poseBone >= 0) { store.togglePoseKey(false) },
    MenuEntry.Item(stringResource(R.string.rig_key_all), glyph = CupertinoGlyph.SuitDiamond) { store.togglePoseKey(true) },
    MenuEntry.Divider,
    MenuEntry.Item(stringResource(R.string.skeleton_copy)) { store.copyPose() },
    MenuEntry.Item(stringResource(R.string.skeleton_paste), enabled = store.poseClipboard != null) { store.pastePose() },
    MenuEntry.Item(stringResource(R.string.skeleton_mirror_pose)) { store.mirrorPose(true) },
    MenuEntry.Divider,
    MenuEntry.Item(stringResource(R.string.skeleton_zero_pose)) { store.zeroPose(true) },
    MenuEntry.Item(stringResource(R.string.rig_reset_all), glyph = CupertinoGlyph.ArrowCounterclockwise) { store.resetPose(true) },
    MenuEntry.Item(stringResource(R.string.skeleton_show), checked = !store.skeletonHidden, keepOpen = true) { store.showSkeleton(store.skeletonHidden) },
    MenuEntry.Divider,
    MenuEntry.Item(stringResource(R.string.pc_mocap_menu), glyph = CupertinoGlyph.Person3Fill) { PcMocap.open = true },
)

@Composable
private fun snapEntries(store: EditorStore): List<MenuEntry> = listOf(
    MenuEntry.Item(stringResource(R.string.pc3d_cursor_to_selected), enabled = store.selection.isNotEmpty()) { PcSceneOps.cursorToSelected(store, false) },
    MenuEntry.Item(stringResource(R.string.pc3d_cursor_to_active), enabled = store.primary != null) { PcSceneOps.cursorToSelected(store, true) },
    MenuEntry.Item(stringResource(R.string.pc3d_cursor_to_origin), sublabel = "Shift+C") { PcSceneOps.cursorToWorldOrigin() },
    MenuEntry.Divider,
    MenuEntry.Item(stringResource(R.string.pc3d_selection_to_cursor), enabled = store.selection.isNotEmpty()) { PcSceneOps.selectionToCursor(store) },
)

@Composable
private fun mirrorEntries(store: EditorStore): List<MenuEntry> = listOf("X", "Y", "Z").mapIndexed { axis, name ->
    MenuEntry.Item(stringResource(R.string.pc3d_mirror_axis, name)) { PcSceneOps.mirror(store, axis) }
}

/** M: o Aurea não tem coleções; aqui a coleção é um Vazio que serve de pai (ver PcSceneOps). */
@Composable
private fun collectionEntries(store: EditorStore): List<MenuEntry> = buildList {
    val fresh = stringResource(R.string.pc3d_collection_name, PcSceneOps.collections(store).size + 1)
    add(MenuEntry.Item(stringResource(R.string.pc3d_collection_new), glyph = CupertinoGlyph.Plus) { PcSceneOps.newCollection(store, fresh) })
    add(MenuEntry.Item(stringResource(R.string.pc3d_collection_none), sublabel = "Alt+P") { PcScene3D.clearParent(store) })
    val existing = PcSceneOps.collections(store).filter { it.id !in store.selection }
    if (existing.isNotEmpty()) add(MenuEntry.Divider)
    existing.take(30).forEach { c -> add(MenuEntry.Item(c.name, glyph = CupertinoGlyph.Folder) { PcSceneOps.moveToCollection(store, c.id) }) }
}

@Composable
private fun orientationEntries(store: EditorStore): List<MenuEntry> = listOf(
    MenuEntry.Item(stringResource(R.string.s3d_space_global), checked = !store.gizmoLocalSpace) { if (store.gizmoLocalSpace) store.toggleGizmoSpace() },
    MenuEntry.Item(stringResource(R.string.s3d_space_local), checked = store.gizmoLocalSpace) { if (!store.gizmoLocalSpace) store.toggleGizmoSpace() },
)

@Composable
private fun pivotEntries(): List<MenuEntry> = listOf(PcScene3D.PIVOT_MEDIAN, PcScene3D.PIVOT_INDIVIDUAL, PcScene3D.PIVOT_ACTIVE, PcScene3D.PIVOT_CURSOR).map { p ->
    MenuEntry.Item(stringResource(pivotLabel(p)), checked = PcScene3D.pivot == p) { PcScene3D.pivot = p }
}

@Composable
private fun shadingEntries(store: EditorStore): List<MenuEntry> = listOf(
    MenuEntry.Item(stringResource(R.string.pc3d_shading_wire), enabled = false, sublabel = stringResource(R.string.pc3d_needs_engine_short)) {},
    MenuEntry.Item(stringResource(R.string.pc3d_shading_solid), enabled = false, sublabel = stringResource(R.string.pc3d_needs_engine_short)) {},
    MenuEntry.Item(stringResource(R.string.pc3d_shading_material), checked = PcScene3D.shading == PcScene3D.SHADING_MATERIAL) { PcScene3D.chooseShading(store, PcScene3D.SHADING_MATERIAL) },
    MenuEntry.Item(stringResource(R.string.pc3d_shading_rendered), checked = PcScene3D.shading == PcScene3D.SHADING_RENDERED) { PcScene3D.chooseShading(store, PcScene3D.SHADING_RENDERED) },
)

@Composable
private fun overlayEntries(store: EditorStore): List<MenuEntry> {
    fun guide(label: String, bits: Int) = MenuEntry.Item(label, checked = !PcScene3D.guideHidden(bits), keepOpen = true) { PcScene3D.toggleGuide(bits) }
    return listOf(
        MenuEntry.Item(stringResource(R.string.pc3d_overlay_all), glyph = CupertinoGlyph.Eye, sublabel = "Alt+Z", checked = !ViewModes.hideOverlay, keepOpen = true) {
            ViewModes.hideOverlay = !ViewModes.hideOverlay
        },
        MenuEntry.Divider,
        guide(stringResource(R.string.pc3d_overlay_floor), PcScene3D.GUIDE_FLOOR),
        guide(stringResource(R.string.pc3d_overlay_axes), PcScene3D.GUIDE_AXES),
        guide(stringResource(R.string.pc3d_overlay_frame), PcScene3D.GUIDE_FRAME),
        guide(stringResource(R.string.pc3d_overlay_cameras), PcScene3D.GUIDE_CAMERA),
        guide(stringResource(R.string.pc3d_overlay_lights), PcScene3D.GUIDE_LIGHT),
        MenuEntry.Item(stringResource(R.string.pc3d_overlay_bones), checked = !store.skeletonHidden, keepOpen = true) { store.showSkeleton(store.skeletonHidden) },
        MenuEntry.Item(stringResource(R.string.pc3d_overlay_gizmo), checked = !store.sceneSelectOnly, keepOpen = true) {
            store.chooseSceneTool(if (store.sceneSelectOnly) store.gizmoTool else SCENE_TOOL_SELECT)
        },
    )
}

// =============================================================================
// Viewport
// =============================================================================

@Composable
private fun SceneViewport(store: EditorStore, stage: @Composable (Modifier) -> Unit, modifier: Modifier) {
    val focus = LocalFocusManager.current
    Box(modifier.clipToBounds().background(AureaColors.EditorCanvas)
        .onSizeChanged { PcScene3D.viewSize = it }
        .pointerInput(store) {
            // Só OBSERVA (passe inicial): onde o ponteiro está, a roda do mouse e o modal seguindo o ponteiro.
            awaitPointerEventScope {
                while (true) {
                    val ev = awaitPointerEvent(PointerEventPass.Initial)
                    val at = ev.changes.firstOrNull()?.position
                    val mods = ev.keyboardModifiers
                    when (ev.type) {
                        PointerEventType.Exit -> PcScene3D.pointerInside = false
                        PointerEventType.Enter -> PcScene3D.pointerInside = true
                        PointerEventType.Press -> { PcScene3D.pointerInside = true; if (at != null) PcScene3D.mouse = at; focus.clearFocus() }
                        PointerEventType.Move -> if (at != null) {
                            PcScene3D.pointerInside = true
                            PcScene3D.mouse = at
                            if (PcScene3D.modal != null) PcScene3D.modalMove(store, at, mods.isShiftPressed, mods.isCtrlPressed)
                        }
                        PointerEventType.Scroll -> {
                            val change = ev.changes.firstOrNull()
                            // Alt+roda é o zoom da timeline (Main.kt), que já consumiu o evento.
                            if (change != null && !change.isConsumed && !mods.isAltPressed && PcScene3D.modal == null) {
                                val dy = change.scrollDelta.y
                                val m = PcScene3D.map(store)
                                when {
                                    dy == 0f -> {}
                                    mods.isShiftPressed && m != null -> store.panSceneView(0f, -dy * 40f / m.fit)
                                    mods.isCtrlPressed && m != null -> store.panSceneView(-dy * 40f / m.fit, 0f)
                                    else -> PcScene3D.zoom(store, if (dy > 0f) 1.12f else 1f / 1.12f)
                                }
                                change.consume()
                            }
                        }
                        else -> {}
                    }
                }
            }
        }) {
        // A cena 3D e os gizmos não espelham em árabe (ver `KeepLtr`).
        KeepLtr { stage(Modifier.fillMaxSize()) }
        ViewLabel(store, Modifier.align(Alignment.TopStart).padding(start = if (PcScene3D.toolShelf) 52.dp else 10.dp, top = 8.dp))
        if (PcScene3D.toolShelf) KeepLtr { SceneToolbar(store, Modifier.align(Alignment.TopStart).padding(top = 6.dp)) }
        NavGizmo(store, Modifier.align(Alignment.TopEnd).padding(top = 8.dp, end = if (PcScene3D.sidebar) 238.dp else 10.dp))
        if (PcScene3D.sidebar) PcSceneSidebar(store, Modifier.align(Alignment.TopEnd).padding(6.dp))
        SceneOverlay(store)
        ExtraTools(store, Modifier.align(Alignment.BottomStart).padding(start = 6.dp, bottom = 8.dp))
        // Caixa de seleção.
        PcScene3D.box?.let { r ->
            val ink = AureaColors.Accent
            Canvas(Modifier.fillMaxSize()) {
                drawRect(ink.copy(alpha = 0.12f), r.topLeft, r.size)
                drawRect(ink, r.topLeft, r.size, style = Stroke(1.2.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(5.dp.toPx(), 3.dp.toPx()))))
            }
        }
        ModalStatus(store, Modifier.align(Alignment.BottomCenter).padding(8.dp))
        PointerMenu(store)
    }
}

/** As 12 arestas de uma caixa cujos cantos são numerados por bits (1 = x, 2 = y, 4 = z). */
private val BoxEdges = intArrayOf(0, 1, 2, 3, 4, 5, 6, 7, 0, 2, 1, 3, 4, 6, 5, 7, 0, 4, 1, 5, 2, 6, 3, 7)

/**
 * O que a viewport desenha por cima da cena (só no PC): o contorno da seleção
 * (a caixa orientada de cada objeto, no destaque do Aurea; o ATIVO mais claro),
 * o cursor 3D, a medida e, na vista da câmera, o escuro fora do quadro.
 */
@Composable
private fun SceneOverlay(store: EditorStore) {
    val measurer = rememberTextMeasurer()
    val accent = AureaColors.Accent
    val activeInk = AureaColors.Keyframe
    Canvas(Modifier.fillMaxSize()) {
        // Lidos aqui para redesenhar quando a vista, a seleção, o cabeçote ou um valor mudam.
        store.sceneViewTick; store.detail; store.playhead; store.sceneCameraView; store.layers
        val selection = store.selection
        val primary = store.primary
        val m = PcScene3D.map(store) ?: return@Canvas
        if (store.sceneCameraView) {
            // Passepartout: fora do quadro da câmera fica escuro.
            val l = m.sx(0f); val t = m.sy(0f); val r = m.sx(m.compW); val b = m.sy(m.compH)
            val veil = Color.Black.copy(alpha = 0.55f)
            drawRect(veil, Offset.Zero, Size(size.width, t.coerceAtLeast(0f)))
            drawRect(veil, Offset(0f, b), Size(size.width, (size.height - b).coerceAtLeast(0f)))
            drawRect(veil, Offset(0f, t), Size(l.coerceAtLeast(0f), b - t))
            drawRect(veil, Offset(r, t), Size((size.width - r).coerceAtLeast(0f), b - t))
            drawRect(accent.copy(alpha = 0.8f), Offset(l, t), Size(r - l, b - t), style = Stroke(1.2.dp.toPx()))
        }
        if (!ViewModes.hideOverlay && !rigPoseActive(store)) {
            for (id in selection) {
                val box = PcSceneOps.bounds(store, id)?.takeIf { it[25] > 0.5f } ?: continue
                val ink = if (id == primary) activeInk else accent
                var k = 0
                while (k < BoxEdges.size) {
                    val a = BoxEdges[k]; val b = BoxEdges[k + 1]
                    val pa = Offset(m.sx(box[9 + a * 2]), m.sy(box[10 + a * 2]))
                    val pb = Offset(m.sx(box[9 + b * 2]), m.sy(box[10 + b * 2]))
                    drawLine(Color.Black.copy(alpha = 0.45f), pa, pb, 3.dp.toPx())
                    drawLine(ink, pa, pb, 1.5.dp.toPx())
                    k += 2
                }
            }
        }
        if (!ViewModes.hideOverlay) {
            // Cursor 3D: anel tracejado vermelho e branco com a cruz, como o do Blender, desenhado aqui.
            PcSceneOps.project(store, PcSceneOps.cursorPoint(store))?.let { c ->
                val p = Offset(m.sx(c.x), m.sy(c.y))
                val ring = 9.dp.toPx(); val arm = 15.dp.toPx(); val gapArm = 4.dp.toPx()
                drawCircle(Color.White, ring, p, style = Stroke(2.dp.toPx()))
                drawCircle(Color(0xFFFF5A5A), ring, p, style = Stroke(2.dp.toPx(), pathEffect = PathEffect.dashPathEffect(floatArrayOf(5.dp.toPx(), 5.dp.toPx()))))
                for ((dx, dy) in listOf(1f to 0f, -1f to 0f, 0f to 1f, 0f to -1f)) {
                    drawLine(Color.Black.copy(alpha = 0.6f), p + Offset(dx * gapArm, dy * gapArm), p + Offset(dx * arm, dy * arm), 2.5.dp.toPx())
                    drawLine(Color.White, p + Offset(dx * gapArm, dy * gapArm), p + Offset(dx * arm, dy * arm), 1.dp.toPx())
                }
            }
        }
        PcSceneOps.measure?.let { (a, b) ->
            val pa = PcSceneOps.project(store, a) ?: return@let
            val pb = PcSceneOps.project(store, b) ?: return@let
            val sa = Offset(m.sx(pa.x), m.sy(pa.y)); val sb = Offset(m.sx(pb.x), m.sy(pb.y))
            drawLine(Color.Black.copy(alpha = 0.55f), sa, sb, 3.5.dp.toPx())
            drawLine(activeInk, sa, sb, 1.5.dp.toPx())
            drawCircle(activeInk, 3.5.dp.toPx(), sa); drawCircle(activeInk, 3.5.dp.toPx(), sb)
            val d = kotlin.math.sqrt((0..2).sumOf { ((a[it] - b[it]) * (a[it] - b[it])).toDouble() }).toFloat()
            val label = measurer.measure(String.format(java.util.Locale.ROOT, "%.1f px", d), TextStyle(color = Color.White, fontSize = 11.sp, fontWeight = FontWeight.SemiBold))
            val mid = (sa + sb) / 2f
            drawRoundRect(Color(0xE6151C24), mid - Offset(label.size.width / 2f + 6.dp.toPx(), label.size.height / 2f + 3.dp.toPx()),
                Size(label.size.width + 12.dp.toPx(), label.size.height + 6.dp.toPx()), androidx.compose.ui.geometry.CornerRadius(6.dp.toPx()))
            drawText(label, topLeft = mid - Offset(label.size.width / 2f, label.size.height / 2f))
        }
    }
}

/** Ferramentas que a barra do Aurea não tem: Cursor 3D e Medir (clicar de novo volta às ferramentas de cima). */
@Composable
private fun ExtraTools(store: EditorStore, modifier: Modifier) {
    if (!PcScene3D.toolShelf) return
    // Escolher uma ferramenta de gizmo ou a mãozinha na barra de cima solta a ferramenta daqui.
    LaunchedEffect(store.sceneSelectOnly, store.sceneHand, store.gizmoTool) {
        if (!store.sceneSelectOnly || store.sceneHand) PcSceneOps.tool = PcSceneOps.TOOL_NONE
    }
    Column(modifier.clip(RoundedCornerShape(10.dp)).background(Color(0xE6151C24)).padding(3.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
        fun pick(tool: Int) {
            if (PcSceneOps.tool == tool) { PcSceneOps.tool = PcSceneOps.TOOL_NONE; return }
            store.chooseSceneTool(SCENE_TOOL_SELECT)
            PcSceneOps.tool = tool
            if (tool != PcSceneOps.TOOL_MEASURE) PcSceneOps.measure = null
        }
        SceneTooltip(stringResource(R.string.pc3d_tool_cursor) + "  (Shift+RMB)") {
            val on = PcSceneOps.tool == PcSceneOps.TOOL_CURSOR
            Box(Modifier.size(34.dp).clip(RoundedCornerShape(9.dp)).background(if (on) AureaColors.Brand else Color.Transparent).tocavel(shrink = 1f) { pick(PcSceneOps.TOOL_CURSOR) },
                contentAlignment = Alignment.Center) { CupertinoIcon(CupertinoGlyph.SmallcircleCircle, 20.dp, if (on) AureaColors.OnAction else AureaColors.Text) }
        }
        SceneTooltip(stringResource(R.string.pc3d_tool_measure)) {
            val on = PcSceneOps.tool == PcSceneOps.TOOL_MEASURE
            Box(Modifier.size(34.dp).clip(RoundedCornerShape(9.dp)).background(if (on) AureaColors.Brand else Color.Transparent).tocavel(shrink = 1f) { pick(PcSceneOps.TOOL_MEASURE) },
                contentAlignment = Alignment.Center) { CupertinoIcon(CupertinoGlyph.ArrowLeftRight, 19.dp, if (on) AureaColors.OnAction else AureaColors.Text) }
        }
    }
}

/** "Perspectiva do usuário", "Frente · ortográfica"…: o nome da vista, no canto, como no Blender. */
@Composable
private fun ViewLabel(store: EditorStore, modifier: Modifier) {
    val yaw = store.sceneYaw; val pitch = store.scenePitch
    fun near(a: Float, b: Float) = kotlin.math.abs(((a - b + 540f).mod(360f)) - 180f) < 0.6f
    val side = when {
        store.sceneCameraView -> R.string.s3d_view_camera
        pitch > 89f -> R.string.om_view_top
        pitch < -89f -> R.string.om_view_bottom
        kotlin.math.abs(pitch) > 0.6f -> R.string.pc3d_view_user
        near(yaw, 0f) -> R.string.om_view_front
        near(yaw, 180f) -> R.string.om_view_back
        near(yaw, 90f) -> R.string.om_view_right
        near(yaw, -90f) -> R.string.om_view_left
        else -> R.string.pc3d_view_user
    }
    val projection = stringResource(if (store.sceneOrtho) R.string.pc3d_orthographic else R.string.pc3d_perspective)
    val name = stringResource(side)
    val active = store.layers.firstOrNull { it.id == store.primary }?.name
    Column(modifier) {
        Text(if (store.sceneCameraView) name else "$name · $projection", color = AureaColors.Text.copy(alpha = 0.85f), fontSize = 11.sp, maxLines = 1)
        if (active != null) Text(if (store.selection.size > 1) "$active  (+${store.selection.size - 1})" else active,
            color = AureaColors.Muted, fontSize = 10.5.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

/** A faixa do mover/girar/escalar: operação, eixo travado e o valor (digitado ou do ponteiro). */
@Composable
private fun ModalStatus(store: EditorStore, modifier: Modifier) {
    val s = PcScene3D.modal ?: return
    PcScene3D.modalTick
    val op = stringResource(when (s.op) { PcScene3D.OP_ROTATE -> R.string.gizmo_tool_rotate; PcScene3D.OP_SCALE -> R.string.gizmo_tool_scale; else -> R.string.gizmo_tool_move })
    val axis = when {
        s.axis < 0 -> ""
        s.plane -> "  " + "XYZ".filterIndexed { i, _ -> i != s.axis }
        else -> "  " + "XYZ"[s.axis]
    }
    val space = if (s.axis >= 0 && s.op == PcScene3D.OP_MOVE) " " + stringResource(if (store.gizmoLocalSpace) R.string.s3d_space_local else R.string.s3d_space_global) else ""
    fun n(v: Float) = String.format(java.util.Locale.ROOT, if (s.op == PcScene3D.OP_SCALE) "%.3f" else "%.1f", v)
    val unit = when (s.op) { PcScene3D.OP_ROTATE -> "°"; PcScene3D.OP_SCALE -> "×"; else -> " px" }
    val value = if (s.typed.isNotEmpty()) "[${s.typed}]$unit" else if (!s.value2.isNaN()) "${n(s.value)} · ${n(s.value2)}$unit" else n(s.value) + unit
    Column(modifier.clip(RoundedCornerShape(8.dp)).background(Color(0xE6151C24)).padding(horizontal = 10.dp, vertical = 6.dp)) {
        Text("$op$axis$space   $value", color = AureaColors.Text, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
        Text(stringResource(R.string.pc3d_modal_hint), color = AureaColors.Muted, fontSize = 10.5.sp)
    }
}

/** Os menus que abrem onde o ponteiro está (Shift+A, I, X, Z, ponto, vírgula e o botão direito). */
@Composable
private fun PointerMenu(store: EditorStore) {
    val menu = PcScene3D.menu ?: return
    val entries = when (menu.kind) {
        PcSceneMenu.ADD -> listOf<MenuEntry>(MenuEntry.Header(stringResource(R.string.panel_adicionar).removePrefix("+ "))) + sceneAddEntries(store)
        PcSceneMenu.KEY -> sceneKeyEntries(store)
        PcSceneMenu.DELETE -> listOf(
            MenuEntry.Header(stringResource(R.string.pc_sc_delete)),
            MenuEntry.Item(stringResource(R.string.pc3d_delete_selected, store.selection.size), glyph = CupertinoGlyph.Trash, destructive = true) { PcScene3D.delete(store) },
        )
        PcSceneMenu.SHADING -> listOf<MenuEntry>(MenuEntry.Header(stringResource(R.string.pc3d_shading))) + shadingEntries(store)
        PcSceneMenu.PIVOT -> listOf<MenuEntry>(MenuEntry.Header(stringResource(R.string.pc3d_pivot))) + pivotEntries()
        PcSceneMenu.ORIENTATION -> listOf<MenuEntry>(MenuEntry.Header(stringResource(R.string.pc3d_orientation))) + orientationEntries(store)
        PcSceneMenu.PARENT -> listOf<MenuEntry>(MenuEntry.Header(stringResource(R.string.pc3d_parent))) + parentEntries(store)
        PcSceneMenu.SNAP -> listOf<MenuEntry>(MenuEntry.Header(stringResource(R.string.pc3d_cursor))) + snapEntries(store)
        PcSceneMenu.MIRROR -> listOf<MenuEntry>(MenuEntry.Header(stringResource(R.string.pc3d_mirror))) + mirrorEntries(store)
        PcSceneMenu.COLLECTION -> listOf<MenuEntry>(MenuEntry.Header(stringResource(R.string.pc3d_collection_move))) + collectionEntries(store)
        else -> if (rigPoseActive(store)) poseEntries(store) else sceneObjectEntries(store)
    }
    Box(Modifier.offset { IntOffset(menu.at.x.roundToInt(), menu.at.y.roundToInt()) }.size(1.dp)) {
        AnchoredMenu(true, { if (PcScene3D.menu === menu) PcScene3D.menu = null }, entries = entries, tone = AureaSurfaceTone.Editor,
            width = 260.dp, tag = "pc3d.pointer.menu")
    }
}

/**
 * O gizmo de navegação do canto: os três eixos do mundo do Aurea (X direita,
 * Y para baixo, Z para o fundo) como a vista os enxerga. Clicar numa bolinha
 * olha por aquele eixo; arrastar orbita.
 */
@Composable
private fun NavGizmo(store: EditorStore, modifier: Modifier) {
    val measurer = rememberTextMeasurer()
    val colors = listOf(Color(0xFFFF5A5A), Color(0xFF5AD27A), Color(0xFF5AA8FF))
    val description = stringResource(R.string.s3d_nav_orbit)
    // Eixos do mundo na tela: (v·x, v·y) com a base da câmera de órbita do motor (Renderer.cpp).
    fun axes(): List<Triple<Int, Offset, Float>> {
        val yaw = Math.toRadians(store.sceneYaw.toDouble()); val pitch = Math.toRadians(store.scenePitch.toDouble())
        val zx = (-sin(yaw) * cos(pitch)).toFloat(); val zy = sin(pitch).toFloat(); val zz = (cos(yaw) * cos(pitch)).toFloat()
        val xl = hypot(zz, zx).coerceAtLeast(1e-5f)
        val xx = zz / xl; val xz = -zx / xl                       // x = (0,1,0) × z
        val yx = zy * xz; val yy = zz * xx - zx * xz; val yz = -zy * xx   // y = z × x
        return listOf(
            Triple(0, Offset(xx, yx), zx), Triple(1, Offset(0f, yy), zy), Triple(2, Offset(xz, yz), zz),
        )
    }
    Canvas(modifier.size(78.dp).semantics { contentDescription = description }.pointerInput(store) {
        awaitEachGesture {
            val down = awaitFirstDown()
            down.consume()
            var moved = false
            var last = down.position
            val slop = 4.dp.toPx()
            while (true) {
                val e = awaitPointerEvent()
                val c = e.changes.firstOrNull { it.id == down.id } ?: break
                if (!c.pressed) break
                c.consume()
                if (!moved && (c.position - down.position).getDistance() > slop) moved = true
                if (moved) {
                    val d = c.position - last
                    PcScene3D.orbit(store, -d.x * 0.6f / 1.dp.toPx(), d.y * 0.6f / 1.dp.toPx())
                }
                last = c.position
            }
            if (!moved) {
                val center = Offset(size.width / 2f, size.height / 2f)
                val reach = size.width / 2f - 11.dp.toPx()
                var best = -1; var bestSign = 1f; var bestDistance = 13.dp.toPx()
                for ((axis, dir, _) in axes()) for (sign in listOf(1f, -1f)) {
                    val p = center + dir * (reach * sign)
                    val dd = (p - down.position).getDistance()
                    if (dd < bestDistance) { bestDistance = dd; best = axis; bestSign = sign }
                }
                // Bolinha +X = olhar do lado +X (Direita); −Y é "para cima" (Topo); −Z é a Frente.
                when (best) {
                    0 -> PcScene3D.axisView(store, if (bestSign > 0) 90f else -90f, 0f)
                    1 -> PcScene3D.axisView(store, 0f, if (bestSign > 0) -89.5f else 89.5f)
                    2 -> PcScene3D.axisView(store, if (bestSign > 0) 180f else 0f, 0f)
                }
            }
        }
    }) {
        val center = Offset(size.width / 2f, size.height / 2f)
        val reach = size.width / 2f - 11.dp.toPx()
        drawCircle(Color(0x33000000), size.width / 2f, center)
        // De trás para a frente: o que aponta para o fundo (z maior) desenha primeiro.
        val ends = axes().flatMap { (axis, dir, depth) -> listOf(Triple(axis, 1f, depth) to dir, Triple(axis, -1f, -depth) to dir * -1f) }
            .sortedByDescending { it.first.third }
        for ((info, dir) in ends) {
            val (axis, sign, _) = info
            val p = center + dir * reach
            val color = colors[axis]
            if (sign > 0f) {
                drawLine(color, center, p, 2.dp.toPx())
                drawCircle(color, 8.5.dp.toPx(), p)
                val label = measurer.measure("XYZ"[axis].toString(), TextStyle(color = Color(0xFF10151B), fontSize = 10.sp, fontWeight = FontWeight.Bold))
                drawText(label, topLeft = Offset(p.x - label.size.width / 2f, p.y - label.size.height / 2f))
            } else {
                drawCircle(color.copy(alpha = 0.28f), 7.dp.toPx(), p)
                drawCircle(color, 7.dp.toPx(), p, style = Stroke(1.2.dp.toPx()))
            }
        }
    }
}

// =============================================================================
// Mouse na viewport (chamado de dentro do gesto do palco: SceneWorkspaceHost)
// =============================================================================

/**
 * Antes do palco: o clique que confirma (ou o direito que cancela) o modal; a
 * navegação do Blender — botão do meio orbita, Shift+meio leva a vista,
 * Ctrl+meio aproxima, Alt+esquerdo orbita (mouse sem botão do meio); e o menu
 * de contexto do botão direito. Falso = o palco segue (alças, trackball, ossos).
 */
private suspend fun AwaitPointerEventScope.pcScenePress(store: EditorStore, m: StageMapper, down: PointerInputChange): Boolean {
    val buttons = currentEvent.buttons
    val mods = currentEvent.keyboardModifiers
    val s = PcScene3D
    if (s.modal != null) {
        down.consume()
        if (buttons.isSecondaryPressed) s.cancelModal(store) else if (buttons.isPrimaryPressed) s.confirmModal(store)
        dragUntilRelease(down) {}
        return true
    }
    s.menu = null
    val hand = buttons.isPrimaryPressed && store.sceneHand
    if (buttons.isTertiaryPressed || (buttons.isPrimaryPressed && mods.isAltPressed) || hand) {
        down.consume()
        val degPerPx = 0.4f / 1.dp.toPx()
        val mode = if (mods.isShiftPressed || hand) 1 else if (mods.isCtrlPressed) 2 else 0
        dragUntilRelease(down) { d ->
            when (mode) {
                1 -> store.panSceneView(d.x / m.fit, d.y / m.fit)
                2 -> s.zoom(store, exp(d.y * 0.006f / 1.dp.toPx()))
                else -> s.orbit(store, -d.x * degPerPx, d.y * degPerPx)
            }
        }
        return true
    }
    if (buttons.isSecondaryPressed && mods.isShiftPressed) {
        // Shift+botão direito: o cursor 3D vai para o ponto (arrastar leva junto).
        down.consume()
        PcSceneOps.placeCursor(store, m.cx(down.position.x), m.cy(down.position.y))
        var at = down.position
        dragUntilRelease(down) { d -> at += d; PcSceneOps.placeCursor(store, m.cx(at.x), m.cy(at.y)) }
        return true
    }
    if (buttons.isSecondaryPressed) {
        down.consume()
        if (!dragUntilRelease(down) {}) {
            // Botão direito: escolhe o que está embaixo (se nada estava escolhido ali) e abre o menu do objeto.
            val picked = store.scenePick(m.cx(down.position.x), m.cy(down.position.y), 18.dp.toPx() / m.fit)
            if (picked != null && picked !in store.selection && !rigPoseActive(store)) s.click(store, picked, false)
            s.menu = PcSceneMenu(PcSceneMenu.CONTEXT, down.position)
        }
        return true
    }
    return false
}

/** Segue o ponteiro até soltar; `true` = arrastou (passou da folga). */
private suspend fun AwaitPointerEventScope.dragUntilRelease(down: PointerInputChange, onMove: (Offset) -> Unit): Boolean {
    var moved = false
    var last = down.position
    val slop = 4.dp.toPx()
    while (true) {
        val e = awaitPointerEvent()
        val c = e.changes.firstOrNull { it.id == down.id } ?: break
        if (!c.pressed) break
        c.consume()
        if (!moved && (c.position - down.position).getDistance() > slop) moved = true
        val d = c.position - last
        last = c.position
        if (d != Offset.Zero) onMove(d)
    }
    return moved
}

/**
 * No lugar do gesto de toque: clique escolhe (Shift soma e torna ativo; no vazio
 * solta tudo), arrastar abre a caixa de seleção (Shift soma, Ctrl tira).
 */
private suspend fun AwaitPointerEventScope.pcSceneGesture(store: EditorStore, m: StageMapper, down: PointerInputChange) {
    val mods = currentEvent.keyboardModifiers
    val shift = mods.isShiftPressed
    val ctrl = mods.isCtrlPressed
    val slop = 4.dp.toPx()
    var moved = false
    var last = down.position
    down.consume()
    if (PcSceneOps.tool == PcSceneOps.TOOL_CURSOR) {
        PcSceneOps.placeCursor(store, m.cx(down.position.x), m.cy(down.position.y))
        dragUntilRelease(down) { d -> last += d; PcSceneOps.placeCursor(store, m.cx(last.x), m.cy(last.y)) }
        return
    }
    if (PcSceneOps.tool == PcSceneOps.TOOL_MEASURE) {
        // Medir: do ponto apertado ao ponto solto, na superfície dos modelos ou na profundidade do cursor.
        val through = PcSceneOps.cursorPoint(store)
        val from = PcSceneOps.pointAt(store, m.cx(down.position.x), m.cy(down.position.y), through, true)
        if (from == null) { PcScene3D.toast(R.string.pc3d_need_engine_update); dragUntilRelease(down) {}; return }
        PcSceneOps.measure = from to from
        dragUntilRelease(down) { d ->
            last += d
            PcSceneOps.pointAt(store, m.cx(last.x), m.cy(last.y), from, true)?.let { PcSceneOps.measure = from to it }
        }
        return
    }
    while (true) {
        val e = awaitPointerEvent()
        val c = e.changes.firstOrNull { it.id == down.id } ?: break
        if (!c.pressed) break
        c.consume()
        last = c.position
        if (!moved && (last - down.position).getDistance() > slop) moved = true
        if (moved) PcScene3D.box = Rect(min(down.position.x, last.x), min(down.position.y, last.y), max(down.position.x, last.x), max(down.position.y, last.y))
    }
    val box = PcScene3D.box
    PcScene3D.box = null
    if (!moved || box == null) {
        PcScene3D.click(store, store.scenePick(m.cx(down.position.x), m.cy(down.position.y), 18.dp.toPx() / m.fit), shift)
        return
    }
    val inside = PcScene3D.objects(store).filter { it.visible && !it.locked }.mapNotNull { row ->
        // Pela caixa do objeto projetada (o que encosta na caixa de seleção entra); motor antigo: pela origem.
        val r = PcSceneOps.screenRect(store, row.id)
        if (r != null) return@mapNotNull row.id.takeIf { Rect(m.sx(r.left), m.sy(r.top), m.sx(r.right), m.sy(r.bottom)).overlaps(box) || box.contains(Offset(m.sx(r.left), m.sy(r.top))) }
        val g = store.sceneGizmoOf(row.id) ?: return@mapNotNull null
        row.id.takeIf { box.contains(Offset(m.sx(g[0]), m.sy(g[1]))) }
    }
    val now = store.selection.toList()
    PcScene3D.setSelection(store, when {
        ctrl -> now - inside.toSet()
        shift -> now + inside.filter { it !in now }
        else -> inside
    })
}
