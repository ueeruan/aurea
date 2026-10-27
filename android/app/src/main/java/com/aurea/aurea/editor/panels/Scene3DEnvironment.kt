package com.aurea.aurea.editor.panels

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaActionSheet
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.SheetAction
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel

// =============================================================================
// 0.3.6 (env3d) AMBIENTE 3D REFEITO. Retorno do dono: o "Environment Settings"
// estava complicado e compacto, com coisas que não eram de ambiente. Agora:
//
//   Aba Ambiente  — só o ambiente, em seções com título e respiro (medidas de
//                   PanelMetrics): Céu e fundo (estúdio ou HDRI, fundo 360°),
//                   Luz ambiente (intensidade, giro, exposição), Chão e sombra.
//   Aba Render    — qualidade, tone map e bloom: valem para a cena inteira
//                   (estavam no "Avançado" do ambiente).
//   Aba Material  — seção Objeto: projeta/recebe sombra (era do ambiente) e o
//                   interior; e a TEXTURA de cada material (arquivo ou timeline).
//
// Nada mudou no motor para o ambiente: cada controle chama o mesmo comando de
// antes. Par do iOS: Panel3DView.swift (`Environment3DTab`, `Render3DTab`,
// `MaterialTextureRow`).
// =============================================================================

/** Estúdios da fonte de luz, na ordem dos ids do motor (StudioEnvironment.hpp: 0 neutro … 4 cromado). */
private val EnvStudios = listOf(
    R.string.scene031_light_neutral, R.string.scene_dark, R.string.scene_product, R.string.scene_sky, R.string.scene_chrome,
)

/** Operadores de tone map do 3D, na ordem do motor (0 PBR Neutral … 5 ACES). */
private val EnvToneMaps = listOf(
    R.string.scene031_tonemap_neutral, R.string.scene031_tonemap_agx, R.string.scene031_tonemap_agx_punchy,
    R.string.scene031_tonemap_agx_golden, R.string.scene031_tonemap_uchimura, R.string.scene031_tonemap_aces,
)

private val EnvQualities = listOf(R.string.scene_auto, R.string.scene_low, R.string.scene_medium, R.string.scene_high, R.string.scene_ultra)

private val EnvFloors = listOf(R.string.scene031_floor_off, R.string.scene031_floor_visible, R.string.scene031_floor_shadow)

/** Os ajustes da cena do motor (8 valores) e a escrita de um deles (um passo de desfazer). */
@Composable
private fun rememberSceneSettings(store: EditorStore): Pair<FloatArray, (Int, Float) -> Unit> {
    val revision = store.sceneSettingsRevision
    var settings by remember(revision) { mutableStateOf(store.sceneSettings()) }
    val change: (Int, Float) -> Unit = { index, value -> store.setSceneSetting(index, value); settings = store.sceneSettings() }
    return settings to change
}

/**
 * AMBIENTE: céu/fundo, luz ambiente e chão — nada de objeto nem de render.
 * LUZ: uma fileira de fontes exclusivas (estúdios e "Imagem HDR…"), só uma
 * acesa: a HDR quando há HDRI (ela vence o estúdio), senão o estúdio. Escolher
 * um estúdio tira o HDRI (a ponte faz numa mutação só: um toque = um passo).
 */
@Composable
internal fun Environment3DTab(env: PanelEnv) {
    val store = env.store
    val e by remember(store) { derivedStateOf { store.environment } }
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) { store.importHdri(uri); if (store.detail?.kind == 8) store.setEnvironmentBackground(true) }
    }
    val (settings, change) = rememberSceneSettings(store)
    val ready = settings.size >= 8
    val hdr = e[0] >= 0.5f
    val launchHdr = { pick.launch(arrayOf("image/vnd.radiance", "image/x-exr", "application/zip", "application/octet-stream", "*/*")) }

    EnvSection(stringResource(R.string.env3d_section_sky), stringResource(R.string.env3d_sky_hint), Modifier.testTag("env3d.sky")) {
        EnvChoices {
            if (ready) {
                EnvStudios.forEachIndexed { i, label ->
                    val on = !hdr && settings[0].toInt() == i
                    // Tocar no estúdio já aceso não gera passo de desfazer vazio.
                    EnvChoice(stringResource(label), on, "panel3d.light.$i") { if (!on) change(0, i.toFloat()) }
                }
            }
            // Acesa, toca de novo para trocar a imagem.
            EnvChoice(stringResource(R.string.scene031_light_hdr), hdr, "panel3d.light.hdr") { launchHdr() }
        }
        if (hdr) {
            EnvLine(stringResource(R.string.env3d_hdr_image)) {
                EnvButton(stringResource(R.string.shape3d_image_change), "env3d.hdr.change") { launchHdr() }
            }
        }
        EnvToggle(stringResource(R.string.environment_background), (e.getOrNull(3) ?: 0f) > .5f, "env3d.background") {
            store.setEnvironmentBackground(it)
        }
    }

    EnvSection(stringResource(R.string.env3d_section_light), null, Modifier.testTag("env3d.light")) {
        EnvValueRow(env, stringResource(R.string.panel_intensidade), e[1] * 100f, 1f, 0f, 2000f, "%", 100f, "ambiente") {
            store.setEnvironment(it / 100f, store.environment[2])
        }
        EnvValueRow(env, stringResource(R.string.pn_env_rotate_light), e[2], 1f, -360f, 360f, "°", 0f, "ambiente") {
            store.setEnvironment(store.environment[1], it)
        }
        // Exposição: multiplicador linear (1 = 100 %). É do ambiente (claro/escuro da cena).
        if (ready) EnvValueRow(env, stringResource(R.string.scene_exposure), settings[4] * 100f, 1f, 1f, 400f, "%", 100f, "exposicao 3D") {
            change(4, it / 100f)
        }
    }

    if (ready) {
        // Chão: 0 sem chão, 1 visível, 2 só sombra (compõe sobre o 2D).
        EnvSection(stringResource(R.string.env3d_section_floor), stringResource(R.string.env3d_floor_hint), Modifier.testTag("env3d.floor")) {
            val floor = settings[1].toInt()
            EnvChoices {
                EnvFloors.forEachIndexed { i, label ->
                    EnvChoice(stringResource(label), floor == i, "panel3d.floor.$i") { if (floor != i) change(1, i.toFloat()) }
                }
            }
        }
    }
    Spacer(Modifier.height(16.dp))
}

/** RENDER: qualidade, tone map e bloom da cena 3D inteira (eram o "Avançado" do ambiente). */
@Composable
internal fun Render3DTab(env: PanelEnv) {
    val (settings, change) = rememberSceneSettings(env.store)
    if (settings.size < 8) return
    EnvSection(stringResource(R.string.scene_quality), stringResource(R.string.env3d_render_hint), Modifier.testTag("env3d.quality")) {
        EnvChoices {
            EnvQualities.forEachIndexed { i, label ->
                val on = settings[2].toInt() == i
                EnvChoice(stringResource(label), on, "env3d.quality.$i") { if (!on) change(2, i.toFloat()) }
            }
        }
    }
    EnvSection(stringResource(R.string.scene_tonemap), null, Modifier.testTag("env3d.tonemap")) {
        EnvChoices {
            EnvToneMaps.forEachIndexed { i, label ->
                val on = settings[3].toInt() == i
                EnvChoice(stringResource(label), on, "env3d.tonemap.$i") { if (!on) change(3, i.toFloat()) }
            }
        }
    }
    EnvSection(stringResource(R.string.scene_bloom), null, Modifier.testTag("env3d.bloom")) {
        EnvToggle(stringResource(R.string.scene_bloom), settings[5] > 0.5f, "env3d.bloom.switch") { change(5, if (it) 1f else 0f) }
        if (settings[5] > 0.5f) {
            EnvValueRow(env, stringResource(R.string.ui3d_bloom_strength), settings[6] * 100f, 1f, 0f, 400f, "%", 60f, "bloom 3D") {
                change(6, it / 100f)
            }
            EnvValueRow(env, stringResource(R.string.scene031_bloom_threshold), settings[7], .05f, 0f, 8f, "", 1.25f, "bloom 3D", decimals = 2) {
                change(7, it)
            }
        }
    }
    Spacer(Modifier.height(16.dp))
}

/**
 * OBJETO (aba Material): projeta / recebe sombra — eram do ambiente, mas são
 * deste objeto. O "Mostrar interior" continua logo abaixo ([InteriorToggle]).
 */
@Composable
internal fun ObjectShadowSection(store: EditorStore) {
    val sh by remember(store) { derivedStateOf { store.modelShadows } }
    val cur = sh ?: return
    EnvSection(stringResource(R.string.env3d_section_object), null, Modifier.testTag("env3d.object")) {
        EnvToggle(stringResource(R.string.pn_t3d_cast_shadow), cur.first, "panel3d.shadow.cast") { store.setModelShadows(it, cur.second) }
        EnvToggle(stringResource(R.string.pn_t3d_receive_shadow), cur.second, "panel3d.shadow.receive") { store.setModelShadows(cur.first, it) }
    }
}

/**
 * TEXTURA do material [material] (0.3.6): a miniatura (bola do motor, com a
 * textura nova) e o que a cobre agora — o mapa do modelo, uma imagem ou uma
 * camada da timeline — com Arquivo · Timeline · Remover. [allowFile] falso na
 * forma 3D: a imagem do arquivo ela já escolhe na linha "Imagem" da parte.
 */
@Composable
internal fun MaterialTextureRow(env: PanelEnv, material: Int, fallback: Color, allowFile: Boolean = true) {
    val store = env.store
    val layer = store.primary ?: return
    val revision = store.curveRevision
    // A revisão do modelo muda a cada edição, desfazer e refazer.
    val (kind, name) = remember(layer, material, revision) { store.materialTexture(material) }
    val thumb = rememberMaterialThumb(store, layer, material, Triple(revision, kind, name))
    var choosing by remember { mutableStateOf(false) }
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) store.setMaterialTextureImage(material, uri)
    }
    val value = when (kind) {
        1 -> stringResource(R.string.env3d_texture_image)
        2 -> stringResource(R.string.env3d_texture_layer_value, name.ifBlank { stringResource(R.string.env3d_layer_unnamed) })
        else -> stringResource(R.string.env3d_texture_original)
    }
    val desc = stringResource(R.string.env3d_texture_desc, material + 1)
    Column(Modifier.fillMaxWidth().padding(top = 6.dp).testTag("panel3d.texture.$material")) {
        Row(Modifier.fillMaxWidth().heightIn(min = 56.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.semantics { contentDescription = "$desc: $value" }) { MaterialBall(thumb, fallback, 44.dp) }
            Spacer(Modifier.width(12.dp))
            Column(Modifier.weight(1f)) {
                Text(stringResource(R.string.env3d_texture), style = AureaType.Base.merge(TextStyle(fontSize = PanelMetrics.Label,
                    fontWeight = FontWeight.W600, color = AureaColors.Text)))
                Text(value, maxLines = 1, overflow = TextOverflow.Ellipsis,
                    style = AureaType.Base.merge(TextStyle(fontSize = PanelMetrics.Note, color = if (kind != 0) AureaColors.Accent else AureaColors.Muted)))
            }
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            if (allowFile) EnvButton(stringResource(R.string.env3d_texture_from_file), "panel3d.texture.file") { picker.launch(arrayOf("image/*")) }
            EnvButton(stringResource(R.string.env3d_texture_from_timeline), "panel3d.texture.timeline", on = kind == 2) { choosing = true }
            if (kind != 0) EnvButton(stringResource(R.string.env3d_texture_remove), "panel3d.texture.remove") { store.clearMaterialTexture(material) }
        }
    }
    if (choosing) {
        val options = remember(layer, revision) { store.materialTextureLayers() }
        val unnamed = stringResource(R.string.env3d_layer_unnamed)
        AureaActionSheet(
            title = stringResource(R.string.env3d_texture_layer_title),
            message = stringResource(if (options.isEmpty()) R.string.env3d_texture_no_layers else R.string.env3d_texture_layer_hint),
            actions = options.map { (id, label) ->
                SheetAction(label.ifBlank { unnamed }, tag = "panel3d.texture.layer.$id") { store.setMaterialTextureLayer(material, id) }
            },
            onDismiss = { choosing = false },
        )
    }
}

// --- Peças (medidas de PanelMetrics: título 15, rótulo 13, nota 12, chip 36/14, toque 44) ---

/** Seção com título (e dica) e respiro: um cartão por assunto. */
@Composable
internal fun EnvSection(title: String, hint: String?, modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Column(modifier.fillMaxWidth().padding(top = 14.dp)) {
        Text(title, modifier = Modifier.semantics { heading() },
            style = AureaType.Base.merge(TextStyle(fontSize = PanelMetrics.Title, fontWeight = FontWeight.W700, color = AureaColors.Text)))
        if (hint != null) {
            Spacer(Modifier.height(3.dp))
            Text(hint, style = AureaType.Base.merge(TextStyle(fontSize = PanelMetrics.Note, lineHeight = 16.sp, color = AureaColors.Muted)))
        }
        Spacer(Modifier.height(8.dp))
        Column(
            Modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp)).background(AureaColors.PanelFrameContent)
                .padding(horizontal = 12.dp, vertical = 8.dp),
            content = content,
        )
    }
}

/** Escolhas que quebram linha (nada escondido numa fileira que rola). */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun EnvChoices(content: @Composable () -> Unit) {
    FlowRow(
        Modifier.fillMaxWidth().padding(vertical = 4.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalArrangement = Arrangement.spacedBy(2.dp),
    ) { content() }
}

/** Ficha de escolha: 36 de desenho, 44 de toque; "selecionado" para o leitor de tela. */
@Composable
private fun EnvChoice(label: String, on: Boolean, tag: String, onClick: () -> Unit) {
    Box(
        Modifier.heightIn(min = PanelMetrics.Touch).testTag(tag)
            .semantics(mergeDescendants = true) { selected = on }
            .tocavel(shrink = 1f, role = Role.Button, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier.heightIn(min = PanelMetrics.ChipHeight).clip(RoundedCornerShape(10.dp))
                .background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                .padding(horizontal = PanelMetrics.ChipPad),
            contentAlignment = Alignment.Center,
        ) {
            Text(label, maxLines = 1, style = AureaType.Base.merge(TextStyle(fontSize = PanelMetrics.ChipText,
                fontWeight = if (on) FontWeight.W600 else FontWeight.W500, color = if (on) AureaColors.Accent else AureaColors.Text)))
        }
    }
}

/** Botão de ação da seção (mesma ficha, sem estado de escolha salvo o [on]). */
@Composable
private fun EnvButton(label: String, tag: String, on: Boolean = false, onClick: () -> Unit) =
    EnvChoice(label, on, tag, onClick)

/** Linha "rótulo · interruptor" de 52 dp (a linha toda toca). */
@Composable
private fun EnvToggle(label: String, on: Boolean, tag: String, onChange: (Boolean) -> Unit) {
    Row(
        Modifier.fillMaxWidth().heightIn(min = 52.dp).testTag(tag)
            .semantics(mergeDescendants = true) {}
            .tocavel(shrink = 1f) { onChange(!on) },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600)))
        AureaToggle(checked = on, onCheckedChange = onChange)
    }
}

/** Linha "rótulo · conteúdo à direita". */
@Composable
private fun EnvLine(label: String, trailing: @Composable () -> Unit) {
    Row(Modifier.fillMaxWidth().heightIn(min = 52.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600)))
        trailing()
    }
}

/** Linha de valor da cena/ambiente: um passo de desfazer por arrasto (quando [gesture] existe). */
@Composable
private fun EnvValueRow(
    env: PanelEnv,
    label: String,
    value: Float,
    step: Float,
    min: Float,
    max: Float,
    unit: String,
    default: Float,
    gesture: String?,
    decimals: Int = 0,
    onValue: (Float) -> Unit,
) {
    val store = env.store
    HumanRow(
        env, label, value, step, min, max, unit, decimals, default,
        onStart = { if (gesture != null) store.beginGesture(gesture) },
        onValue = onValue,
        onEnd = { if (gesture != null) store.endGesture() },
        onCommit = { v -> if (gesture != null) store.beginGesture(gesture); onValue(v); if (gesture != null) store.endGesture() },
    )
}
