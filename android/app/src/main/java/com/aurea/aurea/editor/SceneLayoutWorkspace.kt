package com.aurea.aurea.editor

import com.aurea.aurea.ui.i18n.KeepLtr
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextOverflow
import kotlin.math.abs
import kotlin.math.roundToInt
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.Alignment
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.sp
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import com.aurea.aurea.ui.ds.AnchoredMenu
import com.aurea.aurea.ui.ds.AureaSurfaceTone
import com.aurea.aurea.ui.ds.MenuEntry
import kotlinx.coroutines.delay
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.window.Dialog
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.editor.panels.PanelContent
import com.aurea.aurea.editor.panels.AureaPropertyPanel
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.displayCutoutPadding
import androidx.compose.runtime.DisposableEffect
import com.aurea.aurea.R
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.aurea.aurea.editor.panels.curveStart
import com.aurea.aurea.editor.panels.sameTrack
import com.aurea.aurea.state.EditorStore

/**
 * Cena 3D no jeito do Blender no celular, com as cores do AUREA (nov/2026):
 * duas linhas no alto (voltar, modo, Global/Local, desfazer · menus Visualizar,
 * Selecionar, Adicionar, Objeto), a viewport inteira no meio com ferramentas à
 * esquerda e navegação à direita (Scene3DViewport.kt) e, embaixo, a área que
 * recolhe: Linha do tempo, Propriedades, Outliner e Esqueleto. Nada cobre a cena:
 * o Esqueleto mora nessa área. Gestos: `sceneGesture` no Stage.kt e
 * `rigPoseGesture` no RigPoseStage.kt.
 */
@Composable
internal fun SceneLayoutWorkspace(store: EditorStore, ui: EditorUi, stage: @Composable (Modifier) -> Unit) {
    var materials by remember { mutableStateOf(false) }
    var lights by remember { mutableStateOf(false) }
    var addMenu by remember { mutableStateOf(false) }
    var area by remember { mutableIntStateOf(0) }
    var materialPanel by remember { mutableStateOf(EditorPanel.Element3D) }
    var preview by androidx.compose.runtime.saveable.rememberSaveable { mutableStateOf(true) }
    // Em pé as viewports empilham; deitado ficam lado a lado.
    val wide = sceneWide()
    var sideBySide by androidx.compose.runtime.saveable.rememberSaveable(wide) { mutableStateOf(wide) }
    var split by androidx.compose.runtime.saveable.rememberSaveable { mutableStateOf(.5f) }
    var fullscreen by androidx.compose.runtime.saveable.rememberSaveable { mutableStateOf(false) }
    // Deitado a altura é pouca: as barras do sistema saem para dar espaço às viewports.
    val context = androidx.compose.ui.platform.LocalContext.current
    DisposableEffect(context, wide) {
        val bars = context.sceneActivity()?.window?.let { androidx.core.view.WindowCompat.getInsetsController(it, it.decorView) }
        if (wide) {
            bars?.systemBarsBehavior = androidx.core.view.WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            bars?.hide(androidx.core.view.WindowInsetsCompat.Type.systemBars())
        }
        onDispose { if (wide) bars?.show(androidx.core.view.WindowInsetsCompat.Type.systemBars()) }
    }
    if (ui.adding) Dialog(onDismissRequest = { ui.adding = false }) {
        Surface { Box(Modifier.fillMaxWidth().fillMaxHeight(.8f)) { AddLayerPanel(store, ui) } }
    }
    if (materials) SceneSheet(onDismiss = { materials = false }) {
        PanelContent(store = store, panel = materialPanel, onClose = { materials = false }, onOpenPanel = { materialPanel = it }, onOpenEffectsBrowser = { ui.effectsBrowser = true }, modifier = Modifier.fillMaxWidth().fillMaxHeight(.8f),
            alwaysHeader = true)
    }
    if (lights) SceneSheet(onDismiss = { lights = false }) {
        Column(Modifier.fillMaxWidth().fillMaxHeight(.8f)) {
            SceneSheetHeader(stringResource(R.string.pn_t3d_lighting)) { lights = false }
            Column(Modifier.fillMaxWidth().weight(1f).verticalScroll(rememberScrollState()).padding(horizontal = 12.dp).padding(bottom = 12.dp)) {
                val light = remember(store.primary, store.curveRevision, store.detail, store.sceneSettingsRevision, store.playhead) { store.lightInfo() }
                light?.let { values -> AureaPropertyPanel(store, "light", values, onOpenCurve = {
                    lights = false; materialPanel = EditorPanel.Curve; materials = true
                }) }
            }
        }
    }
    val selected = store.layers.firstOrNull { it.id == store.primary }
    val openMaterial = { materialPanel = EditorPanel.Element3D; materials = true }
    val openLights = { lights = true }
    val isModel = selected?.kind == LayerType.Model3D.kind
    // O painel 3D e os menus abrem o Esqueleto: ele vira a área de baixo.
    LaunchedEffect(store.skeletonPanel) { if (store.skeletonPanel) area = AREA_SKELETON }
    LaunchedEffect(isModel) { if (!isModel && area == AREA_SKELETON) area = 0 }
    val chooseArea = { i: Int -> area = i; store.openSkeletonPanel(i == AREA_SKELETON) }
    val menus = studioMenus(store, selected, openMaterial, openLights) { chooseArea(AREA_SKELETON) }
    Column(Modifier.fillMaxSize().background(AureaColors.Background).then(if (wide) Modifier.displayCutoutPadding() else Modifier)) {
        StudioHeader(store, fullscreen) { fullscreen = !fullscreen }
        SceneSplit(Modifier.fillMaxWidth().weight(1f).padding(horizontal = 8.dp), preview, sideBySide, split, { split = it }, edit = {
            Box(Modifier.fillMaxSize().clip(RoundedCornerShape(12.dp)).background(AureaColors.EditorCanvas)) {
                // A cena 3D e os gizmos não espelham em árabe (ver `KeepLtr`).
                KeepLtr {
                    stage(Modifier.fillMaxSize())
                    ScenePill(stringResource(R.string.s3d_viewport_edit), null, "scene.viewport", Modifier.align(Alignment.TopStart).padding(6.dp), buildList {
                        addAll(menus.mode); add(MenuEntry.Divider); addAll(menus.view)
                        add(MenuEntry.Divider)
                        add(MenuEntry.Item(stringResource(if (preview) R.string.s3d_preview_hide else R.string.s3d_preview_show),
                            glyph = if (preview) CupertinoGlyph.EyeSlash else CupertinoGlyph.Eye) { preview = !preview })
                    })
                    SceneToolbar(store, Modifier.align(Alignment.TopStart).padding(top = 44.dp))
                }
                Column(Modifier.align(Alignment.BottomCenter).fillMaxWidth().padding(bottom = 8.dp),
                    horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    if (store.rigFixMode && store.humanoidJoints != null) RigFixBar(store)
                }
                if (addMenu) StudioAddMenu(store, ui) { addMenu = false }
            }
        }, result = {
            Box(Modifier.fillMaxSize().clip(RoundedCornerShape(12.dp)).background(AureaColors.EditorCanvas)) {
                ScenePreviewPane(store)
                KeepLtr {
                    ScenePill(stringResource(R.string.s3d_preview), CupertinoGlyph.Eye, "scene.preview.menu", Modifier.align(Alignment.TopStart).padding(6.dp), listOf(
                        MenuEntry.Item(stringResource(R.string.s3d_preview_hide), glyph = CupertinoGlyph.EyeSlash) { preview = false },
                        MenuEntry.Item(stringResource(R.string.s3d_split_switch), glyph = CupertinoGlyph.RectangleStack) { sideBySide = !sideBySide },
                    ))
                }
            }
        })
        if (!fullscreen) StudioBottomArea(store, isModel, area, chooseArea, menus, { addMenu = !addMenu }, openMaterial, openLights) {
            materialPanel = EditorPanel.Curve; materials = true
        }
    }
}

/** Deitado (ou tablet): uma linha de barra e o gráfico ao lado da timeline. */
@Composable
private fun sceneWide(): Boolean = androidx.compose.ui.platform.LocalConfiguration.current.screenWidthDp >= 600

private tailrec fun android.content.Context.sceneActivity(): android.app.Activity? = when (this) {
    is android.app.Activity -> this
    is android.content.ContextWrapper -> baseContext.sceneActivity()
    else -> null
}

/** Um rótulo com seta que abre um menu: o título das viewports e do painel. */
@Composable
private fun ScenePill(label: String, glyph: Char?, tag: String, modifier: Modifier, entries: List<MenuEntry>, description: String = label) {
    var open by remember { mutableStateOf(false) }
    Box(modifier) {
        Row(Modifier.height(32.dp).clip(RoundedCornerShape(9.dp)).background(AureaColors.EditorRowPill).semantics { contentDescription = description }.tocavel { open = !open }
            .padding(horizontal = 10.dp).testTag(tag), verticalAlignment = Alignment.CenterVertically) {
            glyph?.let { CupertinoIcon(it, 15.dp, AureaColors.Text); Spacer(Modifier.width(7.dp)) }
            if (label.isNotEmpty()) { Text(label, color = AureaColors.Text, fontSize = 13.sp, maxLines = 1); Spacer(Modifier.width(7.dp)) }
            CupertinoIcon(CupertinoGlyph.ChevronDown, 11.dp, AureaColors.Muted)
        }
        AnchoredMenu(open, { open = false }, entries = entries, tone = AureaSurfaceTone.Editor, width = 240.dp, tag = "$tag.list")
    }
}

private const val AREA_SKELETON = 3

// --- Cena 3D: cabeçalho ----------------------------------------------------------

@Composable
private fun StudioHeader(store: EditorStore, fullscreen: Boolean, onFullscreen: () -> Unit) {
    KeepLtr {
        Row(Modifier.fillMaxWidth().height(36.dp).padding(horizontal = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            StudioIcon(CupertinoGlyph.ChevronLeft, stringResource(R.string.editor_voltar_editor), store::exitSceneEditor)
            Spacer(Modifier.width(8.dp))
            CupertinoIcon(CupertinoGlyph.Cube, 20.dp, AureaColors.Text)
            Spacer(Modifier.width(10.dp))
            Text(stringResource(R.string.s3d_scene_title), color = AureaColors.Text, fontSize = 16.sp, fontWeight = FontWeight.SemiBold, maxLines = 1)
            Spacer(Modifier.weight(1f))
            StudioIcon(if (fullscreen) CupertinoGlyph.FullscreenExit else CupertinoGlyph.Fullscreen, stringResource(R.string.s3d_fullscreen), onFullscreen)
            StudioIcon(CupertinoGlyph.Xmark, stringResource(R.string.editor_voltar_editor), store::exitSceneEditor)
        }
    }
}

/** O que os menus da cena oferecem: modo, vista, seleção e o menu do objeto ou da pose. */
private class StudioMenus(val mode: List<MenuEntry>, val view: List<MenuEntry>, val select: List<MenuEntry>, val more: List<MenuEntry>)

@Composable
private fun studioMenus(store: EditorStore, selected: com.aurea.aurea.engine.LayerRow?,
                        openMaterial: () -> Unit, openLights: () -> Unit, openSkeleton: () -> Unit): StudioMenus {
    val pose = rigPoseActive(store)
    val isModel = selected?.kind == LayerType.Model3D.kind
    val viewEntry = { label: String, yaw: Float, pitch: Float -> MenuEntry.Item(label) { store.snapSceneView(yaw, pitch) } }
    val canPose = isModel && store.poseBones != null
    val mode = listOf<MenuEntry>(
        MenuEntry.Item(stringResource(R.string.s3d_mode_object), glyph = CupertinoGlyph.CubeFill, selected = !pose, checked = !pose) { if (store.rigPoseMode) store.setRigPose(false) },
        MenuEntry.Item(stringResource(R.string.s3d_mode_pose), glyph = CupertinoGlyph.PersonCropCircle, enabled = canPose, selected = pose, checked = pose) { if (!store.rigPoseMode) store.setRigPose(true) },
        MenuEntry.Item(stringResource(if (store.gizmoLocalSpace) R.string.s3d_space_local else R.string.s3d_space_global), keepOpen = true) { store.toggleGizmoSpace() },
    )
    val view = buildList<MenuEntry> {
                add(MenuEntry.Item(stringResource(R.string.s3d_view_frame), glyph = CupertinoGlyph.Fullscreen) { sceneFrameAll(store) })
                add(MenuEntry.Item(stringResource(R.string.s3d_view_camera), glyph = CupertinoGlyph.Videocam, checked = store.sceneCameraView) { store.toggleSceneCameraView() })
                add(MenuEntry.Divider)
                add(viewEntry(stringResource(R.string.om_view_front), 0f, 0f))
                add(viewEntry(stringResource(R.string.om_view_back), 180f, 0f))
                add(viewEntry(stringResource(R.string.om_view_left), -90f, 0f))
                add(viewEntry(stringResource(R.string.om_view_right), 90f, 0f))
                add(viewEntry(stringResource(R.string.om_view_top), store.sceneYaw, 80f))
                add(viewEntry(stringResource(R.string.om_view_bottom), store.sceneYaw, -80f))
                add(MenuEntry.Divider)
                add(MenuEntry.Item(stringResource(R.string.s3d_overlays), glyph = CupertinoGlyph.Eye, checked = !ViewModes.hideOverlay, keepOpen = true) { ViewModes.hideOverlay = !ViewModes.hideOverlay })
                if (pose) add(MenuEntry.Item(stringResource(R.string.skeleton_show), checked = !store.skeletonHidden, keepOpen = true) { store.showSkeleton(store.skeletonHidden) })
            }
    val objects = store.layers.filter { it.isThreeD }
    val select = buildList<MenuEntry> {
                if (pose) {
                    add(MenuEntry.Item(stringResource(R.string.s3d_select_none), enabled = store.poseBone >= 0) { store.choosePoseBone(-1) })
                    add(MenuEntry.Item(stringResource(R.string.skeleton_bones), glyph = CupertinoGlyph.PersonCropCircle) { openSkeleton() })
                } else {
                    add(MenuEntry.Item(stringResource(R.string.s3d_select_all)) { store.selectAll() })
                    add(MenuEntry.Item(stringResource(R.string.s3d_select_none), enabled = store.selection.isNotEmpty()) { store.clearSelection() })
                    if (objects.isNotEmpty()) add(MenuEntry.Divider)
                    objects.forEach { o -> add(MenuEntry.Item(o.name, glyph = LayerType.of(o.kind).glyph, checked = o.id == store.primary) { store.select(o.id, openOptions = false) }) }
                }
            }
    val more = if (pose) {
                buildList<MenuEntry> {
                    add(MenuEntry.Item(stringResource(R.string.rig_key_bone), glyph = CupertinoGlyph.SuitDiamond, enabled = store.poseBone >= 0) { store.togglePoseKey(false) })
                    add(MenuEntry.Item(stringResource(R.string.rig_key_all), glyph = CupertinoGlyph.SuitDiamond) { store.togglePoseKey(true) })
                    add(MenuEntry.Divider)
                    add(MenuEntry.Item(stringResource(R.string.skeleton_copy)) { store.copyPose() })
                    add(MenuEntry.Item(stringResource(R.string.skeleton_paste), enabled = store.poseClipboard != null) { store.pastePose() })
                    add(MenuEntry.Item(stringResource(R.string.skeleton_mirror_pose)) { store.mirrorPose(true) })
                    add(MenuEntry.Divider)
                    add(MenuEntry.Item(stringResource(R.string.skeleton_zero_pose)) { store.zeroPose(true) })
                    add(MenuEntry.Item(stringResource(R.string.rig_reset_all), glyph = CupertinoGlyph.ArrowCounterclockwise) { store.resetPose(true) })
                }
            } else {
                buildList<MenuEntry> {
                    val has = selected != null
                    add(MenuEntry.Item(stringResource(R.string.s3d_insert_key_desc), glyph = CupertinoGlyph.SuitDiamond, enabled = has) {
                        store.primary?.let { store.toggleTransformKeyframe(IntArray(9) { i -> i }, it) }
                    })
                    add(MenuEntry.Item(stringResource(R.string.s3d_duplicate), glyph = CupertinoGlyph.DocOnDoc, enabled = has) { store.duplicateLayers() })
                    if (isModel || (selected?.kind == LayerType.Text.kind && selected.isThreeD)) {
                        add(MenuEntry.Divider)
                        add(MenuEntry.Item(stringResource(R.string.scene_material), glyph = CupertinoGlyph.CubeFill) { openMaterial() })
                    }
                    if (selected?.kind == LayerType.Light.kind) {
                        add(MenuEntry.Divider)
                        add(MenuEntry.Item(stringResource(R.string.pn_t3d_lighting), glyph = CupertinoGlyph.Lightbulb) { openLights() })
                    }
                    if (isModel) {
                        add(MenuEntry.Divider)
                        add(MenuEntry.Item(stringResource(R.string.skeleton_title), glyph = CupertinoGlyph.PersonCropCircle) { openSkeleton() })
                        add(MenuEntry.Item(stringResource(R.string.mocap_title), glyph = CupertinoGlyph.Videocam) {
                            com.aurea.aurea.editor.panels.Element3DRequest.tab = com.aurea.aurea.editor.panels.Tab3D.Rig
                            openMaterial()
                        })
                    }
                    add(MenuEntry.Divider)
                    add(MenuEntry.Item(selected?.let { stringResource(R.string.s3d_delete_named, it.name) } ?: stringResource(R.string.s3d_nothing_selected),
                        glyph = CupertinoGlyph.Trash, enabled = has, destructive = true) { selected?.let { store.deleteLayers(listOf(it.id)) } })
                }
            }
    return StudioMenus(mode, view, select, more)
}

@Composable
private fun StudioMenu(label: String, tag: String, entries: List<MenuEntry>) {
    var open by remember { mutableStateOf(false) }
    Box {
        StudioMenuLabel(label, tag, open) { open = !open }
        AnchoredMenu(open, { open = false }, entries = entries, tone = AureaSurfaceTone.Editor, width = 240.dp, tag = "$tag.list")
    }
}

@Composable
private fun StudioMenuLabel(label: String, tag: String, on: Boolean, onClick: () -> Unit) {
    Box(Modifier.height(38.dp).clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.EditorRowPill else Color.Transparent)
        .tocavel(onClick = onClick).padding(horizontal = 11.dp).testTag(tag), contentAlignment = Alignment.Center) {
        Text(label, color = AureaColors.Text, fontSize = 14.sp, maxLines = 1)
    }
}

@Composable
private fun StudioIcon(glyph: Char, description: String, onClick: () -> Unit) {
    Box(Modifier.size(40.dp).clip(RoundedCornerShape(10.dp)).semantics { contentDescription = description }
        .tocavel(haptic = true, onClick = onClick), contentAlignment = Alignment.Center) {
        CupertinoIcon(glyph, 18.dp, AureaColors.Text)
    }
}

// --- Cena 3D: menu Adicionar -------------------------------------------------------

@Composable
private fun StudioAddMenu(store: EditorStore, ui: EditorUi, onClose: () -> Unit) {
    Column(Modifier.fillMaxWidth().padding(8.dp).clip(RoundedCornerShape(16.dp)).background(AureaColors.Surface)
        .border(1.dp, AureaColors.EditorBarLine, RoundedCornerShape(16.dp))
        // Opaco ao toque: nada passa para a cena por baixo.
        .pointerInput(Unit) { awaitEachGesture { awaitFirstDown(requireUnconsumed = false); do { val e = awaitPointerEvent() } while (e.changes.any { it.pressed }) } }
        .verticalScroll(rememberScrollState()).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.panel_adicionar).removePrefix("+ "), color = AureaColors.Text, fontSize = 15.sp,
                fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
            StudioIcon(CupertinoGlyph.Xmark, stringResource(R.string.common_close), onClose)
        }
        StudioSection(stringResource(R.string.s3d_section_import))
        Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(AureaColors.Brand)
            .tocavel(haptic = true) { onClose(); ui.addTab = AddTab.Model3D; ui.adding = true }
            .padding(horizontal = 14.dp, vertical = 12.dp), verticalAlignment = Alignment.CenterVertically) {
            CupertinoIcon(CupertinoGlyph.CubeFill, 20.dp, AureaColors.OnAction)
            Spacer(Modifier.width(10.dp))
            Column(Modifier.weight(1f)) {
                Text(stringResource(R.string.s3d_import_model), color = AureaColors.OnAction, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
                Text(stringResource(R.string.s3d_import_formats), color = AureaColors.OnAction.copy(alpha = .75f), fontSize = 11.sp)
            }
        }
        StudioSection(stringResource(R.string.s3d_section_mesh))
        // Shape3DKind (engine/include/aurea/scene3d/Shape3D.hpp): 0 cubo, 1 esfera,
        // 2 cilindro, 3 cone, 4 pirâmide, 5 toro.
        val meshes = listOf(R.string.s3d_cube to 0, R.string.s3d_sphere to 1, R.string.s3d_cylinder to 2,
            R.string.s3d_cone to 3, R.string.s3d_pyramid to 4, R.string.s3d_torus to 5)
        meshes.chunked(3).forEach { row ->
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                row.forEach { (label, kind) ->
                    StudioTile(CupertinoGlyph.CubeFill, stringResource(label), Modifier.weight(1f)) { onClose(); store.addShape3D(kind) }
                }
            }
        }
        StudioSection(stringResource(R.string.s3d_section_other))
        val others = listOf<Triple<Char, String, () -> Unit>>(
            Triple(CupertinoGlyph.Textformat, stringResource(R.string.sh_add_text_3d)) { store.addText3D() },
            Triple(CupertinoGlyph.Lightbulb, stringResource(R.string.scene_light_point)) { store.addLight(1) },
            Triple(CupertinoGlyph.Lightbulb, stringResource(R.string.scene_light_directional)) { store.addLight(0) },
            Triple(CupertinoGlyph.Lightbulb, stringResource(R.string.scene_light_spot)) { store.addLight(2) },
            Triple(CupertinoGlyph.Sparkles, stringResource(R.string.scene_flare3d)) { store.addLight(3) },
            Triple(CupertinoGlyph.Videocam, stringResource(R.string.panel_camera_3d)) { store.addCamera() },
            Triple(CupertinoGlyph.SmallcircleCircle, stringResource(R.string.s3d_empty)) { store.addNull(true) },
            Triple(CupertinoGlyph.Camera, stringResource(R.string.real_camera_title)) { store.startRealCamera() },
        )
        others.chunked(3).forEach { row ->
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                row.forEach { (glyph, label, action) -> StudioTile(glyph, label, Modifier.weight(1f)) { onClose(); action() } }
                repeat(3 - row.size) { Spacer(Modifier.weight(1f)) }
            }
        }
    }
}

@Composable
private fun StudioSection(t: String) = Text(t.uppercase(), color = AureaColors.Muted, fontSize = 11.sp, fontWeight = FontWeight.SemiBold)

@Composable
private fun StudioTile(glyph: Char, label: String, modifier: Modifier, onClick: () -> Unit) {
    val desc = stringResource(R.string.s3d_add_named, label)
    Column(modifier.clip(RoundedCornerShape(12.dp)).background(AureaColors.SurfaceHigh).tocavel(haptic = true, onClick = onClick)
        .padding(vertical = 12.dp, horizontal = 4.dp).semantics { contentDescription = desc }, horizontalAlignment = Alignment.CenterHorizontally) {
        CupertinoIcon(glyph, 22.dp, AureaColors.Text)
        Spacer(Modifier.height(6.dp))
        Text(label, color = AureaColors.Text, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

// --- Cena 3D: área de baixo --------------------------------------------------------

@Composable
private fun StudioBottomArea(store: EditorStore, isModel: Boolean, area: Int, onArea: (Int) -> Unit, menus: StudioMenus, onAdd: () -> Unit,
                             openMaterial: () -> Unit, openLights: () -> Unit, openCurve: () -> Unit) {
    val wide = sceneWide()
    val layer = store.primary
    val pose = store.rigPoseMode && store.poseBones != null
    var picked by remember(layer) { mutableStateOf<Pair<SceneLane, Int>?>(null) }
    val activeLane = picked?.first ?: layer?.let { SceneLane(it, if (pose) store.poseBone else -1) }
    val needKeys = stringResource(R.string.s3d_curve_need_keys)
    val areas = buildList { add(R.string.s3d_animation); add(R.string.s3d_area_properties); add(R.string.s3d_area_outliner); if (isModel) add(R.string.skeleton_title) }
    val addKey = {
        if (pose) store.togglePoseKey(store.poseBone < 0)
        else if (layer != null) store.toggleTransformKeyframe(IntArray(9) { it }, layer)
    }
    val deleteKey = {
        val lane = activeLane
        if (lane != null && lane.layer != -1L) {
            val at = picked?.second ?: store.playhead
            val shift = laneShift(store, lane.layer)
            store.deleteKeyframesTogether(lane.layer, laneKeys(store, lane).filter { it.time + shift == at })
        }
        picked = null
    }
    val showCurve = {
        val keys = activeLane?.let { laneTrack(store, it) }.orEmpty()
        val start = store.curveStart(keys)
        val lane = activeLane
        if (lane != null && start != null) { store.selectKeyframe(lane.layer, start); openCurve() } else store.showToast(needKeys)
    }
    KeepLtr {
        Column(Modifier.fillMaxWidth().padding(horizontal = 8.dp).padding(top = 6.dp, bottom = 6.dp).clip(RoundedCornerShape(12.dp)).background(AureaColors.EditorCanvas)) {
            val areaPill: @Composable () -> Unit = {
                ScenePill(stringResource(areas[area.coerceIn(0, areas.lastIndex)]), CupertinoGlyph.PersonCropCircle, "scene.area", Modifier,
                    areas.mapIndexed { i, label -> MenuEntry.Item(stringResource(label), selected = i == area, checked = i == area) { onArea(i) } })
            }
            val editButtons: @Composable () -> Unit = {
                val addLabel = stringResource(R.string.panel_adicionar)
                Box(Modifier.size(38.dp, 30.dp).clip(RoundedCornerShape(8.dp)).background(AureaColors.Brand).semantics { contentDescription = addLabel }.tocavel(onClick = onAdd).testTag("scene.add"),
                    contentAlignment = Alignment.Center) { CupertinoIcon(CupertinoGlyph.Plus, 17.dp, AureaColors.OnAction) }
                StudioIcon(CupertinoGlyph.ArrowUturnLeft, stringResource(R.string.editor_desfazer), store::undo)
                StudioIcon(CupertinoGlyph.ArrowUturnRight, stringResource(R.string.editor_refazer), store::redo)
            }
            val transport: @Composable () -> Unit = {
                StudioIcon(if (store.playing) CupertinoGlyph.PauseFill else CupertinoGlyph.PlayFill,
                    stringResource(if (store.playing) R.string.s3d_pause else R.string.s3d_play)) { store.togglePlayback() }
                StudioIcon(CupertinoGlyph.BackwardEnd, stringResource(R.string.s3d_prev_key)) { store.stepToKeyframe(-1) }
                StudioIcon(CupertinoGlyph.ForwardEnd, stringResource(R.string.s3d_next_key)) { store.stepToKeyframe(1) }
                ScenePill("", CupertinoGlyph.SuitDiamond, "scene.key.menu", Modifier, listOf(
                    MenuEntry.Item(stringResource(R.string.s3d_insert_key_desc), glyph = CupertinoGlyph.SuitDiamond) { addKey() },
                    MenuEntry.Item(stringResource(R.string.s3d_delete_key), glyph = CupertinoGlyph.Xmark) { deleteKey() },
                    MenuEntry.Item(stringResource(if (store.autoKeyTransforms) R.string.s3d_autokey_on else R.string.s3d_autokey_off),
                        checked = store.autoKeyTransforms, keepOpen = true) { store.autoKeyTransforms = !store.autoKeyTransforms },
                ), description = stringResource(R.string.s3d_insert_key_desc))
            }
            val keyButtons: @Composable () -> Unit = {
                val keyLabel = stringResource(R.string.s3d_insert_key_desc)
                Box(Modifier.size(38.dp, 30.dp).clip(RoundedCornerShape(8.dp)).background(AureaColors.Brand)
                    .semantics { contentDescription = keyLabel }.tocavel(haptic = true) { addKey() }.testTag("scene.key"),
                    contentAlignment = Alignment.Center) { CupertinoIcon(CupertinoGlyph.SuitDiamond, 15.dp, AureaColors.OnAction) }
                StudioIcon(CupertinoGlyph.Scribble, stringResource(R.string.s3d_curve)) { showCurve() }
                var more by remember { mutableStateOf(false) }
                Box {
                    StudioIcon(CupertinoGlyph.Ellipsis, stringResource(R.string.s3d_more)) { more = !more }
                    AnchoredMenu(more, { more = false }, entries = menus.more + MenuEntry.Divider + menus.select, tone = AureaSurfaceTone.Editor,
                        width = 240.dp, alignEnd = true, tag = "scene.more.list")
                }
            }
            // Deitado cabe numa linha; em pé são duas.
            if (wide) Row(Modifier.fillMaxWidth().height(40.dp).padding(horizontal = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                areaPill(); Spacer(Modifier.weight(1f)); editButtons()
                Box(Modifier.width(1.dp).height(20.dp).background(AureaColors.EditorBarLine))
                transport(); Spacer(Modifier.weight(1f)); keyButtons()
            } else {
                Row(Modifier.fillMaxWidth().height(40.dp).padding(horizontal = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                    areaPill(); Spacer(Modifier.weight(1f)); editButtons()
                }
                Row(Modifier.fillMaxWidth().height(40.dp).padding(horizontal = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                    transport(); Spacer(Modifier.weight(1f)); keyButtons()
                }
            }
            Box(Modifier.fillMaxWidth().height(if (!wide) 270.dp else if (area == 0) 116.dp else 150.dp)) {
                when (area) {
                    0 -> SceneAnimation(store, picked, { picked = it }, showCurve)
                    1 -> StudioProperties(store, openMaterial, openLights)
                    2 -> StudioOutliner(store)
                    else -> com.aurea.aurea.editor.panels.SkeletonPanel(store, Modifier.fillMaxSize())
                }
            }
        }
    }
}

/** Uma linha do painel de animação: um objeto ou um osso dele. */
private data class SceneLane(val layer: Long, val node: Int = -1)

/** As chaves que a linha mostra. São as mesmas da timeline principal: não há cópia. */
private fun laneKeys(store: EditorStore, lane: SceneLane): List<com.aurea.aurea.engine.KeyframeRow> {
    val all = store.keyframes[lane.layer].orEmpty()
    return if (lane.node >= 0) all.filter { it.property == com.aurea.aurea.engine.TrackProperty.BONE_POSE && it.effectIndex == lane.node && it.timelineVisible }
    else all.filter { it.effectIndex < 0 && it.property <= 12 && it.timelineVisible }
}

/** Uma trilha só da linha (um eixo do osso): é a que o gráfico mostra. */
private fun laneTrack(store: EditorStore, lane: SceneLane): List<com.aurea.aurea.engine.KeyframeRow> {
    val all = laneKeys(store, lane)
    val first = all.firstOrNull() ?: return emptyList()
    return all.filter { it.sameTrack(first) }.sortedBy { it.time }
}

/** Tempo local da camada → quadro da composição. */
private fun laneShift(store: EditorStore, layer: Long): Int =
    store.layers.firstOrNull { it.id == layer }?.let { it.startFrame - it.offsetFrames } ?: 0

private fun laneFrames(store: EditorStore, lane: SceneLane): List<Int> {
    val shift = laneShift(store, lane.layer)
    return laneKeys(store, lane).map { it.time + shift }.distinct().sorted()
}

/** A lista de ossos, a timeline em segundos e o gráfico da curva, lado a lado. */
@Composable
private fun SceneAnimation(store: EditorStore, picked: Pair<SceneLane, Int>?, onPick: (Pair<SceneLane, Int>?) -> Unit, onCurve: () -> Unit) {
    val end = (store.project.durationFrames.coerceAtLeast(2)) - 1
    val fps = store.project.fps.toFloat().coerceAtLeast(1f)
    val layer = store.primary
    val pose = store.rigPoseMode && store.poseBones != null
    // Chave sendo arrastada: linha, de onde saiu e onde está agora.
    var dragging by remember(layer) { mutableStateOf<Triple<SceneLane, Int, Int>?>(null) }
    val objects = store.layers.filter { it.isThreeD }
    var folded by remember(layer) { mutableStateOf(false) }
    val bones = store.poseBones
    val rows = buildList {
        objects.filter { it.id == layer || laneKeys(store, SceneLane(it.id)).isNotEmpty() }.forEach { o ->
            add(SceneLane(o.id))
            if (o.id == layer && bones != null && !folded) {
                for (i in 0 until bones.size / BONE_FLOATS) add(SceneLane(o.id, bones[i * BONE_FLOATS].toInt()))
            }
        }
    }
    val keysOf = remember(store.keyframes, store.layers, rows) { rows.associateWith { laneFrames(store, it) } }
    val rowH = 26.dp; val rulerH = 24.dp
    val scroll = rememberScrollState()
    // Escolheu um osso na viewport: a lista vai até ele.
    val density = androidx.compose.ui.platform.LocalDensity.current
    LaunchedEffect(store.poseBone, pose) {
        val at = rows.indexOfFirst { it.layer == layer && it.node == store.poseBone }
        val h = with(density) { rowH.roundToPx() }
        if (pose && at >= 0 && (at * h < scroll.value || (at + 1) * h > scroll.value + scroll.viewportSize)) scroll.animateScrollTo(at * h)
    }
    val measurer = rememberTextMeasurer()
    val keyColor = AureaColors.Brand; val keyPicked = AureaColors.Keyframe; val head = AureaColors.Accent
    val grid = AureaColors.TickWeak; val muted = AureaColors.Muted; val line = AureaColors.EditorBarLine
    val activeLane = picked?.first ?: layer?.let { SceneLane(it, if (pose) store.poseBone else -1) }
    val wide = sceneWide()
    Column(Modifier.fillMaxSize()) {
      Row(Modifier.fillMaxWidth().weight(1f)) {
        Column(Modifier.width(if (wide) 168.dp else 140.dp).fillMaxHeight()) {
            Spacer(Modifier.height(rulerH))
            Column(Modifier.verticalScroll(scroll)) {
                rows.forEach { lane ->
                    val bone = lane.node >= 0
                    val row = objects.firstOrNull { it.id == lane.layer }
                    val on = lane.layer == layer && lane.node == (if (pose) store.poseBone else -1)
                    val here = store.playhead in keysOf[lane].orEmpty()
                    Row(Modifier.fillMaxWidth().height(rowH).background(if (on) AureaColors.EditorRowPill else Color.Transparent)
                        .tocavel {
                            if (bone) { if (!store.rigPoseMode) store.setRigPose(true); store.choosePoseBone(lane.node) }
                            else store.select(lane.layer, openOptions = false)
                        }.padding(start = if (bone) 26.dp else 8.dp, end = 2.dp), verticalAlignment = Alignment.CenterVertically) {
                        if (!bone && row != null) {
                            Box(Modifier.size(22.dp).tocavel { store.setLayerVisible(row.id, !row.visible) }, contentAlignment = Alignment.Center) {
                                CupertinoIcon(if (row.visible) CupertinoGlyph.Eye else CupertinoGlyph.EyeSlash, 15.dp, AureaColors.Muted)
                            }
                            Spacer(Modifier.width(4.dp))
                            if (row.id == layer && bones != null) Box(Modifier.size(18.dp).tocavel { folded = !folded }, contentAlignment = Alignment.Center) {
                                CupertinoIcon(if (folded) CupertinoGlyph.ChevronRight else CupertinoGlyph.ChevronDown, 10.dp, AureaColors.Muted)
                            }
                        } else if (bone) { CupertinoIcon(CupertinoGlyph.Link, 12.dp, AureaColors.Muted); Spacer(Modifier.width(6.dp)) }
                        Text(if (bone) store.modelNodeName(lane.node) else row?.name.orEmpty(), color = if (on) AureaColors.Text else AureaColors.Muted, fontSize = 12.sp,
                            maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
                        // Cheio = há chave neste instante. Tocar põe ou tira a chave da linha.
                        Box(Modifier.size(26.dp).tocavel(haptic = true) {
                            if (bone) {
                                if (!store.rigPoseMode) store.setRigPose(true)
                                store.choosePoseBone(lane.node)
                                store.togglePoseKey(false)
                            } else store.toggleTransformKeyframe(IntArray(9) { it }, lane.layer)
                        }, contentAlignment = Alignment.Center) {
                            Box(Modifier.size(11.dp).clip(CircleShape).background(if (here) keyColor else Color.Transparent)
                                .border(1.dp, if (here) keyColor else AureaColors.Muted, CircleShape))
                        }
                    }
                }
            }
        }
        Box(Modifier.width(1.dp).fillMaxHeight().background(line))
        Canvas(Modifier.weight(1f).fillMaxHeight().clipToBounds().testTag("scene.timeline").pointerInput(rows, keysOf, end) {
            awaitEachGesture {
                val down = awaitFirstDown()
                val pad = 10.dp.toPx(); val w = size.width - pad * 2
                fun frameAt(x: Float) = ((x - pad) / w * end).roundToInt().coerceIn(0, end)
                fun xOf(f: Int) = pad + f / end.toFloat() * w
                if (store.playing) store.pause()
                val row = ((down.position.y - rulerH.toPx() + scroll.value) / rowH.toPx()).toInt()
                val track = if (down.position.y > rulerH.toPx()) rows.getOrNull(row) else null
                val frames = track?.let { keysOf[it] }.orEmpty()
                val near = frames.minByOrNull { abs(xOf(it) - down.position.x) }?.takeIf { abs(xOf(it) - down.position.x) < 14.dp.toPx() }
                if (near != null && track != null) {
                    onPick(track to near)
                    if (track.layer != layer) store.select(track.layer, openOptions = false)
                    store.seek(near)
                } else { onPick(null); store.seek(frameAt(down.position.x)) }
                val slop = viewConfiguration.touchSlop
                var moved = false
                while (true) {
                    val e = awaitPointerEvent(); val ch = e.changes.firstOrNull { it.pressed } ?: break
                    if (near == null) store.seek(frameAt(ch.position.x))
                    else if (track != null) {
                        if (!moved && abs(ch.position.x - down.position.x) > slop) moved = true
                        if (moved) dragging = Triple(track, near, frameAt(ch.position.x))
                    }
                    ch.consume()
                }
                // Soltou: as chaves do instante andam juntas, em um passo de desfazer.
                dragging?.let { (lane, from, to) ->
                    dragging = null
                    if (to != from) {
                        val shift = laneShift(store, lane.layer)
                        store.moveKeyframesTogether(lane.layer, laneKeys(store, lane).filter { it.time + shift == from }, to - from)
                        onPick(lane to to)
                        store.seek(to)
                    }
                }
            }
        }) {
            val pad = 10.dp.toPx(); val w = size.width - pad * 2
            fun x(f: Float) = pad + f / end * w
            val rt = rulerH.toPx(); val rh = rowH.toPx()
            // Régua em segundos: o menor passo com rótulos a pelo menos 56 dp.
            val seconds = end / fps
            val stepS = listOf(.1f, .25f, .5f, 1f, 2f, 5f, 10f, 30f, 60f).firstOrNull { it / seconds * w >= 56.dp.toPx() } ?: 120f
            rows.forEachIndexed { r, lane ->
                val top = rt + r * rh - scroll.value
                drawLine(line.copy(alpha = .5f), Offset(0f, top + rh), Offset(size.width, top + rh), 1f)
                val cy = top + rh / 2
                val move = dragging?.takeIf { it.first == lane }
                val frames = keysOf[lane].orEmpty().map { f0 -> if (move != null && f0 == move.second) move.third else f0 }
                if (frames.size >= 2) drawLine(keyColor.copy(alpha = .5f), Offset(x(frames.min().toFloat()), cy), Offset(x(frames.max().toFloat()), cy), 1.dp.toPx())
                keysOf[lane].orEmpty().forEach { f0 ->
                    val f = if (move != null && f0 == move.second) move.third else f0
                    val c = Offset(x(f.toFloat()), cy); val rr = 5.5.dp.toPx()
                    val p = Path().apply { moveTo(c.x, c.y - rr); lineTo(c.x + rr, c.y); lineTo(c.x, c.y + rr); lineTo(c.x - rr, c.y); close() }
                    drawPath(p, if (picked?.first == lane && picked.second == f0) keyPicked else keyColor)
                }
            }
            drawRect(AureaColors_ruler(line), Offset.Zero, Size(size.width, rt))
            var t = 0f
            while (t <= seconds + .001f) {
                val xx = x(t * fps)
                drawLine(grid, Offset(xx, rt - 6.dp.toPx()), Offset(xx, rt), 1f)
                val text = if (stepS < 1f) "%.1fs".format(java.util.Locale.ROOT, t) else "${t.toInt()}s"
                val l = measurer.measure(text, TextStyle(color = muted, fontSize = 10.sp))
                drawText(l, topLeft = Offset(xx + 3.dp.toPx(), 2.dp.toPx()))
                for (k in 1..4) { val xm = x((t + stepS * k / 5f) * fps); if (xm < size.width) drawLine(grid.copy(alpha = .5f), Offset(xm, rt - 3.dp.toPx()), Offset(xm, rt), 1f) }
                t += stepS
            }
            val px = x(store.playhead.coerceIn(0, end).toFloat())
            drawLine(head, Offset(px, rt), Offset(px, size.height), 1.5.dp.toPx())
            drawRoundRect(head, Offset(px - 6.dp.toPx(), 3.dp.toPx()), Size(12.dp.toPx(), rt - 8.dp.toPx()), CornerRadius(3.dp.toPx()))
        }
        if (wide) {
            Box(Modifier.width(1.dp).fillMaxHeight().background(line))
            SceneCurveGraph(store, activeLane, onCurve, Modifier.width(176.dp).fillMaxHeight())
        }
      }
      if (!wide) {
          Box(Modifier.fillMaxWidth().height(1.dp).background(line))
          SceneCurveGraph(store, activeLane, onCurve, Modifier.fillMaxWidth().height(84.dp))
      }
    }
}

private fun AureaColors_ruler(line: Color) = line.copy(alpha = .25f)

/** O gráfico da trilha escolhida, em % entre o menor e o maior valor. Tocar abre o editor. */
@Composable
private fun SceneCurveGraph(store: EditorStore, lane: SceneLane?, onOpen: () -> Unit, modifier: Modifier) {
    val keys = remember(store.keyframes, lane) { lane?.let { laneTrack(store, it) }.orEmpty() }
    val measurer = rememberTextMeasurer()
    val brand = AureaColors.Brand; val accent = AureaColors.Accent; val grid = AureaColors.EditorBarLine; val muted = AureaColors.Muted
    val description = stringResource(R.string.s3d_curve)
    Canvas(modifier.semantics { contentDescription = description }.tocavel(onClick = onOpen).testTag("scene.graph")) {
        val left = 30.dp.toPx(); val top = 12.dp.toPx(); val bottom = size.height - 12.dp.toPx(); val right = size.width - 8.dp.toPx()
        fun y(p: Float) = bottom - p / 100f * (bottom - top)
        for (p in listOf(0f, 50f, 100f)) {
            drawLine(grid.copy(alpha = .5f), Offset(left, y(p)), Offset(right, y(p)), 1f)
            val l = measurer.measure("${p.toInt()}", TextStyle(color = muted, fontSize = 10.sp))
            drawText(l, topLeft = Offset(left - l.size.width - 5.dp.toPx(), y(p) - l.size.height / 2f))
        }
        drawLine(grid, Offset(left, top), Offset(left, bottom), 1f)
        if (keys.size < 2) return@Canvas
        val t0 = keys.first().time; val t1 = keys.last().time.coerceAtLeast(t0 + 1)
        val lo = keys.minOf { it.value }; val hi = keys.maxOf { it.value }
        val span = (hi - lo).takeIf { it > 1e-5f }
        fun pct(v: Float) = if (span == null) 50f else (v - lo) / span * 100f
        fun x(t: Float) = left + (t - t0) / (t1 - t0) * (right - left)
        val curve = Path()
        keys.zipWithNext().forEachIndexed { i, (a, b) ->
            if (i == 0) curve.moveTo(x(a.time.toFloat()), y(pct(a.value)))
            for (k in 1..16) {
                val u = k / 16f; val e = u * u * (3f - 2f * u)
                curve.lineTo(x(a.time + (b.time - a.time) * u), y(pct(a.value + (b.value - a.value) * e)))
            }
        }
        drawPath(curve, brand, style = Stroke(2.dp.toPx()))
        keys.forEach { drawCircle(accent, 3.dp.toPx(), Offset(x(it.time.toFloat()), y(pct(it.value)))) }
    }
}

@Composable
private fun StudioProperties(store: EditorStore, openMaterial: () -> Unit, openLights: () -> Unit) {
    val d = store.detail?.takeIf { it.id == store.primary }
    val row = store.layers.firstOrNull { it.id == store.primary }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(10.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        if (d == null || row == null) {
            Text(stringResource(R.string.s3d_nothing_selected), color = AureaColors.Muted, fontSize = 12.sp); return@Column
        }
        val t = com.aurea.aurea.engine.TrackProperty
        Row(verticalAlignment = Alignment.CenterVertically) {
            CupertinoIcon(LayerType.of(row.kind).glyph, 14.dp, AureaColors.Muted)
            Spacer(Modifier.width(6.dp))
            Text(row.name, color = AureaColors.Text, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, maxLines = 1,
                overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
            val keyed = (0..8).any { d.hasKeyAtPlayhead(it) }
            Text(if (keyed) stringResource(R.string.s3d_key_here, store.playhead) else stringResource(R.string.s3d_no_key),
                color = if (keyed) AureaColors.Keyframe else AureaColors.Muted, fontSize = 11.sp)
        }
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            if (row.kind == LayerType.Model3D.kind || (row.kind == LayerType.Text.kind && row.isThreeD))
                StudioMiniPill(CupertinoGlyph.CubeFill, stringResource(R.string.scene_material), openMaterial)
            if (row.kind == LayerType.Light.kind) StudioMiniPill(CupertinoGlyph.Lightbulb, stringResource(R.string.pn_t3d_lighting), openLights)
            if (row.kind == LayerType.Model3D.kind && store.poseBones != null && !store.rigPoseMode)
                StudioMiniPill(CupertinoGlyph.PersonCropCircle, stringResource(R.string.rig_pose)) { store.setRigPose(true) }
        }
        // 0.4.6: AutoRig 3D e Motion Capture direto da Cena 3D (o painel abre na aba Esqueleto).
        if (row.kind == LayerType.Model3D.kind) Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            StudioMiniPill(CupertinoGlyph.Sparkles, stringResource(R.string.rig3d_autorig)) { store.startAutoRig() }
            StudioMiniPill(CupertinoGlyph.Videocam, stringResource(R.string.mocap_title)) {
                com.aurea.aurea.editor.panels.Element3DRequest.tab = com.aurea.aurea.editor.panels.Tab3D.Rig
                openMaterial()
            }
        }
        StudioVec3(stringResource(R.string.s3d_position), d.position, "", 1f, 10f, 0) { k, v -> store.setTransform(t.POSITION_X + k, v, d.id) }
        StudioVec3(stringResource(R.string.s3d_rotation), d.rotation, "°", .5f, 5f, 0) { k, v -> store.setTransform(t.ROTATION_X + k, v, d.id) }
        StudioVec3(stringResource(R.string.s3d_scale), d.scale, "", .01f, .1f, 2) { k, v -> store.setTransform(t.SCALE_X + k, v, d.id) }
    }
}

@Composable
private fun StudioMiniPill(glyph: Char, label: String, onClick: () -> Unit) {
    Row(Modifier.height(40.dp).clip(RoundedCornerShape(10.dp)).background(AureaColors.EditorRowPill)
        .tocavel(haptic = true, onClick = onClick).padding(horizontal = 12.dp), verticalAlignment = Alignment.CenterVertically) {
        CupertinoIcon(glyph, 14.dp, AureaColors.Text)
        Spacer(Modifier.width(6.dp))
        Text(label, color = AureaColors.Text, fontSize = 12.sp, maxLines = 1)
    }
}

@Composable
private fun StudioVec3(label: String, v: List<Float>, unit: String, speed: Float, step: Float, decimals: Int, onChange: (Int, Float) -> Unit) {
    Text(label, color = AureaColors.Muted, fontSize = 11.sp)
    Column(verticalArrangement = Arrangement.spacedBy(1.dp)) {
        listOf("X", "Y", "Z").forEachIndexed { k, n ->
            val shape = when (k) {
                0 -> RoundedCornerShape(topStart = 10.dp, topEnd = 10.dp)
                2 -> RoundedCornerShape(bottomStart = 10.dp, bottomEnd = 10.dp)
                else -> RoundedCornerShape(0.dp)
            }
            StudioNumber(n, v.getOrElse(k) { 0f }, unit, step, speed, decimals, shape) { onChange(k, it) }
        }
    }
}

/** Campo numérico no jeito do Blender: arrastar na horizontal muda; ‹ › dão passos. */
@Composable
private fun StudioNumber(label: String, value: Float, unit: String, step: Float, speed: Float, decimals: Int,
                         shape: RoundedCornerShape, onChange: (Float) -> Unit) {
    var acc by remember { mutableFloatStateOf(0f) }
    val current by rememberUpdatedState(value)
    val change by rememberUpdatedState(onChange)
    val text = "%.${decimals}f".format(java.util.Locale.ROOT, value) + unit
    KeepLtr {
        Row(Modifier.fillMaxWidth().height(40.dp).clip(shape).background(AureaColors.EditorGlyphBox)
            .pointerInput(Unit) {
                detectHorizontalDragGestures(onDragStart = { acc = current }) { ch, dx -> ch.consume(); acc += dx * speed; change(acc) }
            }.semantics { contentDescription = "$label $text" },
            verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(40.dp).tocavel { change(current - step) }, contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.ChevronLeft, 12.dp, AureaColors.Muted)
            }
            Text(label, color = AureaColors.Muted, fontSize = 12.sp)
            Spacer(Modifier.weight(1f))
            Text(text, color = AureaColors.Text, fontSize = 12.sp)
            Spacer(Modifier.weight(1f))
            Box(Modifier.size(40.dp).tocavel { change(current + step) }, contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.ChevronRight, 12.dp, AureaColors.Muted)
            }
        }
    }
}

@Composable
private fun StudioOutliner(store: EditorStore) {
    val objects = store.layers.filter { it.isThreeD }
    val bones = store.poseBones?.takeIf { store.rigPoseMode }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(vertical = 4.dp)) {
        StudioOutRow(0, CupertinoGlyph.CubeFill, stringResource(R.string.s3d_scene), false, false, null) {}
        objects.forEach { o ->
            val keyed = store.keyframes[o.id].orEmpty().any { it.effectIndex < 0 && it.property <= 12 }
            val deleteDesc = stringResource(R.string.s3d_delete_named, o.name)
            StudioOutRow(1, LayerType.of(o.kind).glyph, o.name, o.id == store.primary, keyed, deleteDesc to { store.deleteLayers(listOf(o.id)) }) {
                store.select(o.id, openOptions = false)
            }
            if (bones != null && o.id == store.primary) {
                val count = bones.size / BONE_FLOATS
                fun depth(i: Int): Int { var dd = 0; var p = bones[i * BONE_FLOATS + 1].toInt(); while (p >= 0 && dd < 64) { dd++; p = bones[p * BONE_FLOATS + 1].toInt() }; return dd }
                for (i in 0 until count) {
                    val node = bones[i * BONE_FLOATS].toInt()
                    StudioOutRow(2 + depth(i).coerceAtMost(8), null, store.modelNodeName(node), store.poseBone == node, false, null) {
                        store.choosePoseBone(node)
                    }
                }
            }
        }
    }
}

@Composable
private fun StudioOutRow(depth: Int, glyph: Char?, name: String, on: Boolean, keyed: Boolean,
                         delete: Pair<String, () -> Unit>?, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().height(44.dp).background(if (on) AureaColors.Brand.copy(alpha = .35f) else Color.Transparent)
        .tocavel(onClick = onClick).padding(start = (10 + depth * 14).dp, end = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        if (glyph != null) CupertinoIcon(glyph, 14.dp, AureaColors.Muted)
        else Text("◇", color = AureaColors.Muted, fontSize = 11.sp)
        Spacer(Modifier.width(6.dp))
        Text(name, color = if (on) AureaColors.Text else AureaColors.Muted, fontSize = 13.sp, maxLines = 1,
            overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
        if (keyed) Text("◆", color = AureaColors.Keyframe, fontSize = 10.sp)
        if (delete != null) Box(Modifier.size(44.dp).clip(RoundedCornerShape(8.dp)).tocavel(onClick = delete.second)
            .semantics { contentDescription = delete.first }, contentAlignment = Alignment.Center) {
            CupertinoIcon(CupertinoGlyph.Trash, 14.dp, AureaColors.Muted)
        }
    }
}

/**
 * Folha de baixo da Cena 3D (material e luz): sobe até ~55 % da tela e deixa a
 * cena à vista em cima, com véu leve (o [com.aurea.aurea.ui.ds.AureaModalSheet]
 * escurece demais para ver o material mudar). Par do iOS: `.sheet` com
 * `.presentationDetents([.medium, .large])` no SceneLayoutWorkspace.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun SceneSheet(onDismiss: () -> Unit, content: @Composable () -> Unit) {
    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true),
        containerColor = AureaColors.EditorPanel,
        contentColor = AureaColors.Text,
        scrimColor = Color(0x33000000),
        shape = RoundedCornerShape(topStart = 18.dp, topEnd = 18.dp),
        dragHandle = {
            Box(Modifier.padding(top = 10.dp, bottom = 6.dp).size(width = 36.dp, height = 5.dp)
                .clip(RoundedCornerShape(3.dp)).background(Color(0x2EFFFFFF)))
        },
    ) {
        Column(Modifier.navigationBarsPadding()) { content() }
    }
}

/** Título da folha + Fechar (48 dp). */
@Composable
private fun SceneSheetHeader(title: String, onClose: () -> Unit) {
    val close = stringResource(R.string.common_close)
    Row(Modifier.fillMaxWidth().height(48.dp).padding(start = 16.dp, end = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(title, color = AureaColors.Text, fontSize = 15.sp, fontWeight = FontWeight.W600, maxLines = 1, modifier = Modifier.weight(1f))
        Box(Modifier.size(48.dp).semantics { contentDescription = close }.tocavel(onClick = onClose), contentAlignment = Alignment.Center) {
            CupertinoIcon(CupertinoGlyph.Xmark, 16.dp, AureaColors.Muted)
        }
    }
}

@Composable
private fun SceneRoundButton(glyph: Char, description: String, onClick: () -> Unit) {
    Box(Modifier.size(40.dp).clip(CircleShape).background(ShellColors.FloatingDark)
        .semantics { contentDescription = description }.tocavel(haptic = true, onClick = onClick),
        contentAlignment = Alignment.Center) {
        CupertinoIcon(glyph, 19.dp, AureaColors.Text)
    }
}

@Composable
private fun ScenePillButton(glyph: Char, label: String, onClick: () -> Unit) {
    Row(Modifier.height(40.dp).clip(RoundedCornerShape(20.dp)).background(ShellColors.FloatingDark)
        .tocavel(haptic = true, onClick = onClick).padding(horizontal = 14.dp), verticalAlignment = Alignment.CenterVertically) {
        CupertinoIcon(glyph, 16.dp, AureaColors.Text)
        Spacer(Modifier.width(6.dp))
        Text(label, color = AureaColors.Text, fontSize = 13.sp, fontWeight = FontWeight.W500, maxLines = 1)
    }
}

/** Keep the user's exact text/caret while typing; evaluated Float updates must
 * never reformat a focused field or append a decimal zero into the next key. */
@Composable
private fun SceneNumberField(value: Float, label: String, identity: String, onEdit: (Float) -> Unit, modifier: Modifier = Modifier) {
    key(identity) {
        var draft by remember { mutableStateOf(TextFieldValue(value.toString())) }
        var focused by remember { mutableStateOf(false) }
        var pointerDown by remember { mutableStateOf(false) }
        var selectOnFocus by remember { mutableStateOf(false) }
        LaunchedEffect(value, focused) {
            if (!focused) draft = TextFieldValue(value.toString())
        }
        LaunchedEffect(focused, pointerDown, selectOnFocus) {
            if (focused && !pointerDown && selectOnFocus) {
                // Also covers keyboard/accessibility focus. Let Foundation finish
                // positioning the caret before applying the initial selection.
                withFrameNanos { }
                if (focused && !pointerDown && selectOnFocus) {
                    draft = draft.copy(selection = TextRange(0, draft.text.length))
                    selectOnFocus = false
                }
            }
        }
        OutlinedTextField(draft, { next ->
            if (next.text != draft.text) selectOnFocus = false
            draft = next
            next.text.toFloatOrNull()?.takeIf { it.isFinite() }?.let(onEdit)
        }, label = { Text(label) }, singleLine = true,
            // Número sempre LTR: em RTL "-1.5" sairia "1.5-".
            textStyle = androidx.compose.material3.LocalTextStyle.current.copy(textDirection = androidx.compose.ui.text.style.TextDirection.Ltr),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal),
            modifier = modifier
                .pointerInput(Unit) {
                    awaitEachGesture {
                        awaitFirstDown(requireUnconsumed = false, pass = PointerEventPass.Initial)
                        pointerDown = true
                        do {
                            val event = awaitPointerEvent(PointerEventPass.Final)
                        } while (event.changes.any { it.pressed })
                        // Observe, never consume: later taps, selection handles and
                        // long presses retain the text field's normal behavior.
                        pointerDown = false
                    }
                }
                .onFocusChanged { state ->
                    if (state.isFocused && !focused) selectOnFocus = true
                    if (!state.isFocused) selectOnFocus = false
                    focused = state.isFocused
                })
    }
}

/** Barra do Corrigir esqueleto (AutoRig 3D, 0.3.6): dica, progresso dos pesos e concluir. */
@Composable
private fun RigFixBar(store: EditorStore) {
    val working = store.autoRigState == 1
    val text = if (working) stringResource(R.string.rig3d_working, (store.autoRigProgress * 100f).toInt().coerceIn(0, 100))
               else stringResource(R.string.rig3d_fix_hint)
    Text(text, color = AureaColors.Text, fontSize = 12.sp,
        textAlign = TextAlign.Center, modifier = Modifier.padding(horizontal = 24.dp)
            .background(ShellColors.FloatingDark, RoundedCornerShape(12.dp)).padding(horizontal = 12.dp, vertical = 8.dp))
    Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 12.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
        if (working) ScenePillButton(CupertinoGlyph.ArrowCounterclockwise, stringResource(R.string.rig3d_cancel)) { store.cancelAutoRig() }
        ScenePillButton(CupertinoGlyph.CheckmarkAlt, stringResource(R.string.rig_done)) { store.setRigFix(false) }
    }
}

