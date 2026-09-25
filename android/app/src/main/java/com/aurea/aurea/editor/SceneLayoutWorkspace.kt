package com.aurea.aurea.editor

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.background
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
import kotlinx.coroutines.delay
import androidx.compose.foundation.horizontalScroll
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
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import com.aurea.aurea.R
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.aurea.aurea.state.EditorStore

/**
 * Cena 3D "seca": a cena ocupa a tela e se mexe com o dedo (ver
 * `sceneGesture` no Stage.kt) — arrastar o objeto move, 1 dedo no vazio gira
 * a vista, pinça aproxima, toque duplo recentra. Sem sliders nem campos XYZ:
 * só o que a pessoa procura — voltar, desfazer, adicionar, trocar de objeto
 * e, conforme o escolhido, material ou luz. Layout estático desloca a
 * animação existente; a órbita é só da prévia.
 */
@Composable
internal fun SceneLayoutWorkspace(store: EditorStore, ui: EditorUi, stage: @Composable (Modifier) -> Unit) {
    var materials by remember { mutableStateOf(false) }
    var lights by remember { mutableStateOf(false) }
    var addMenu by remember { mutableStateOf(false) }
    var materialPanel by remember { mutableStateOf(EditorPanel.Element3D) }
    var hint by remember { mutableStateOf(true) }
    LaunchedEffect(Unit) { delay(7000); hint = false }
    if (ui.adding) Dialog(onDismissRequest = { ui.adding = false }) {
        Surface { Box(Modifier.fillMaxWidth().fillMaxHeight(.8f)) { AddLayerPanel(store, ui) } }
    }
    if (materials) Dialog(onDismissRequest = { materials = false }) {
        Surface { PanelContent(store = store, panel = materialPanel, onClose = { materials = false }, onOpenPanel = { materialPanel = it }, onOpenEffectsBrowser = { ui.effectsBrowser = true }, modifier = Modifier.fillMaxWidth().fillMaxHeight(.85f)) }
    }
    if (lights) Dialog(onDismissRequest = { lights = false }) {
        Surface { Column(Modifier.fillMaxWidth().padding(12.dp)) {
            store.lightInfo()?.let { values ->
                for (param in 1..5) {
                    if (param == 5 && values[0] == 0f) continue
                    val label = listOf("", stringResource(R.string.panel_intensidade), "R", "G", "B", stringResource(R.string.scene_light_range))[param]
                    Row {
                        SceneNumberField(values[param], label, "light:${store.primary}:$param", { store.setLightParam(param, it) }, Modifier.weight(1f))
                        if (param < 5) TextButton(onClick = { store.toggleLightKey(param, values[param]) }) { Text(if (store.detail?.hasKeyAtPlayhead(20 + param) == true) "◆" else "◇") }
                    }
                }
                if (values[0] == 0f) Row { Text(stringResource(R.string.scene_light_shadows)); Switch(values[8] >= .5f, { store.setLightParam(8, if (it) 1f else 0f) }) }
            }
        } }
    }
    val selected = store.layers.firstOrNull { it.id == store.primary }
    Box(Modifier.fillMaxSize().background(AureaColors.EditorPanel)) {
        stage(Modifier.fillMaxSize())
        Row(Modifier.align(Alignment.TopCenter).fillMaxWidth().padding(horizontal = 8.dp, vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically) {
            SceneRoundButton(CupertinoGlyph.ChevronLeft, stringResource(R.string.editor_voltar_editor), store::exitSceneEditor)
            Spacer(Modifier.width(8.dp))
            Text(stringResource(R.string.scene_workspace), color = AureaColors.Text, fontSize = 15.sp, fontWeight = FontWeight.W600)
            Spacer(Modifier.weight(1f))
            SceneRoundButton(CupertinoGlyph.ArrowUturnLeft, stringResource(R.string.editor_desfazer), store::undo)
            Spacer(Modifier.width(6.dp))
            SceneRoundButton(CupertinoGlyph.ArrowUturnRight, stringResource(R.string.editor_refazer), store::redo)
        }
        Column(Modifier.align(Alignment.BottomCenter).fillMaxWidth().padding(bottom = 12.dp),
            horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
            AnimatedVisibility(hint, enter = fadeIn(), exit = fadeOut()) {
                Text(stringResource(R.string.scene_hint), color = AureaColors.Text, fontSize = 12.sp, textAlign = TextAlign.Center,
                    modifier = Modifier.padding(horizontal = 24.dp).background(ShellColors.FloatingDark, RoundedCornerShape(12.dp))
                        .padding(horizontal = 12.dp, vertical = 8.dp))
            }
            val objects = store.layers.filter { it.isThreeD }
            if (objects.isNotEmpty()) Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 12.dp),
                horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                objects.forEach { row ->
                    val on = store.primary == row.id
                    Row(Modifier.clip(RoundedCornerShape(16.dp))
                        .background(if (on) AureaColors.Accent.copy(alpha = .9f) else ShellColors.FloatingDark)
                        .tocavel(haptic = true) { store.select(row.id) }
                        .padding(horizontal = 12.dp, vertical = 7.dp), verticalAlignment = Alignment.CenterVertically) {
                        CupertinoIcon(LayerType.of(row.kind).glyph, 13.dp, if (on) Color.Black else AureaColors.Text)
                        Spacer(Modifier.width(6.dp))
                        Text(row.name, color = if (on) Color.Black else AureaColors.Text, fontSize = 13.sp, maxLines = 1)
                    }
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                Box {
                    ScenePillButton(CupertinoGlyph.Plus, stringResource(R.string.panel_adicionar).removePrefix("+ ")) { addMenu = true }
                    if (addMenu) ShellPopupMenu(listOf(
                        PopupItem(stringResource(R.string.sh_add_text_3d), false) { addMenu = false; store.addText3D() },
                        PopupItem(stringResource(R.string.panel_camera_3d), false) { addMenu = false; store.addCamera() },
                        PopupItem(stringResource(R.string.sh_add_null_3d), false) { addMenu = false; store.addNull(true) },
                        PopupItem(stringResource(R.string.scene_light_directional), false) { addMenu = false; store.addLight(0) },
                        PopupItem(stringResource(R.string.scene_light_point), false) { addMenu = false; store.addLight(1) },
                        PopupItem(stringResource(R.string.sh_add_model_3d) + "…", false) { addMenu = false; ui.adding = true },
                    ), onDismiss = { addMenu = false }, width = 220.dp)
                }
                if (selected != null && (selected.kind == LayerType.Model3D.kind || (selected.kind == LayerType.Text.kind && selected.isThreeD))) {
                    ScenePillButton(CupertinoGlyph.CubeFill, stringResource(R.string.scene_material)) { materialPanel = EditorPanel.Element3D; materials = true }
                }
                if (selected?.kind == LayerType.Light.kind) {
                    ScenePillButton(CupertinoGlyph.Lightbulb, stringResource(R.string.pn_t3d_lighting)) { lights = true }
                }
                ScenePillButton(CupertinoGlyph.ArrowCounterclockwise, stringResource(R.string.scene_recenter)) { store.resetSceneView() }
            }
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
