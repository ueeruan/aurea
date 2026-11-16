package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
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
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.AureaPropertyPanel
import com.aurea.aurea.editor.panels.ColorRequest
import com.aurea.aurea.editor.panels.ColorRequestHost
import com.aurea.aurea.editor.panels.EditorPanel
import com.aurea.aurea.editor.panels.Environment3DTab
import com.aurea.aurea.editor.panels.LocalElement3DOnly
import com.aurea.aurea.editor.panels.ObjectShadowSection
import com.aurea.aurea.editor.panels.PanelContent
import com.aurea.aurea.editor.panels.PanelEnv
import com.aurea.aurea.editor.panels.Render3DTab
import com.aurea.aurea.editor.panels.SkeletonPanel
import com.aurea.aurea.editor.panels.Tab3D
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.engine.TrackProperty
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AnchoredMenu
import com.aurea.aurea.ui.ds.AureaSurfaceTone
import com.aurea.aurea.ui.ds.KeypadRequest
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.ds.NumericKeypadSheet
import com.aurea.aurea.ui.i18n.KeepLtr
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel
import java.util.Locale

// =============================================================================
// Outliner: a hierarquia da cena (pais e filhos), olho, cadeado, renomear e arrastar para um pai
// =============================================================================

private class OutlinerRow(val row: LayerRow?, val depth: Int, val expandable: Boolean, val bone: Int = -1, val boneName: String = "")

private val OutlinerRowHeight = 28.dp

@Composable
internal fun ColumnScope.PcSceneOutliner(store: EditorStore) {
    val layers = store.layers
    val objects = PcScene3D.objects(store)
    val ids = remember(objects) { objects.map { it.id }.toSet() }
    var collapsed by remember { mutableStateOf(setOf<Long>()) }
    var renaming by remember { mutableStateOf<Long?>(null) }
    var dragging by remember { mutableStateOf<Long?>(null) }
    var dropIndex by remember { mutableIntStateOf(-1) }
    val bones = store.poseBones
    val primary = store.primary
    // `parentIndex` é índice na mesma lista de camadas; pai fora do 3D = raiz da cena.
    val rows = remember(layers, collapsed, bones, primary) {
        val parentOf = objects.associate { o -> o.id to (layers.getOrNull(o.parentIndex)?.id?.takeIf { it in ids } ?: 0L) }
        val children = objects.groupBy { parentOf[it.id] ?: 0L }
        buildList {
            fun visit(o: LayerRow, depth: Int) {
                val kids = children[o.id].orEmpty()
                val hasBones = o.id == primary && bones != null && o.kind == LayerType.Model3D.kind
                add(OutlinerRow(o, depth, kids.isNotEmpty() || hasBones))
                if (o.id in collapsed || depth > 24) return
                if (hasBones && bones != null) {
                    val count = bones.size / BONE_FLOATS
                    fun boneDepth(i: Int): Int {
                        var dd = 0; var p = bones[i * BONE_FLOATS + 1].toInt()
                        while (p in 0 until count && dd < 48) { dd++; p = bones[p * BONE_FLOATS + 1].toInt() }
                        return dd
                    }
                    for (i in 0 until count) {
                        val node = bones[i * BONE_FLOATS].toInt()
                        add(OutlinerRow(null, depth + 1 + boneDepth(i).coerceAtMost(10), false, node, store.modelNodeName(node)))
                    }
                }
                kids.forEach { visit(it, depth + 1) }
            }
            children[0L].orEmpty().forEach { visit(it, 0) }
        }
    }
    Row(Modifier.fillMaxWidth().height(32.dp).padding(start = 10.dp, end = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        CupertinoIcon(CupertinoGlyph.RectangleStack, 14.dp, AureaColors.Muted)
        Spacer(Modifier.width(7.dp))
        Text(stringResource(R.string.s3d_area_outliner), color = AureaColors.Text, fontSize = 12.5.sp, fontWeight = FontWeight.Medium, modifier = Modifier.weight(1f), maxLines = 1)
        Text(objects.size.toString(), color = AureaColors.Subtle, fontSize = 11.sp)
        Spacer(Modifier.width(4.dp))
        var add by remember { mutableStateOf(false) }
        Box {
            SceneHeaderIcon(CupertinoGlyph.Plus, stringResource(R.string.panel_adicionar).removePrefix("+ ") + "  (Shift+A)", false) { add = !add }
            AnchoredMenu(add, { add = false }, entries = sceneAddEntries(store), tone = AureaSurfaceTone.Editor, width = 250.dp, alignEnd = true, tag = "pc3d.outliner.add")
        }
    }
    Box(Modifier.fillMaxWidth().height(1.dp).background(AureaColors.Hairline))
    val scroll = rememberScrollState()
    val density = LocalDensity.current
    val rowPx = with(density) { OutlinerRowHeight.toPx() }
    val currentRows by rememberUpdatedState(rows)
    Column(Modifier.fillMaxWidth().weight(1f).verticalScroll(scroll)
        // Arrastar uma linha para cima de outra = virar filho dela (como no Outliner do Blender).
        .pointerInput(store) {
            detectDragGestures(
                onDragStart = { at -> dragging = currentRows.getOrNull(((at.y) / rowPx).toInt() - 1)?.row?.id },
                onDragEnd = {
                    val child = dragging
                    val target = currentRows.getOrNull(dropIndex)?.row
                    if (child != null && target != null && target.id != child) {
                        val moving = if (child in store.selection) store.selection.toList() else listOf(child)
                        store.setParentMany(moving.filter { it != target.id }, target.id)
                    } else if (child != null && dropIndex == -2) {
                        store.setParentMany(if (child in store.selection) store.selection.toList() else listOf(child), 0L)
                    }
                    dragging = null; dropIndex = -1
                },
                onDragCancel = { dragging = null; dropIndex = -1 },
            ) { change, _ ->
                if (dragging != null) {
                    change.consume()
                    val i = (change.position.y / rowPx).toInt() - 1
                    // Em cima da linha "Cena" (a primeira) = soltar o pai.
                    dropIndex = if (i < 0) -2 else if (currentRows.getOrNull(i)?.row != null) i else -1
                }
            }
        }) {
        OutlinerLine(0, CupertinoGlyph.Cube, stringResource(R.string.s3d_scene), on = false, active = false, drop = dropIndex == -2) {}
        if (objects.isEmpty()) {
            Text(stringResource(R.string.pc3d_outliner_empty), color = AureaColors.Subtle, fontSize = 11.5.sp, modifier = Modifier.padding(horizontal = 14.dp, vertical = 10.dp))
        }
        rows.forEachIndexed { index, r ->
            val o = r.row
            if (o == null) {
                OutlinerLine(r.depth + 1, CupertinoGlyph.Link, r.boneName, on = store.poseBone == r.bone && store.rigPoseMode, active = false, drop = false, small = true) {
                    if (!store.rigPoseMode) store.setRigPose(true)
                    store.choosePoseBone(r.bone)
                }
            } else key(o.id) {
                OutlinerObject(store, o, r, selected = o.id in store.selection, active = o.id == primary, drop = dropIndex == index && dragging != o.id,
                    folded = o.id in collapsed, renaming = renaming == o.id,
                    onFold = { collapsed = if (o.id in collapsed) collapsed - o.id else collapsed + o.id },
                    onRename = { renaming = if (it) o.id else null })
            }
        }
        Spacer(Modifier.height(8.dp))
    }
}

@Composable
private fun OutlinerLine(depth: Int, glyph: Char, name: String, on: Boolean, active: Boolean, drop: Boolean, small: Boolean = false, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().height(OutlinerRowHeight)
        .background(when { drop -> AureaColors.Accent.copy(alpha = 0.28f); active -> AureaColors.Brand.copy(alpha = 0.42f); on -> AureaColors.Brand.copy(alpha = 0.22f); else -> Color.Transparent })
        .tocavel(shrink = 1f, onClick = onClick).padding(start = (8 + depth * 14).dp, end = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        CupertinoIcon(glyph, if (small) 11.dp else 13.dp, AureaColors.Muted)
        Spacer(Modifier.width(7.dp))
        Text(name, color = if (on || active) AureaColors.Text else AureaColors.Muted, fontSize = if (small) 11.5.sp else 12.5.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

@Composable
private fun OutlinerObject(store: EditorStore, o: LayerRow, r: OutlinerRow, selected: Boolean, active: Boolean, drop: Boolean, folded: Boolean,
                           renaming: Boolean, onFold: () -> Unit, onRename: (Boolean) -> Unit) {
    val keyed = store.keyframes[o.id].orEmpty().any { it.effectIndex < 0 && it.property <= 12 }
    Row(Modifier.fillMaxWidth().height(OutlinerRowHeight)
        .background(when { drop -> AureaColors.Accent.copy(alpha = 0.28f); active -> AureaColors.Brand.copy(alpha = 0.42f); selected -> AureaColors.Brand.copy(alpha = 0.22f); else -> Color.Transparent })
        .pointerInput(o.id) {
            detectTapGestures(
                onDoubleTap = { onRename(true) },
                onTap = { PcScene3D.click(store, o.id, com.aurea.pc.PcKeys.shift || com.aurea.pc.PcKeys.ctrl) },
            )
        }.padding(start = (8 + (r.depth + 1) * 14 - 14).dp, end = 2.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(14.dp).then(if (r.expandable) Modifier.tocavel(shrink = 1f, onClick = onFold) else Modifier), contentAlignment = Alignment.Center) {
            if (r.expandable) CupertinoIcon(if (folded) CupertinoGlyph.ChevronRight else CupertinoGlyph.ChevronDown, 8.dp, AureaColors.Muted)
        }
        val collection = o.kind == LayerType.Null.kind && r.expandable
        CupertinoIcon(if (collection) CupertinoGlyph.Folder else LayerType.of(o.kind).glyph, 13.dp, if (active) AureaColors.Accent else AureaColors.Muted)
        Spacer(Modifier.width(7.dp))
        if (renaming) {
            SceneNameField(o.name, Modifier.weight(1f), onCommit = { if (it.isNotBlank() && it != o.name) store.renameLayer(o.id, it.trim()); onRename(false) })
        } else {
            Text(o.name, color = if (!o.visible) AureaColors.Subtle else if (selected) AureaColors.Text else AureaColors.Muted, fontSize = 12.5.sp,
                maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
        }
        if (keyed) Text("◆", color = AureaColors.Keyframe, fontSize = 9.sp, modifier = Modifier.padding(horizontal = 3.dp))
        // Coleção (um Vazio com membros) ou Shift: o olho e o cadeado valem para a família inteira.
        val cascade = { r.expandable && (o.kind == LayerType.Null.kind || com.aurea.pc.PcKeys.shift) }
        OutlinerToggle(if (o.visible) CupertinoGlyph.Eye else CupertinoGlyph.EyeSlash, stringResource(R.string.pc3d_visible), o.visible) {
            if (cascade()) PcSceneOps.setFamily(store, o.id, visible = !o.visible) else store.setLayerVisible(o.id, !o.visible)
        }
        OutlinerToggle(if (o.locked) CupertinoGlyph.LockFill else CupertinoGlyph.LockOpen, stringResource(R.string.pc3d_locked), !o.locked) {
            if (cascade()) PcSceneOps.setFamily(store, o.id, locked = !o.locked) else store.setLayerLocked(o.id, !o.locked)
        }
    }
}

@Composable
private fun OutlinerToggle(glyph: Char, description: String, bright: Boolean, onClick: () -> Unit) {
    Box(Modifier.size(24.dp).clip(RoundedCornerShape(5.dp)).semantics { contentDescription = description }.tocavel(shrink = 1f, onClick = onClick), contentAlignment = Alignment.Center) {
        CupertinoIcon(glyph, 12.dp, if (bright) AureaColors.Muted else AureaColors.Accent)
    }
}

/** Renomear na própria linha: Enter grava, sair do campo também. */
@Composable
private fun SceneNameField(initial: String, modifier: Modifier, onCommit: (String) -> Unit) {
    var value by remember { mutableStateOf(TextFieldValue(initial, TextRange(0, initial.length))) }
    val focus = remember { FocusRequester() }
    var focused by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) { focus.requestFocus() }
    // Enquanto o campo existe, as teclas são dele; Esc desiste (o nome fica como estava).
    DisposableEffect(Unit) {
        val cancel = { onCommit(initial) }
        PcScene3D.typingCancel = cancel
        onDispose { if (PcScene3D.typingCancel === cancel) PcScene3D.typingCancel = null }
    }
    BasicTextField(value, { value = it }, singleLine = true,
        textStyle = TextStyle(color = AureaColors.Text, fontSize = 12.5.sp), cursorBrush = SolidColor(AureaColors.Accent),
        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done), keyboardActions = KeyboardActions(onDone = { onCommit(value.text) }),
        modifier = modifier.clip(RoundedCornerShape(4.dp)).background(AureaColors.EditorGlyphBox).padding(horizontal = 4.dp, vertical = 3.dp)
            .focusRequester(focus).onFocusChanged { if (focused && !it.isFocused) onCommit(value.text); focused = it.isFocused })
}

// =============================================================================
// Editor de propriedades: abas em ícones à esquerda e, em cada aba, os painéis do Aurea
// =============================================================================

private class PropTab(val id: Int, val glyph: Char, val label: Int)

@Composable
internal fun ColumnScope.PcSceneProperties(store: EditorStore, ui: EditorUi) {
    val d = store.detail?.takeIf { it.id == store.primary }
    val row = store.layers.firstOrNull { it.id == store.primary }
    val kind = row?.kind
    val isModel = kind == LayerType.Model3D.kind
    val isLight = kind == LayerType.Light.kind
    val isCamera = kind == LayerType.Camera.kind
    // Forma 3D, texto 3D e imagem 3D são "modelos" para o motor; o painel 3D sabe qual é qual.
    val hasMaterial = row != null && !isLight && !isCamera && kind != LayerType.Null.kind && (isModel || store.text3d != null || store.shape3d != null || store.image3d != null)
    val importedModel = isModel && store.shape3d == null && store.text3d == null && store.image3d == null
    val tabs = buildList {
        add(PropTab(PcScene3D.PROP_RENDER, CupertinoGlyph.Camera, R.string.env3d_tab_render))
        add(PropTab(PcScene3D.PROP_WORLD, CupertinoGlyph.Lightbulb, R.string.pc3d_tab_world))
        if (row != null) {
            add(PropTab(PcScene3D.PROP_OBJECT, CupertinoGlyph.Cube, R.string.s3d_menu_object))
            add(PropTab(PcScene3D.PROP_MODIFIERS, CupertinoGlyph.Wrench, R.string.pc3d_tab_modifiers))
            add(PropTab(PcScene3D.PROP_CONSTRAINTS, CupertinoGlyph.Link, R.string.pc3d_tab_constraints))
            add(PropTab(PcScene3D.PROP_DATA, LayerType.of(row.kind).glyph, R.string.pc3d_tab_data))
            if (hasMaterial) add(PropTab(PcScene3D.PROP_MATERIAL, CupertinoGlyph.CircleLefthalfFill, R.string.ui3d_tab_material))
            if (importedModel) add(PropTab(PcScene3D.PROP_RIG, CupertinoGlyph.Person3, R.string.rig3d_tab))
            if (importedModel) add(PropTab(PcScene3D.PROP_BONE, CupertinoGlyph.PersonCropCircle, R.string.pc3d_tab_bone))
        }
    }
    val tab = tabs.firstOrNull { it.id == PcScene3D.propsTab } ?: tabs.firstOrNull { it.id == PcScene3D.PROP_OBJECT } ?: tabs.first()
    Row(Modifier.fillMaxWidth().height(32.dp).padding(start = 10.dp, end = 8.dp), verticalAlignment = Alignment.CenterVertically) {
        CupertinoIcon(CupertinoGlyph.SliderHorizontal3, 14.dp, AureaColors.Muted)
        Spacer(Modifier.width(7.dp))
        Text(stringResource(R.string.s3d_area_properties) + "  ·  " + stringResource(tab.label), color = AureaColors.Text, fontSize = 12.5.sp,
            fontWeight = FontWeight.Medium, modifier = Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis)
        if (row != null) Text(row.name, color = AureaColors.Subtle, fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
    Box(Modifier.fillMaxWidth().height(1.dp).background(AureaColors.Hairline))
    Row(Modifier.fillMaxWidth().weight(1f)) {
        // As abas em ícones, à esquerda como no Blender.
        Column(Modifier.width(38.dp).fillMaxHeight().background(AureaColors.EditorPanelHigh).verticalScroll(rememberScrollState()).padding(vertical = 4.dp),
            horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(2.dp)) {
            tabs.forEach { t ->
                val on = t.id == tab.id
                SceneTooltip(stringResource(t.label)) {
                    Box(Modifier.size(30.dp).clip(RoundedCornerShape(7.dp)).background(if (on) AureaColors.AccentDim else Color.Transparent)
                        .tocavel(shrink = 1f) { PcScene3D.propsTab = t.id }, contentAlignment = Alignment.Center) {
                        CupertinoIcon(t.glyph, 16.dp, if (on) AureaColors.Accent else AureaColors.Muted)
                    }
                }
            }
        }
        Box(Modifier.width(1.dp).fillMaxHeight().background(AureaColors.Hairline))
        Box(Modifier.weight(1f).fillMaxHeight()) {
            val openPanel: (EditorPanel) -> Unit = { pcOpenPanel(store, ui, it) }
            when (tab.id) {
                PcScene3D.PROP_RENDER -> ScenePanelHost(store, openPanel) { env -> ScrollColumn { Render3DTab(env) } }
                PcScene3D.PROP_WORLD -> ScenePanelHost(store, openPanel) { env -> ScrollColumn { Environment3DTab(env) } }
                PcScene3D.PROP_OBJECT -> if (d != null && row != null) ObjectTab(store, row) else EmptyState(stringResource(R.string.s3d_nothing_selected))
                PcScene3D.PROP_MODIFIERS -> if (d == null) EmptyState(stringResource(R.string.s3d_nothing_selected)) else Column(Modifier.fillMaxSize()) {
                    // O motor não tem pilha de modificadores de malha; o que deforma/anima um objeto no Aurea são
                    // os EFEITOS da camada (Animar parte 3D, Esconder partes 3D, Trocar textura 3D…): é a pilha daqui.
                    Text(stringResource(R.string.pc3d_modifiers_hint), color = AureaColors.Subtle, fontSize = 11.sp, modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp))
                    Box(Modifier.fillMaxWidth().height(1.dp).background(AureaColors.Hairline))
                    PanelContent(store = store, panel = EditorPanel.Effects, onClose = {}, onOpenPanel = openPanel,
                        onOpenEffectsBrowser = { ui.effectsBrowser = true }, modifier = Modifier.fillMaxWidth().weight(1f))
                }
                PcScene3D.PROP_DATA -> when {
                    d == null -> EmptyState(stringResource(R.string.s3d_nothing_selected))
                    isLight -> {
                        val light = remember(store.primary, store.curveRevision, store.detail, store.sceneSettingsRevision, store.playhead) { store.lightInfo() }
                        if (light != null) ScrollColumn { AureaPropertyPanel(store, "light", light, onOpenCurve = { PanelRouting.curveHost?.invoke() }) }
                        else EmptyState(stringResource(R.string.pc3d_data_empty))
                    }
                    isCamera -> PanelContent(store = store, panel = EditorPanel.Camera, onClose = {}, onOpenPanel = openPanel,
                        onOpenEffectsBrowser = { ui.effectsBrowser = true }, modifier = Modifier.fillMaxSize())
                    store.text3d != null || store.image3d != null -> Element3DOnly(store, ui, Tab3D.Shape, openPanel)
                    isModel || store.shape3d != null -> Element3DOnly(store, ui, Tab3D.Parts, openPanel)
                    else -> EmptyState(stringResource(R.string.pc3d_data_empty))
                }
                PcScene3D.PROP_MATERIAL -> Element3DOnly(store, ui, Tab3D.Material, openPanel)
                PcScene3D.PROP_CONSTRAINTS -> if (d != null && row != null) ConstraintsTab(store, row) else EmptyState(stringResource(R.string.s3d_nothing_selected))
                PcScene3D.PROP_RIG -> Element3DOnly(store, ui, Tab3D.Rig, openPanel)
                else -> SkeletonPanel(store, Modifier.fillMaxSize())
            }
        }
    }
}

@Composable
private fun ScrollColumn(content: @Composable ColumnScope.() -> Unit) =
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()), content = content)

@Composable
private fun EmptyState(text: String) {
    Box(Modifier.fillMaxSize().padding(18.dp), contentAlignment = Alignment.Center) {
        Text(text, color = AureaColors.Subtle, fontSize = 12.sp, textAlign = TextAlign.Center)
    }
}

/** O painel 3D do Aurea numa aba só (material, forma, partes ou esqueleto), sem o trilho nem as abas dele. */
@Composable
private fun Element3DOnly(store: EditorStore, ui: EditorUi, tab: Tab3D, openPanel: (EditorPanel) -> Unit) {
    CompositionLocalProvider(LocalElement3DOnly provides tab) {
        PanelContent(store = store, panel = EditorPanel.Element3D, onClose = {}, onOpenPanel = openPanel,
            onOpenEffectsBrowser = { ui.effectsBrowser = true }, modifier = Modifier.fillMaxSize())
    }
}

/**
 * Hospeda as abas de CENA do painel 3D (Render e Ambiente), que não dependem de uma
 * camada escolhida: o mesmo `PanelEnv` do app, com o teclado numérico e o seletor
 * de cor que as linhas pedem.
 */
@Composable
internal fun ScenePanelHost(store: EditorStore, openPanel: (EditorPanel) -> Unit, content: @Composable (PanelEnv) -> Unit) {
    var keypad by remember { mutableStateOf<KeypadRequest?>(null) }
    var color by remember { mutableStateOf<ColorRequest?>(null) }
    val open by rememberUpdatedState(openPanel)
    val env = remember(store) {
        PanelEnv(store = store, onClose = {}, onOpenPanel = { open(it) }, onOpenEffectsBrowser = {},
            openKeypad = { keypad = it }, openColor = { color = it }, returnTo = { EditorPanel.Element3D })
    }
    content(env)
    keypad?.let { r -> NumericKeypadSheet(r, onDismiss = { keypad = null }) }
    color?.let { r ->
        DisposableEffect(r) { onDispose { r.finish() } }
        key(r) { ColorRequestHost(store, EditorPanel.Element3D, r, onClose = { if (color === r) color = null }) }
    }
}

/**
 * Aba Restrições. No Aurea a restrição É uma expressão na trilha de transformação (o motor já
 * avalia, salva e exporta): escolher o alvo grava o texto; "Remover" apaga. Uma por grupo
 * (Posição, Rotação, Escala), como as expressões do app.
 */
@Composable
private fun ConstraintsTab(store: EditorStore, row: LayerRow) {
    // Relê quando a camada ou as expressões mudam.
    store.detail; store.aeAnimRevision; store.layers
    val kinds = listOf(
        Triple(PcSceneOps.CONSTRAINT_COPY_LOCATION, R.string.pc3d_constraint_copy_location, 0),
        Triple(PcSceneOps.CONSTRAINT_COPY_ROTATION, R.string.pc3d_constraint_copy_rotation, 1),
        Triple(PcSceneOps.CONSTRAINT_COPY_SCALE, R.string.pc3d_constraint_copy_scale, 2),
        Triple(PcSceneOps.CONSTRAINT_TRACK_TO, R.string.pc3d_constraint_track_to, 1),
    )
    val targets = PcScene3D.objects(store).filter { it.id != row.id }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(10.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(stringResource(R.string.pc3d_constraints_hint), color = AureaColors.Subtle, fontSize = 11.sp)
        PropSection(stringResource(R.string.pc3d_constraint_add))
        kinds.forEach { (kind, label, _) ->
            var open by remember(row.id, kind) { mutableStateOf(false) }
            Box {
                PropLine(stringResource(label), stringResource(R.string.pc3d_constraint_target)) { open = !open }
                AnchoredMenu(open, { open = false }, entries = if (targets.isEmpty()) listOf(MenuEntry.Item(stringResource(R.string.pc3d_constraint_no_target), enabled = false) {})
                    else targets.take(40).map { t ->
                        MenuEntry.Item(t.name, glyph = LayerType.of(t.kind).glyph) {
                            PcSceneOps.addConstraint(store, kind, t)?.let { com.aurea.pc.PcToasts.show(it, true) }
                        }
                    }, tone = AureaSurfaceTone.Editor, width = 250.dp, tag = "pc3d.constraint.$kind")
            }
        }
        PropSection(stringResource(R.string.pc3d_constraint_active))
        val groups = listOf(R.string.s3d_position, R.string.s3d_rotation, R.string.s3d_scale)
        var any = false
        groups.forEachIndexed { group, label ->
            if (PcSceneOps.constrained(store, group)) {
                any = true
                Row(Modifier.fillMaxWidth().height(30.dp).clip(RoundedCornerShape(7.dp)).background(AureaColors.EditorGlyphBox).padding(start = 10.dp, end = 4.dp),
                    verticalAlignment = Alignment.CenterVertically) {
                    CupertinoIcon(CupertinoGlyph.Link, 12.dp, AureaColors.Accent)
                    Spacer(Modifier.width(7.dp))
                    Text(stringResource(label), color = AureaColors.Text, fontSize = 12.sp, modifier = Modifier.weight(1f))
                    Text(stringResource(R.string.pc3d_constraint_remove), color = AureaColors.Accent, fontSize = 12.sp,
                        modifier = Modifier.clip(RoundedCornerShape(6.dp)).tocavel(shrink = 1f) { PcSceneOps.clearConstraint(store, group) }.padding(horizontal = 8.dp, vertical = 5.dp))
                }
            }
        }
        if (!any) Text(stringResource(R.string.pc3d_constraint_none), color = AureaColors.Subtle, fontSize = 11.5.sp)
    }
}

/** Aba Objeto: transformação, relações (pai), visibilidade e sombras. */
@Composable
private fun ObjectTab(store: EditorStore, row: LayerRow) {
    val d = store.detail ?: return
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(10.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        PropSection(stringResource(R.string.pc3d_section_transform))
        TransformFields(store, d.id)
        PropSection(stringResource(R.string.pc3d_section_relations))
        var parents by remember { mutableStateOf(false) }
        val parentName = store.layers.getOrNull(row.parentIndex)?.name
        Box {
            PropLine(stringResource(R.string.pc3d_parent), parentName ?: stringResource(R.string.pc3d_parent_none)) { parents = !parents }
            AnchoredMenu(parents, { parents = false }, entries = buildList {
                add(MenuEntry.Item(stringResource(R.string.pc3d_parent_none), checked = parentName == null) { store.setParentMany(listOf(row.id), 0L) })
                store.parentCandidates(row.id).filter { it.isThreeD }.take(40).forEach { c ->
                    add(MenuEntry.Item(c.name, glyph = LayerType.of(c.kind).glyph, checked = c.id == d.parentId) { store.setParentMany(listOf(row.id), c.id) })
                }
            }, tone = AureaSurfaceTone.Editor, width = 250.dp, tag = "pc3d.parent.list")
        }
        PropSection(stringResource(R.string.pc3d_section_visibility))
        PropToggle(stringResource(R.string.pc3d_visible), row.visible) { store.setLayerVisible(row.id, !row.visible) }
        PropToggle(stringResource(R.string.pc3d_locked), row.locked) { store.setLayerLocked(row.id, !row.locked) }
        // Projeta / recebe sombra: a seção do próprio painel 3D do Aurea (só aparece em quem tem malha).
        ObjectShadowSection(store)
    }
}

@Composable
private fun PropSection(title: String) =
    Text(title.uppercase(), color = AureaColors.Subtle, fontSize = 10.5.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.padding(top = 4.dp))

@Composable
private fun PropLine(label: String, value: String, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().height(30.dp).clip(RoundedCornerShape(7.dp)).background(AureaColors.EditorGlyphBox).tocavel(shrink = 1f, onClick = onClick)
        .padding(horizontal = 10.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, color = AureaColors.Muted, fontSize = 12.sp, modifier = Modifier.weight(1f), maxLines = 1)
        Text(value, color = AureaColors.Text, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        Spacer(Modifier.width(6.dp))
        CupertinoIcon(CupertinoGlyph.ChevronDown, 9.dp, AureaColors.Muted)
    }
}

@Composable
private fun PropToggle(label: String, on: Boolean, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().height(30.dp).clip(RoundedCornerShape(7.dp)).background(AureaColors.EditorGlyphBox).tocavel(shrink = 1f, onClick = onClick)
        .padding(horizontal = 10.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, color = AureaColors.Muted, fontSize = 12.sp, modifier = Modifier.weight(1f), maxLines = 1)
        CupertinoIcon(if (on) CupertinoGlyph.CheckmarkSquareFill else CupertinoGlyph.Square, 15.dp, if (on) AureaColors.Accent else AureaColors.Muted)
    }
}

/** Posição, Rotação e Escala XYZ com campos de arrastar/digitar (um passo de desfazer por arrasto). */
@Composable
private fun TransformFields(store: EditorStore, id: Long, compact: Boolean = false) {
    val d = store.detail?.takeIf { it.id == id } ?: return
    val t = TrackProperty
    val groups = listOf(
        Triple(R.string.s3d_position, t.POSITION_X, d.position),
        Triple(R.string.s3d_rotation, t.ROTATION_X, d.rotation),
        Triple(R.string.s3d_scale, t.SCALE_X, d.scale),
    )
    val axisColors = listOf(Color(0xFFFF5A5A), Color(0xFF5AD27A), Color(0xFF5AA8FF))
    Column(verticalArrangement = Arrangement.spacedBy(if (compact) 6.dp else 8.dp)) {
        groups.forEach { (label, base, values) ->
            val scale = base == t.SCALE_X
            val keyed = (0..2).any { d.hasKeyAtPlayhead(base + it) }
            val animated = (0..2).any { d.isAnimated(base + it) }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(label), color = AureaColors.Muted, fontSize = 11.sp, modifier = Modifier.weight(1f))
                // ◆ da linha: chave do grupo no cabeçote (a mesma função da timeline).
                Box(Modifier.size(20.dp).tocavel(shrink = 1f) { store.toggleTransformKeyframe(intArrayOf(base, base + 1, base + 2), id) }, contentAlignment = Alignment.Center) {
                    Text(if (keyed) "◆" else "◇", color = if (keyed) AureaColors.Keyframe else if (animated) AureaColors.KeyframeDim else AureaColors.Muted, fontSize = 11.sp)
                }
            }
            Column(verticalArrangement = Arrangement.spacedBy(1.dp)) {
                for (k in 0..2) {
                    SceneNumberRow("XYZ"[k].toString(), axisColors[k], values.getOrElse(k) { 0f },
                        unit = if (base == t.ROTATION_X) "°" else "", decimals = if (scale) 3 else 1, speed = if (scale) 0.005f else if (base == t.ROTATION_X) 0.5f else 1f,
                        identity = "$id.$base.$k", onBegin = { store.beginGesture("transformar") }, onEnd = { store.endGesture() }) { v ->
                        store.setTransform(base + k, v, id)
                    }
                }
            }
        }
    }
}

/**
 * Campo numérico no jeito do Blender, com as cores do Aurea: arrastar na
 * horizontal muda o valor (Shift = fino, Ctrl = passos inteiros), duplo clique
 * digita, Enter grava.
 */
@Composable
private fun SceneNumberRow(label: String, tint: Color, value: Float, unit: String, decimals: Int, speed: Float, identity: String,
                           onBegin: () -> Unit, onEnd: () -> Unit, onChange: (Float) -> Unit) {
    key(identity) {
        var editing by remember { mutableStateOf(false) }
        var acc by remember { mutableFloatStateOf(0f) }
        val current by rememberUpdatedState(value)
        val change by rememberUpdatedState(onChange)
        val shown = String.format(Locale.ROOT, "%.${decimals}f", value) + unit
        KeepLtr {
            Row(Modifier.fillMaxWidth().height(26.dp).clip(RoundedCornerShape(6.dp)).background(AureaColors.EditorGlyphBox)
                .pointerInput(Unit) { detectTapGestures(onDoubleTap = { editing = true }) }
                .pointerInput(Unit) {
                    detectHorizontalDragGestures(onDragStart = { acc = current; onBegin() }, onDragEnd = { onEnd() }, onDragCancel = { onEnd() }) { ch, dx ->
                        ch.consume()
                        val fine = if (com.aurea.pc.PcKeys.shift) 0.1f else 1f
                        acc += dx * speed * fine
                        change(if (com.aurea.pc.PcKeys.ctrl) Math.round(acc).toFloat() else acc)
                    }
                }.semantics { contentDescription = "$label $shown" }.padding(horizontal = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(label, color = tint, fontSize = 11.5.sp, fontWeight = FontWeight.SemiBold)
                Spacer(Modifier.weight(1f))
                if (editing) {
                    val text = String.format(Locale.ROOT, "%.${decimals}f", value)
                    var field by remember { mutableStateOf(TextFieldValue(text, TextRange(0, text.length))) }
                    val focus = remember { FocusRequester() }
                    var focused by remember { mutableStateOf(false) }
                    val commit = { field.text.replace(',', '.').toFloatOrNull()?.takeIf { it.isFinite() }?.let { v -> onBegin(); change(v); onEnd() }; editing = false }
                    LaunchedEffect(Unit) { focus.requestFocus() }
                    DisposableEffect(Unit) {
                        val cancel = { editing = false }
                        PcScene3D.typingCancel = cancel
                        onDispose { if (PcScene3D.typingCancel === cancel) PcScene3D.typingCancel = null }
                    }
                    BasicTextField(field, { field = it }, singleLine = true,
                        textStyle = TextStyle(color = AureaColors.Text, fontSize = 12.sp, textAlign = TextAlign.End), cursorBrush = SolidColor(AureaColors.Accent),
                        keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done), keyboardActions = KeyboardActions(onDone = { commit() }),
                        modifier = Modifier.width(110.dp).focusRequester(focus).onFocusChanged { if (focused && !it.isFocused) commit(); focused = it.isFocused })
                } else {
                    Text(shown, color = AureaColors.Text, fontSize = 12.sp, maxLines = 1)
                }
            }
        }
    }
}

// =============================================================================
// Lateral N: Item (posição, rotação, escala e dimensões)
// =============================================================================

@Composable
internal fun PcSceneSidebar(store: EditorStore, modifier: Modifier) {
    val shape = RoundedCornerShape(10.dp)
    Column(modifier.width(222.dp).clip(shape).background(AureaColors.EditorPanel.copy(alpha = 0.96f)).border(1.dp, AureaColors.Hairline, shape)
        // Opaco ao mouse: nada passa para a cena por baixo.
        .pointerInput(Unit) { detectTapGestures { } }.padding(10.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.pc3d_sidebar_item), color = AureaColors.Text, fontSize = 12.5.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
            Box(Modifier.size(22.dp).tocavel(shrink = 1f) { PcScene3D.sidebar = false }, contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.Xmark, 11.dp, AureaColors.Muted)
            }
        }
        val d = store.detail?.takeIf { it.id == store.primary }
        if (d == null) {
            Text(stringResource(R.string.s3d_nothing_selected), color = AureaColors.Subtle, fontSize = 11.5.sp)
            return@Column
        }
        Text(store.layers.firstOrNull { it.id == d.id }?.name.orEmpty(), color = AureaColors.Muted, fontSize = 11.5.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
        TransformFields(store, d.id, compact = true)
        // Dimensões: a caixa do objeto nos eixos dele (Engine::query_layer_bounds); motor antigo: fonte 2D × escala.
        Text(stringResource(R.string.pc3d_dimensions), color = AureaColors.Muted, fontSize = 11.sp)
        store.sceneViewTick
        val box = PcSceneOps.bounds(store, d.id)
        val dims = if (box != null) String.format(Locale.ROOT, "X %.1f   Y %.1f   Z %.1f px", box[6], box[7], box[8])
        else if (d.sourceWidth > 0 && d.sourceHeight > 0)
            String.format(Locale.ROOT, "%.0f × %.0f px", d.sourceWidth * kotlin.math.abs(d.scale.getOrElse(0) { 1f }), d.sourceHeight * kotlin.math.abs(d.scale.getOrElse(1) { 1f }))
        else stringResource(R.string.pc3d_dimensions_unknown)
        Text(dims, color = AureaColors.Text, fontSize = 12.sp)
    }
}
