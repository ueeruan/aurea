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
import com.aurea.aurea.R
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
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
    var areaOpen by remember { mutableStateOf(true) }
    var materialPanel by remember { mutableStateOf(EditorPanel.Element3D) }
    if (ui.adding) Dialog(onDismissRequest = { ui.adding = false }) {
        Surface { Box(Modifier.fillMaxWidth().fillMaxHeight(.8f)) { AddLayerPanel(store, ui) } }
    }
    if (materials) SceneSheet(onDismiss = { materials = false }) {
        PanelContent(store = store, panel = materialPanel, onClose = { materials = false }, onOpenPanel = { materialPanel = it }, onOpenEffectsBrowser = { ui.effectsBrowser = true }, modifier = Modifier.fillMaxWidth().fillMaxHeight(.55f),
            alwaysHeader = true)
    }
    if (lights) SceneSheet(onDismiss = { lights = false }) {
        Column(Modifier.fillMaxWidth().fillMaxHeight(.55f)) {
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
    // O painel 3D e os menus abrem o Esqueleto: ele vira a aba de baixo.
    LaunchedEffect(store.skeletonPanel) { if (store.skeletonPanel) { area = AREA_SKELETON; areaOpen = true } }
    LaunchedEffect(isModel) { if (!isModel && area == AREA_SKELETON) area = 0 }
    val chooseArea = { i: Int ->
        if (i == area) areaOpen = !areaOpen else { area = i; areaOpen = true }
        store.openSkeletonPanel(i == AREA_SKELETON && areaOpen)
    }
    Column(Modifier.fillMaxSize().background(AureaColors.Background)) {
        StudioHeader(store, selected)
        StudioMenuBar(store, selected, addMenu, { addMenu = !addMenu }, openMaterial, openLights) { chooseArea(AREA_SKELETON) }
        Box(Modifier.fillMaxWidth().weight(1f)) {
            // A cena 3D e os gizmos não espelham em árabe (ver `KeepLtr`).
            KeepLtr {
                stage(Modifier.fillMaxSize())
                SceneViewLabel(store, selected?.name, Modifier.align(Alignment.TopStart))
                SceneToolbar(store, Modifier.align(Alignment.TopStart))
                SceneNavColumn(store, Modifier.align(Alignment.TopEnd))
            }
            Column(Modifier.align(Alignment.BottomCenter).fillMaxWidth().padding(bottom = 8.dp),
                horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
                if (store.rigFixMode && store.humanoidJoints != null) RigFixBar(store)
            }
            if (addMenu) StudioAddMenu(store, ui) { addMenu = false }
        }
        StudioBottomArea(store, isModel, area, areaOpen, chooseArea, openMaterial, openLights)
    }
}

private const val AREA_SKELETON = 3

// --- Cena 3D: cabeçalho ----------------------------------------------------------

@Composable
private fun StudioHeader(store: EditorStore, selected: com.aurea.aurea.engine.LayerRow?) {
    KeepLtr {
        Row(Modifier.fillMaxWidth().height(46.dp).background(AureaColors.EditorBar).padding(horizontal = 4.dp),
            verticalAlignment = Alignment.CenterVertically) {
            StudioIcon(CupertinoGlyph.ChevronLeft, stringResource(R.string.editor_voltar_editor), store::exitSceneEditor)
            Spacer(Modifier.width(2.dp))
            var open by remember { mutableStateOf(false) }
            val pose = store.rigPoseMode
            val noRig = stringResource(R.string.s3d_no_rig)
            val canPose = selected?.kind == LayerType.Model3D.kind && store.poseBones != null
            val objectMode = stringResource(R.string.s3d_mode_object)
            val poseMode = stringResource(R.string.s3d_mode_pose)
            Box {
                Row(Modifier.height(36.dp).clip(RoundedCornerShape(9.dp)).background(AureaColors.EditorRowPill).tocavel { open = !open }
                    .padding(start = 10.dp, end = 8.dp).testTag("scene.mode"), verticalAlignment = Alignment.CenterVertically) {
                    CupertinoIcon(if (pose) CupertinoGlyph.PersonCropCircle else CupertinoGlyph.CubeFill, 15.dp, AureaColors.Text)
                    Spacer(Modifier.width(6.dp))
                    Text(if (pose) poseMode else objectMode, color = AureaColors.Text, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, maxLines = 1)
                    Spacer(Modifier.width(4.dp))
                    CupertinoIcon(CupertinoGlyph.ChevronDown, 11.dp, AureaColors.Muted)
                }
                AnchoredMenu(open, { open = false }, tone = AureaSurfaceTone.Editor, width = 220.dp, tag = "scene.mode.menu", entries = listOf(
                    MenuEntry.Item(objectMode, glyph = CupertinoGlyph.CubeFill, selected = !pose, checked = !pose) { if (store.rigPoseMode) store.setRigPose(false) },
                    MenuEntry.Item(poseMode, glyph = CupertinoGlyph.PersonCropCircle, sublabel = if (canPose) null else noRig, selected = pose, checked = pose, enabled = canPose) { store.setRigPose(true) },
                ))
            }
            Spacer(Modifier.width(6.dp))
            val space = stringResource(if (store.gizmoLocalSpace) R.string.s3d_space_local else R.string.s3d_space_global)
            val spaceDescription = stringResource(if (store.gizmoLocalSpace) R.string.i18n_local_xyz else R.string.i18n_world_xyz)
            Row(Modifier.height(36.dp).clip(RoundedCornerShape(9.dp)).background(AureaColors.EditorRowPill).tocavel(onClick = store::toggleGizmoSpace)
                .padding(horizontal = 10.dp).semantics { contentDescription = spaceDescription }.testTag("scene.space"), verticalAlignment = Alignment.CenterVertically) {
                Text(space, color = AureaColors.Text, fontSize = 13.sp, maxLines = 1)
            }
            Spacer(Modifier.weight(1f))
            StudioIcon(CupertinoGlyph.ArrowUturnLeft, stringResource(R.string.editor_desfazer), store::undo)
            StudioIcon(CupertinoGlyph.ArrowUturnRight, stringResource(R.string.editor_refazer), store::redo)
        }
    }
}

/** A linha de menus do Blender: Visualizar, Selecionar, Adicionar e Objeto (ou Pose). */
@Composable
private fun StudioMenuBar(store: EditorStore, selected: com.aurea.aurea.engine.LayerRow?, addOpen: Boolean, onAdd: () -> Unit,
                          openMaterial: () -> Unit, openLights: () -> Unit, openSkeleton: () -> Unit) {
    val pose = rigPoseActive(store)
    val isModel = selected?.kind == LayerType.Model3D.kind
    val viewEntry = { label: String, yaw: Float, pitch: Float -> MenuEntry.Item(label) { store.snapSceneView(yaw, pitch) } }
    KeepLtr {
        Row(Modifier.fillMaxWidth().height(42.dp).background(AureaColors.EditorBar).horizontalScroll(rememberScrollState()).padding(horizontal = 6.dp),
            verticalAlignment = Alignment.CenterVertically) {
            StudioMenu(stringResource(R.string.s3d_menu_view), "scene.menu.view", buildList {
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
            })
            val objects = store.layers.filter { it.isThreeD }
            StudioMenu(stringResource(R.string.s3d_menu_select), "scene.menu.select", buildList {
                if (pose) {
                    add(MenuEntry.Item(stringResource(R.string.s3d_select_none), enabled = store.poseBone >= 0) { store.choosePoseBone(-1) })
                    add(MenuEntry.Item(stringResource(R.string.skeleton_bones), glyph = CupertinoGlyph.PersonCropCircle) { openSkeleton() })
                } else {
                    add(MenuEntry.Item(stringResource(R.string.s3d_select_all)) { store.selectAll() })
                    add(MenuEntry.Item(stringResource(R.string.s3d_select_none), enabled = store.selection.isNotEmpty()) { store.clearSelection() })
                    if (objects.isNotEmpty()) add(MenuEntry.Divider)
                    objects.forEach { o -> add(MenuEntry.Item(o.name, glyph = LayerType.of(o.kind).glyph, checked = o.id == store.primary) { store.select(o.id, openOptions = false) }) }
                }
            })
            StudioMenuLabel(stringResource(R.string.panel_adicionar).removePrefix("+ "), "scene.menu.add", addOpen, onAdd)
            if (pose) {
                StudioMenu(stringResource(R.string.s3d_menu_pose), "scene.menu.pose", buildList {
                    add(MenuEntry.Item(stringResource(R.string.rig_key_bone), glyph = CupertinoGlyph.SuitDiamond, enabled = store.poseBone >= 0) { store.togglePoseKey(false) })
                    add(MenuEntry.Item(stringResource(R.string.rig_key_all), glyph = CupertinoGlyph.SuitDiamond) { store.togglePoseKey(true) })
                    add(MenuEntry.Divider)
                    add(MenuEntry.Item(stringResource(R.string.skeleton_copy)) { store.copyPose() })
                    add(MenuEntry.Item(stringResource(R.string.skeleton_paste), enabled = store.poseClipboard != null) { store.pastePose() })
                    add(MenuEntry.Item(stringResource(R.string.skeleton_mirror_pose)) { store.mirrorPose(true) })
                    add(MenuEntry.Divider)
                    add(MenuEntry.Item(stringResource(R.string.skeleton_zero_pose)) { store.zeroPose(true) })
                    add(MenuEntry.Item(stringResource(R.string.rig_reset_all), glyph = CupertinoGlyph.ArrowCounterclockwise) { store.resetPose(true) })
                })
            } else {
                StudioMenu(stringResource(R.string.s3d_menu_object), "scene.menu.object", buildList {
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
                })
            }
        }
    }
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
private fun StudioBottomArea(store: EditorStore, isModel: Boolean, area: Int, open: Boolean, onArea: (Int) -> Unit,
                             openMaterial: () -> Unit, openLights: () -> Unit) {
    val content = when (area) { 0 -> 176.dp; AREA_SKELETON -> 312.dp; else -> 248.dp }
    Column(Modifier.fillMaxWidth().background(AureaColors.EditorCanvas).navigationBarsPadding()) {
        Box(Modifier.fillMaxWidth().height(1.dp).background(AureaColors.EditorBarLine))
        Row(Modifier.fillMaxWidth().height(44.dp).background(AureaColors.EditorBar).padding(horizontal = 4.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Row(Modifier.weight(1f).horizontalScroll(rememberScrollState()), verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(2.dp)) {
                val tabs = buildList {
                    add(R.string.s3d_area_timeline); add(R.string.s3d_area_properties); add(R.string.s3d_area_outliner)
                    if (isModel) add(R.string.skeleton_title)
                }
                tabs.forEachIndexed { i, label ->
                    val on = i == area && open
                    Box(Modifier.height(36.dp).clip(RoundedCornerShape(9.dp)).background(if (on) AureaColors.EditorRowPill else Color.Transparent)
                        .tocavel { onArea(i) }.padding(horizontal = 9.dp).testTag("scene.area.$i"), contentAlignment = Alignment.Center) {
                        Text(stringResource(label), color = if (on) AureaColors.Text else AureaColors.Muted, fontSize = 13.sp,
                            fontWeight = if (on) FontWeight.SemiBold else FontWeight.Normal, maxLines = 1)
                    }
                }
            }
            // Recolhida, a área perde o transporte: o play fica aqui (aberta, as abas precisam do espaço).
            if (!open) {
                val playDesc = stringResource(if (store.playing) R.string.s3d_pause else R.string.s3d_play)
                Box(Modifier.size(40.dp).semantics { contentDescription = playDesc }.tocavel(haptic = true) { store.togglePlayback() }, contentAlignment = Alignment.Center) {
                    CupertinoIcon(if (store.playing) CupertinoGlyph.PauseFill else CupertinoGlyph.PlayFill, 17.dp, AureaColors.Text)
                }
            }
            val foldDesc = stringResource(if (open) R.string.s3d_area_collapse else R.string.s3d_area_expand)
            Box(Modifier.size(40.dp).semantics { contentDescription = foldDesc }.tocavel { onArea(area) }.testTag("scene.area.fold"), contentAlignment = Alignment.Center) {
                CupertinoIcon(if (open) CupertinoGlyph.ChevronDown else CupertinoGlyph.ChevronUp, 15.dp, AureaColors.Muted)
            }
        }
        if (open) Box(Modifier.fillMaxWidth().height(content)) {
            when (area) {
                0 -> StudioTimeline(store)
                1 -> StudioProperties(store, openMaterial, openLights)
                2 -> StudioOutliner(store)
                else -> com.aurea.aurea.editor.panels.SkeletonPanel(store, Modifier.fillMaxSize())
            }
        }
    }
}

/** Quadros (da linha do tempo) com chave de transformação na camada. */
private fun transformKeyFrames(store: EditorStore, layer: Long): List<Int> {
    val detail = store.detail?.takeIf { it.id == layer }
    val shift = detail?.let { it.startFrame - it.offsetFrames } ?: 0
    return store.keyframes[layer].orEmpty().filter { it.effectIndex < 0 && it.property <= 12 && it.timelineVisible }
        .map { it.time + shift }.distinct().sorted()
}

@Composable
private fun StudioTimeline(store: EditorStore) {
    val total = store.project.durationFrames.coerceAtLeast(2)
    val end = total - 1
    val layer = store.primary
    val pose = store.rigPoseMode && store.poseBones != null
    var picked by remember(layer) { mutableStateOf<Int?>(null) }
    KeepLtr {
        Column(Modifier.fillMaxSize()) {
            Row(Modifier.fillMaxWidth().height(44.dp).padding(horizontal = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                val auto = store.autoKeyTransforms
                val autoDesc = stringResource(if (auto) R.string.s3d_autokey_on else R.string.s3d_autokey_off)
                Box(Modifier.size(34.dp).clip(RoundedCornerShape(10.dp)).background(if (auto) AureaColors.EditorRowPill else Color.Transparent)
                    .tocavel { store.autoKeyTransforms = !auto }.semantics { contentDescription = autoDesc }, contentAlignment = Alignment.Center) {
                    Box(Modifier.size(12.dp).clip(CircleShape).background(if (auto) AureaColors.Danger else AureaColors.Muted))
                }
                Spacer(Modifier.width(4.dp))
                StudioIcon(CupertinoGlyph.BackwardEnd, stringResource(R.string.s3d_start)) { store.seek(0) }
                StudioIcon(CupertinoGlyph.ChevronLeft, stringResource(R.string.s3d_prev_key)) { store.stepToKeyframe(-1) }
                val playDesc = stringResource(if (store.playing) R.string.s3d_pause else R.string.s3d_play)
                Box(Modifier.size(38.dp).clip(CircleShape).background(AureaColors.Text).tocavel(haptic = true) { store.togglePlayback() }
                    .semantics { contentDescription = playDesc }, contentAlignment = Alignment.Center) {
                    CupertinoIcon(if (store.playing) CupertinoGlyph.PauseFill else CupertinoGlyph.PlayFill, 16.dp, AureaColors.Background)
                }
                StudioIcon(CupertinoGlyph.ChevronRight, stringResource(R.string.s3d_next_key)) { store.stepToKeyframe(1) }
                StudioIcon(CupertinoGlyph.ForwardEnd, stringResource(R.string.s3d_end)) { store.seek(end) }
                StudioIcon(CupertinoGlyph.SuitDiamond, stringResource(R.string.s3d_insert_key_desc)) {
                    if (pose) store.togglePoseKey(store.poseBone < 0)
                    else if (layer != null) store.toggleTransformKeyframe(IntArray(9) { it }, layer)
                }
                StudioIcon(CupertinoGlyph.Xmark, stringResource(R.string.s3d_delete_key)) {
                    val at = picked ?: store.playhead
                    val d = store.detail
                    if (layer != null && d != null && d.id == layer) {
                        val local = at - d.startFrame + d.offsetFrames
                        store.keyframes[layer].orEmpty().filter { it.effectIndex < 0 && it.property <= 12 && it.time == local }
                            .forEach { store.deleteKeyframe(layer, it) }
                    }
                    picked = null
                }
                Spacer(Modifier.weight(1f))
                Text("${store.playhead}", color = AureaColors.Text, fontSize = 13.sp, fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.clip(RoundedCornerShape(8.dp)).background(AureaColors.EditorGlyphBox).padding(horizontal = 8.dp, vertical = 6.dp))
            }
            val objects = store.layers.filter { it.isThreeD }
            val rows = buildList {
                add(-1L)
                objects.filter { it.id == layer || store.keyframes[it.id].orEmpty().any { k -> k.effectIndex < 0 && k.property <= 12 } }.forEach { add(it.id) }
            }
            val keysOf = remember(store.keyframes, store.detail, rows) {
                rows.associateWith { id -> if (id == -1L) rows.filter { it != -1L }.flatMap { transformKeyFrames(store, it) }.distinct()
                                            else transformKeyFrames(store, id) }
            }
            val rowH = 34.dp; val rulerH = 24.dp
            val measurer = rememberTextMeasurer()
            val keyColor = AureaColors.Text; val keyPicked = AureaColors.Keyframe; val accent = AureaColors.Brand
            val onAccent = AureaColors.OnAction; val grid = AureaColors.TickWeak; val muted = AureaColors.Muted; val bar = AureaColors.EditorBar
            Row(Modifier.fillMaxSize()) {
                Column(Modifier.width(122.dp).fillMaxHeight().background(AureaColors.EditorBar.copy(alpha = .6f))) {
                    Spacer(Modifier.height(rulerH))
                    Column(Modifier.verticalScroll(rememberScrollState())) {
                        rows.forEach { id ->
                            val on = id == layer
                            val name = if (id == -1L) stringResource(R.string.s3d_summary) else objects.firstOrNull { it.id == id }?.name ?: ""
                            Row(Modifier.fillMaxWidth().height(rowH).background(if (on) AureaColors.Brand.copy(alpha = .35f) else Color.Transparent)
                                .tocavel { if (id != -1L) store.select(id, openOptions = false) }
                                .padding(start = if (id == -1L) 8.dp else 18.dp, end = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                                Text(name, color = if (id == -1L) AureaColors.Text else AureaColors.Muted, fontSize = 11.sp,
                                    fontWeight = if (id == -1L) FontWeight.SemiBold else FontWeight.Normal, maxLines = 1, overflow = TextOverflow.Ellipsis)
                            }
                        }
                    }
                }
                Canvas(Modifier.weight(1f).fillMaxHeight().pointerInput(rows, keysOf, end) {
                    awaitEachGesture {
                        val down = awaitFirstDown()
                        val pad = 10.dp.toPx(); val w = size.width - pad * 2
                        fun frameAt(x: Float) = ((x - pad) / w * end).roundToInt().coerceIn(0, end)
                        fun xOf(f: Int) = pad + f / end.toFloat() * w
                        if (store.playing) store.pause()
                        val row = ((down.position.y - rulerH.toPx()) / rowH.toPx()).toInt()
                        val track = if (down.position.y > rulerH.toPx()) rows.getOrNull(row) else null
                        val frames = track?.let { keysOf[it] }.orEmpty()
                        val near = frames.minByOrNull { abs(xOf(it) - down.position.x) }?.takeIf { abs(xOf(it) - down.position.x) < 12.dp.toPx() }
                        if (near != null) {
                            picked = near
                            if (track != null && track != -1L && track != layer) store.select(track, openOptions = false)
                            store.seek(near)
                        } else { picked = null; store.seek(frameAt(down.position.x)) }
                        while (true) {
                            val e = awaitPointerEvent(); val ch = e.changes.firstOrNull { it.pressed } ?: break
                            if (near == null) store.seek(frameAt(ch.position.x))
                            ch.consume()
                        }
                    }
                }) {
                    val pad = 10.dp.toPx(); val w = size.width - pad * 2
                    fun x(f: Int) = pad + f / end.toFloat() * w
                    val rt = rulerH.toPx(); val rh = rowH.toPx()
                    drawRect(bar.copy(alpha = .45f), Offset.Zero, Size(size.width, rt))
                    val step = when { end > 600 -> 60; end > 240 -> 30; end > 120 -> 20; else -> 10 }
                    for (f in 0..end step step) {
                        val xx = x(f)
                        drawLine(grid.copy(alpha = .5f), Offset(xx, rt), Offset(xx, size.height), 1f)
                        if ((f / step) % 2 == 0) {
                            val l = measurer.measure("$f", TextStyle(color = muted, fontSize = 9.sp))
                            drawText(l, topLeft = Offset(xx - l.size.width / 2f, (rt - l.size.height) / 2))
                        }
                    }
                    rows.forEachIndexed { r, id ->
                        val top = rt + r * rh
                        if (r % 2 == 1) drawRect(keyColor.copy(alpha = .025f), Offset(0f, top), Size(size.width, rh))
                        val cy = top + rh / 2
                        keysOf[id].orEmpty().forEach { f ->
                            val c = Offset(x(f), cy); val rr = (if (id == -1L) 6.dp else 5.dp).toPx()
                            val p = Path().apply { moveTo(c.x, c.y - rr); lineTo(c.x + rr, c.y); lineTo(c.x, c.y + rr); lineTo(c.x - rr, c.y); close() }
                            drawPath(p, if (f == picked) keyPicked else keyColor)
                            drawPath(p, Color(0xCC05070A), style = Stroke(1.dp.toPx()))
                        }
                    }
                    val px = x(store.playhead.coerceIn(0, end))
                    drawLine(accent, Offset(px, rt), Offset(px, size.height), 2.dp.toPx())
                    val label = measurer.measure("${store.playhead}", TextStyle(color = onAccent, fontSize = 10.sp, fontWeight = FontWeight.Bold))
                    val bw = label.size.width + 10.dp.toPx()
                    drawRoundRect(accent, Offset(px - bw / 2, 2.dp.toPx()), Size(bw, rt - 4.dp.toPx()), CornerRadius(5.dp.toPx()))
                    drawText(label, topLeft = Offset(px - label.size.width / 2f, (rt - label.size.height) / 2))
                }
            }
        }
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

