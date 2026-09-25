package com.aurea.aurea.editor

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

/** Static layout edits offset existing animation; orbit is preview-only. */
@Composable
internal fun SceneLayoutWorkspace(store: EditorStore, ui: EditorUi, stage: @Composable (Modifier) -> Unit) {
    var materials by remember { mutableStateOf(false) }
    var lights by remember { mutableStateOf(false) }
    var materialPanel by remember { mutableStateOf(EditorPanel.Element3D) }
    if (ui.adding) Dialog(onDismissRequest = { ui.adding = false }) {
        Surface { Box(Modifier.fillMaxWidth().fillMaxHeight(.8f)) { AddLayerPanel(store, ui) } }
    }
    if (materials) Dialog(onDismissRequest = { materials = false }) {
        Surface { PanelContent(store = store, panel = materialPanel, onClose = { materials = false }, onOpenPanel = { materialPanel = it }, onOpenEffectsBrowser = { ui.effectsBrowser = true }, modifier = Modifier.fillMaxWidth().fillMaxHeight(.85f)) }
    }
    if (lights) Dialog(onDismissRequest = { lights = false }) {
        Surface { Column(Modifier.fillMaxWidth().padding(12.dp)) {
            Row { TextButton(onClick = { store.addLight(0) }) { Text(stringResource(R.string.scene_light_directional)) }; TextButton(onClick = { store.addLight(1) }) { Text(stringResource(R.string.scene_light_point)) } }
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
    Column(Modifier.fillMaxSize()) {
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState())) {
            TextButton(onClick = store::exitSceneEditor) { Text("← Timeline") }
            TextButton(onClick = store::undo) { Text("↶") }
            TextButton(onClick = store::redo) { Text("↷") }
            TextButton(onClick = { ui.adding = true }) { Text(stringResource(R.string.panel_adicionar)) }
            TextButton(onClick = { materialPanel = EditorPanel.Element3D; materials = true }, enabled = store.detail?.kind == 10) { Text("PBR / HDRI") }
            TextButton(onClick = { lights = true }) { Text(stringResource(R.string.pn_t3d_lighting)) }
            TextButton(onClick = store::addCamera) { Text(stringResource(R.string.panel_camera_3d)) }
            TextButton(onClick = { store.addNull(true) }) { Text(stringResource(R.string.sh_add_null_3d)) }
            TextButton(onClick = { store.addText3D() }) { Text(stringResource(R.string.sh_add_text_3d)) }
        }
        stage(Modifier.weight(1f).fillMaxWidth())
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState())) {
            store.layers.filter { it.isThreeD }.forEach { row ->
                TextButton(onClick = { store.select(row.id) }) { Text((if (store.primary == row.id) "● " else "") + row.name) }
            }
        }
        Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp)) {
            Column(Modifier.weight(1f)) { Text(stringResource(R.string.scene_orbit_x)); Slider(store.sceneYaw, { store.updateSceneView(it, store.scenePitch, store.sceneDistance) }, valueRange = -180f..180f) }
            Column(Modifier.weight(1f)) { Text(stringResource(R.string.scene_orbit_y)); Slider(store.scenePitch, { store.updateSceneView(store.sceneYaw, it, store.sceneDistance) }, valueRange = -80f..80f) }
            Column(Modifier.weight(1f)) { Text(stringResource(R.string.scene_zoom)); Slider(store.sceneDistance, { store.updateSceneView(store.sceneYaw, store.scenePitch, it) }, valueRange = .25f..10f) }
        }
        var group by remember { mutableIntStateOf(0) }
        Row { listOf(stringResource(R.string.fx_posicao), stringResource(R.string.panel_escala), stringResource(R.string.panel_rotacao)).forEachIndexed { i, label -> TextButton(onClick = { group = i }) { Text((if (group == i) "● " else "") + label) } } }
        store.detail?.let { detail ->
            val values = when (group) { 1 -> detail.scale; 2 -> detail.rotation; else -> detail.position }
            Row(Modifier.fillMaxWidth().padding(8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                for (axis in 0..2) {
                    SceneNumberField(values[axis], listOf("X", "Y", "Z")[axis], "transform:${store.primary}:$group:$axis",
                        { store.setTransform(group * 3 + axis, it) }, Modifier.weight(1f))
                }
            }
        }
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
