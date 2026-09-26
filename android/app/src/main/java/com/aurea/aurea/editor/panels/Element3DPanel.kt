package com.aurea.aurea.editor.panels

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.material3.Slider
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.TextButton
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.contentDescription
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.Text3DInfo
import com.aurea.aurea.state.Text3DPreset
import com.aurea.aurea.ui.ds.ColorWell
import com.aurea.aurea.ui.ds.PropertyCustomRow
import com.aurea.aurea.ui.ds.TickRuler
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

/**
 * MATERIAL E AMBIENTE do objeto 3D. Material: só o que o motor muda de verdade
 * — a cor do texto 3D (a malha é gerada de novo); o modelo importado usa o
 * material do próprio arquivo (a cena do motor ainda não aceita troca de
 * material: `SceneSetMaterialParam` responde "não implementado"), então aqui
 * não há régua de Metálico/Rugosidade que não faria nada. Ambiente: a luz que
 * envolve a cena (HDRI ou estúdio neutro), intensidade e giro.
 */
@Composable
internal fun Element3DPanel(env: PanelEnv) {
    val store = env.store
    val e by remember(store) { derivedStateOf { store.environment } }
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) store.importHdri(uri)
    }
    val t3 by remember(store) { derivedStateOf { store.text3d } }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(start = 18.dp, top = 12.dp, end = 18.dp, bottom = 24.dp)) {
        t3?.let { Text3DSection(env, it) }
        SectionTitle(stringResource(R.string.panel_material))
        val info = t3
        if (info != null) {
            Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.panel_cor), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
                ColorWell(Color(info.color[0], info.color[1], info.color[2])) {
                    store.beginGesture("cor do texto 3D")
                    env.openColor(ColorRequest(info.color.copyOf(), onChange = { r, g, b, _ ->
                        store.text3d?.let { store.setText3D(it.copy(color = floatArrayOf(r, g, b, 1f)), lazy = true) }
                    }, onDone = { store.endGesture() }))
                }
            }
            Text3DPbrSection(env, store)
        } else ImportedMaterialSection(store)
        LightingSection(store)
        Spacer(Modifier.height(16.dp))
        SectionTitle(stringResource(R.string.panel_luz_ambiente))
        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Chip(stringResource(R.string.panel_estudio_neutro), on = e[0] < 0.5f) { store.clearHdri() }
            Chip(if (e[0] >= 0.5f) stringResource(R.string.panel_imagem_ambiente) else stringResource(R.string.panel_usar_imagem_ambiente_hdr), on = e[0] >= 0.5f) {
                pick.launch(arrayOf("image/vnd.radiance", "application/octet-stream", "*/*"))
            }
        }
        Spacer(Modifier.height(10.dp))
        EnvRuler(store, 1, stringResource(R.string.panel_intensidade), 0.01f, 0f, 20f, e[1], "${(e[1] * 100).roundToInt()}%") { store.setEnvironment(it, store.environment[2]) }
        EnvRuler(store, 2, stringResource(R.string.pn_env_rotate_light), 1f, -360f, 360f, e[2], "${e[2].roundToInt()}°") { store.setEnvironment(store.environment[1], it) }
        Spacer(Modifier.height(8.dp))
        Text(
            stringResource(R.string.pn_env_light_hint),
            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, lineHeight = 16.sp, color = AureaColors.Muted)),
        )
        ObjectEnvironmentSection(store)
    }
}

@Composable
private fun ImportedMaterialSection(store: EditorStore) {
    // Reading detail/curve revision subscribes to playback, edits and undo.
    val detail = store.detail
    val revision = store.curveRevision
    val materials = remember(detail, revision) { store.queryMaterials() }
    var selected by remember(store.primary) { mutableStateOf(0) }
    var dragging by remember(store.primary) { mutableStateOf(false) }
    if (materials.isEmpty()) return
    val current = materials.firstOrNull { it[0].toInt() == selected } ?: materials.first()
    val index = current[0].toInt()
    Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        materials.forEach { material ->
            val id = material[0].toInt()
            Chip("Material ${id + 1}", on = id == index) { selected = id }
        }
    }
    val labels = listOf("R", "G", "B", "Alpha", stringResource(R.string.pn_t3d_metallic), stringResource(R.string.pn_t3d_roughness))
    labels.forEachIndexed { param, label ->
        val value = current[param + 2].coerceIn(0f, 1f)
        val here = (store.keyframes[store.primary] ?: emptyList()).any {
            it.property == 37 && it.effectIndex == index && it.paramIndex == param && it.time == detail?.localPlayhead
        }
        val keyLabel = stringResource(if (here) R.string.panel_tirar_keyframe_daqui else R.string.panel_marcar_keyframe_aqui) + " · " + label
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Text(label, modifier = Modifier.width(78.dp), style = AureaType.BodySmall)
            Slider(value = value, onValueChange = {
                if (!dragging) { dragging = true; store.beginGesture("material") }
                store.setMaterialParameter(index, param, it)
            }, onValueChangeFinished = { if (dragging) { dragging = false; store.endGesture() } }, modifier = Modifier.weight(1f))
            TextButton(onClick = { store.toggleMaterialKeyframe(index, param, value) }, modifier = Modifier.semantics { contentDescription = keyLabel }) { Text(if (here) "◆" else "◇") }
        }
    }
    androidx.compose.runtime.DisposableEffect(store.primary) {
        onDispose { if (dragging) { dragging = false; store.endGesture() } }
    }
}

/**
 * AMBIENTE DO OBJETO (v22): este modelo pode ter a luz dele, sem mexer na dos
 * outros. Sem ambiente próprio, vale o do projeto — o de cima.
 */
@Composable
private fun ObjectEnvironmentSection(store: EditorStore) {
    val oe by remember(store) { derivedStateOf { store.objectEnvironment } }
    var pick by remember { mutableStateOf(false) }
    val escolher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) store.importObjectHdri(uri)
    }
    if (oe == null) return
    val proprio = oe!![0] >= 0.5f
    Spacer(Modifier.height(16.dp))
    SectionTitle(stringResource(R.string.panel_ambiente_do_objeto))
    Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        Chip(stringResource(R.string.panel_do_projeto), on = !proprio) { store.setObjectEnvironment(0) }
        Chip(stringResource(R.string.panel_proprio), on = proprio) {
            store.setObjectEnvironment(1)
            if (oe!![1] <= 0f) escolher.launch(arrayOf("image/vnd.radiance", "application/octet-stream", "*/*"))
        }
    }
    if (proprio) {
        Spacer(Modifier.height(10.dp))
        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Chip(
                if (oe!![1] > 0f) stringResource(R.string.panel_trocar_imagem) else stringResource(R.string.panel_usar_imagem_ambiente_hdr),
                on = oe!![1] > 0f,
            ) { escolher.launch(arrayOf("image/vnd.radiance", "application/octet-stream", "*/*")) }
        }
        Spacer(Modifier.height(10.dp))
        EnvRuler(store, 2, stringResource(R.string.panel_intensidade), 0.01f, 0f, 20f, oe!![2], "${(oe!![2] * 100).roundToInt()}%") {
            store.setObjectEnvironment(1, intensity = it)
        }
        EnvRuler(store, 3, stringResource(R.string.pn_env_rotate_light), 1f, -360f, 360f, oe!![3], "${oe!![3].roundToInt()}°") {
            store.setObjectEnvironment(1, rotation = it)
        }
        EnvRuler(store, 4, stringResource(R.string.panel_exposicao), 0.01f, 0.05f, 20f, oe!![4], "${(oe!![4] * 100).roundToInt()}%") {
            store.setObjectEnvironment(1, exposure = it)
        }
    }
}

@Composable
private fun SectionTitle(t: String) {
    Text(t, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
    Spacer(Modifier.height(6.dp))
}

/** Texto 3D: texto, profundidade e alinhamento (a cor mora em Material) (a malha é gerada de novo a cada mudança). */
@Composable
private fun Text3DSection(env: PanelEnv, info: Text3DInfo) {
    val store = env.store
    var draft by remember(store.primary) { mutableStateOf(info.content) }
    LaunchedEffect(info.content) { if (info.content != draft && !store.textEditing) draft = info.content }
    SectionTitle(stringResource(R.string.pn_text3d_title))
    TextButton(onClick = {
        val type = effectTypeId("aurea.text3d.layout")
        if (store.effects.none { it.typeId == type }) store.addEffect(type)
        env.onOpenPanel(EditorPanel.Effects)
    }) { Text("Letter rotation · Cylinder · Twist") }
    var fontsOpen by remember { mutableStateOf(false) }
    val importFont = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) store.importFont(uri, forText3d = true)
    }
    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        Chip(stringResource(R.string.t3d_font), on = false) { store.loadFonts(); fontsOpen = true }
        Chip(stringResource(R.string.t3d_import_font), on = false) { importFont.launch(arrayOf("font/*", "application/octet-stream", "*/*")) }
    }
    if (fontsOpen) AlertDialog(
        onDismissRequest = { fontsOpen = false },
        title = { Text(stringResource(R.string.t3d_font)) },
        text = {
            LazyColumn(Modifier.heightIn(max = 400.dp)) {
                item { TextButton(onClick = { store.text3d?.let { store.setText3D(it.copy(fontPath = "")) }; fontsOpen = false }) { Text(stringResource(R.string.t3d_default_font)) } }
                items(store.fonts, key = { it.path }) { font ->
                    TextButton(onClick = { store.text3d?.let { store.setText3D(it.copy(fontPath = font.path)) }; fontsOpen = false }) {
                        Text("${font.family} ${font.style}", color = if (info.fontPath == font.path) AureaColors.Accent else AureaColors.Text)
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = { fontsOpen = false }) { Text(stringResource(R.string.t3d_close)) } },
    )
    Spacer(Modifier.height(12.dp))
    Box(
        Modifier.fillMaxWidth().heightIn(min = 52.dp).clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip)
            .padding(horizontal = 12.dp, vertical = 10.dp),
    ) {
        BasicTextField(
            value = draft,
            onValueChange = {
                draft = it
                store.text3d?.let { cur -> store.setText3D(cur.copy(content = it), typing = true) }
            },
            textStyle = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Text)),
            cursorBrush = SolidColor(AureaColors.Accent),
            modifier = Modifier.fillMaxWidth(),
        )
        if (draft.isEmpty()) Text(stringResource(R.string.pn_text3d_placeholder), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Muted)))
    }
    Spacer(Modifier.height(6.dp))
    PropertyCustomRow(stringResource(R.string.pn_depth), selected = false, onSelect = {}) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f).height(40.dp)) {
                TickRuler(
                    value = { store.text3d?.depth ?: 0f },
                    unitsPerDp = 0.005f,
                    active = true,
                    modifier = Modifier.fillMaxSize().valueDrag(
                        enabled = true,
                        start = { store.text3d?.depth ?: 0f },
                        unitsPerDp = { 0.005f },
                        min = 0f,
                        max = 3f,
                        onStart = { store.beginGesture("profundidade do texto 3D") },
                        onValue = { v -> store.text3d?.let { store.setText3D(it.copy(depth = v), lazy = true) } },
                        onEnd = { store.endGesture() },
                    ),
                )
            }
            Spacer(Modifier.width(8.dp))
            ValueBox("${(info.depth * 100).roundToInt()}%", onTap = null)
        }
    }
    Row(Modifier.fillMaxWidth().height(48.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.panel_alinhamento), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf(0 to stringResource(R.string.panel_esquerda), 1 to stringResource(R.string.panel_centro), 2 to stringResource(R.string.panel_direita)).forEach { (a, label) ->
                Chip(label, on = info.alignment == a) { store.text3d?.let { store.setText3D(it.copy(alignment = a)) } }
            }
        }
    }
    Spacer(Modifier.height(14.dp))
    SectionTitle(stringResource(R.string.pn_t3d_geometry))
    // O chanfro e GEOMETRIA: ligado, a malha ganha frente/fundo recuados e o
    // anel de chanfro (mais triângulos, não um brilho no shader).
    Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.pn_t3d_bevel), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Chip(stringResource(R.string.pn_t3d_off), on = !info.bevel) { store.text3d?.let { store.setText3D(it.copy(bevel = false)) } }
            Chip(stringResource(R.string.pn_t3d_on), on = info.bevel) { store.text3d?.let { store.setText3D(it.copy(bevel = true)) } }
        }
    }
    if (info.bevel) {
        T3DRuler(store, stringResource(R.string.pn_t3d_bevel_width), 0.0002f, 0f, 0.2f, info.bevelWidth,
            "${(info.bevelWidth * 1000).roundToInt()}", "chanfro") { v -> store.setText3D(info.copy(bevelWidth = v), lazy = true) }
        T3DRuler(store, stringResource(R.string.pn_t3d_bevel_depth), 0.0002f, 0f, 0.2f, info.bevelDepth,
            "${(info.bevelDepth * 1000).roundToInt()}", "chanfro") { v -> store.setText3D(info.copy(bevelDepth = v), lazy = true) }
        Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.pn_t3d_bevel_segments), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                listOf(1, 2, 3, 5, 8).forEach { n ->
                    Chip("$n", on = info.bevelSegments == n) { store.text3d?.let { store.setText3D(it.copy(bevelSegments = n)) } }
                }
            }
        }
        T3DRuler(store, stringResource(R.string.pn_t3d_bevel_roundness), 0.006f, 0f, 1f, info.bevelRoundness,
            "${(info.bevelRoundness * 100).roundToInt()}%", "arredondamento") { v -> store.setText3D(info.copy(bevelRoundness = v), lazy = true) }
    }
    Spacer(Modifier.height(14.dp))
    SectionTitle(stringResource(R.string.pn_t3d_presets))
    Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        listOf("Smooth", "Brushed", "Scratched", "Hammered", "Weathered Metal").forEachIndexed { index, label ->
            Chip(label, on = info.surfaceFinish == index) {
                store.text3d?.let { store.setText3D(it.copy(surfaceFinish = index)) }
            }
        }
    }
    Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        (listOf(Text3DPreset.CinematicMetal) + Text3DPreset.values().filter { it != Text3DPreset.CinematicMetal }).forEach { preset ->
            Chip(stringResource(preset.labelRes), on = false) {
                store.applyText3DPreset(preset)
            }
        }
    }
    Spacer(Modifier.height(14.dp))
}

/**
 * MATERIAL do texto 3D: o MESMO PBR dos modelos importados (metal, rugosidade,
 * especular, emissão). Nada de caminho especial de texto.
 */
@Composable
private fun Text3DPbrSection(env: PanelEnv, store: EditorStore) {
    val info by remember(store) { derivedStateOf { store.text3d } }
    val cur = info ?: return
    T3DRuler(store, stringResource(R.string.pn_t3d_metallic), 0.005f, 0f, 1f, cur.metallic,
        "${(cur.metallic * 100).roundToInt()}%", "metalico") { v -> store.setText3D(cur.copy(metallic = v), lazy = true) }
    T3DRuler(store, stringResource(R.string.pn_t3d_roughness), 0.005f, 0f, 1f, cur.roughness,
        "${(cur.roughness * 100).roundToInt()}%", "rugosidade") { v -> store.setText3D(cur.copy(roughness = v), lazy = true) }
    T3DRuler(store, stringResource(R.string.pn_t3d_specular), 0.005f, 0f, 1f, cur.specular,
        "${(cur.specular * 100).roundToInt()}%", "especular") { v -> store.setText3D(cur.copy(specular = v), lazy = true) }
    Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.pn_t3d_emissive), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        ColorWell(Color(cur.emissive[0], cur.emissive[1], cur.emissive[2])) {
            store.beginGesture("emissao do texto 3D")
            env.openColor(ColorRequest(cur.emissive.copyOf(), onChange = { r, g, b, _ ->
                store.text3d?.let { store.setText3D(it.copy(emissive = floatArrayOf(r, g, b)), lazy = true) }
            }, onDone = { store.endGesture() }))
        }
    }
    T3DRuler(store, stringResource(R.string.pn_t3d_emissive_strength), 0.02f, 0f, 8f, cur.emissiveStrength,
        "${(cur.emissiveStrength * 100).roundToInt()}%", "forca da emissao") { v -> store.setText3D(cur.copy(emissiveStrength = v), lazy = true) }
    Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.pn_t3d_regions), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Chip(stringResource(R.string.pn_t3d_off), on = !cur.regionMaterials) { store.text3d?.let { store.setText3D(it.copy(regionMaterials = false)) } }
            Chip(stringResource(R.string.pn_t3d_on), on = cur.regionMaterials) { store.text3d?.let { store.setText3D(it.copy(regionMaterials = true)) } }
        }
    }
    if (cur.regionMaterials) {
        // Frente = a cor de Material (acima). Aqui vão a lateral e o chanfro.
        T3DRuler(store, stringResource(R.string.pn_t3d_region_side), 0.005f, 0f, 1f, cur.sideRoughness,
            "${(cur.sideRoughness * 100).roundToInt()}%", "rugosidade da lateral") { v ->
            store.setText3D(cur.copy(sideRoughness = v), lazy = true)
        }
        Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(stringResource(R.string.pn_t3d_region_bevel), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            ColorWell(Color(cur.bevelColor[0], cur.bevelColor[1], cur.bevelColor[2])) {
                store.beginGesture("cor do chanfro")
                env.openColor(ColorRequest(cur.bevelColor.copyOf(), onChange = { r, g, b, _ ->
                    store.text3d?.let { store.setText3D(it.copy(bevelColor = floatArrayOf(r, g, b, 1f)), lazy = true) }
                }, onDone = { store.endGesture() }))
            }
        }
        T3DRuler(store, stringResource(R.string.pn_t3d_metallic) + " · " + stringResource(R.string.pn_t3d_region_bevel), 0.005f, 0f, 1f, cur.bevelMetallic,
            "${(cur.bevelMetallic * 100).roundToInt()}%", "metalico do chanfro") { v ->
            store.setText3D(cur.copy(bevelMetallic = v), lazy = true)
        }
    }
}

/** ILUMINAÇÃO: o objeto projeta e recebe a sombra dos outros. */
@Composable
private fun LightingSection(store: EditorStore) {
    val sh by remember(store) { derivedStateOf { store.modelShadows } }
    val cur = sh ?: return
    Spacer(Modifier.height(16.dp))
    SectionTitle(stringResource(R.string.pn_t3d_lighting))
    Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.pn_t3d_cast_shadow), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Chip(stringResource(R.string.pn_t3d_off), on = !cur.first) { store.setModelShadows(false, cur.second) }
            Chip(stringResource(R.string.pn_t3d_on), on = cur.first) { store.setModelShadows(true, cur.second) }
        }
    }
    Row(Modifier.fillMaxWidth().height(44.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.pn_t3d_receive_shadow), modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            Chip(stringResource(R.string.pn_t3d_off), on = !cur.second) { store.setModelShadows(cur.first, false) }
            Chip(stringResource(R.string.pn_t3d_on), on = cur.second) { store.setModelShadows(cur.first, true) }
        }
    }
}

/** Uma régua de valor do texto 3D: arrasto com passo, rótulo e caixa. */
@Composable
private fun T3DRuler(
    store: EditorStore,
    label: String,
    unitsPerDp: Float,
    min: Float,
    max: Float,
    value: Float,
    shown: String,
    gesture: String,
    onValue: (Float) -> Unit,
) {
    PropertyCustomRow(label, selected = false, onSelect = {}) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f).height(40.dp)) {
                TickRuler(
                    value = { value },
                    unitsPerDp = unitsPerDp,
                    active = true,
                    modifier = Modifier.fillMaxSize().valueDrag(
                        enabled = true,
                        start = { value },
                        unitsPerDp = { unitsPerDp },
                        min = min,
                        max = max,
                        onStart = { store.beginGesture(gesture) },
                        onValue = onValue,
                        onEnd = { store.endGesture() },
                    ),
                )
            }
            Spacer(Modifier.width(8.dp))
            ValueBox(shown, onTap = null)
        }
    }
}

@Composable
private fun Chip(label: String, on: Boolean, onClick: () -> Unit) {
    Box(
        Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
            .tocavel(onClick = onClick).padding(horizontal = 12.dp, vertical = 6.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text)))
    }
}

@Composable
private fun EnvRuler(
    store: com.aurea.aurea.state.EditorStore,
    index: Int, // 1 = intensidade, 2 = girar a luz (posição em `environment`)
    label: String,
    unitsPerDp: Float,
    min: Float,
    max: Float,
    value: Float,
    text: String,
    onValue: (Float) -> Unit,
) {
    PropertyCustomRow(label, selected = false, onSelect = {}) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.weight(1f).height(40.dp)) {
                TickRuler(
                    value = { store.environment[index] },
                    unitsPerDp = unitsPerDp,
                    active = true,
                    modifier = Modifier.fillMaxSize().valueDrag(
                        enabled = true,
                        start = { store.environment[index] },
                        unitsPerDp = { unitsPerDp },
                        min = min,
                        max = max,
                        onStart = { store.beginGesture("ambiente") },
                        onValue = onValue,
                        onEnd = { store.endGesture() },
                    ),
                )
            }
            Spacer(Modifier.width(8.dp))
            ValueBox(text, onTap = null)
        }
    }
}
