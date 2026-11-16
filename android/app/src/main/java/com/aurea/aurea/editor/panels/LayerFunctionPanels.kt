package com.aurea.aurea.editor.panels

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.engine.LayerRow
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.LayerType
import com.aurea.aurea.ui.theme.tocavel

// =============================================================================
// Funções do AUREA (13/11/2026, decisão 1 do dono): as chaves da camada e o 3D
// da camada saíram do ⋮ e ganharam painel próprio, aberto pelo navegador de
// Efeitos (grupo "Funções do Aurea"). Mesma moldura dos outros painéis
// ([PanelFrame], ‹ na coluna esquerda); cada linha é título + dica + chave.
// Par do iOS: LayerFunctionPanels.swift. Os comandos são os mesmos que o ⋮ usava.
// =============================================================================

/** A camada escolhida como a lista de camadas a vê (as chaves moram em [LayerRow]). */
private fun primaryRow(store: EditorStore): Pair<Int, LayerRow>? {
    val id = store.primary ?: return null
    val index = store.editingLayers.indexOfFirst { it.id == id }
    return store.editingLayers.getOrNull(index)?.let { index to it }
}

/**
 * OPÇÕES DA CAMADA: camada de ajuste (com "afeta"), guia, preservar
 * transparência, linha magnética, tímida e, só no Aurea PC, qualidade e
 * amostragem. Uma lista de linhas, sem seções.
 */
@Composable
internal fun LayerOptionsPanel(env: PanelEnv) {
    val store = env.store
    val (index, row) = primaryRow(store) ?: return
    val type = LayerType.of(row.kind)
    val visual = type != LayerType.Audio
    val media = type == LayerType.Video || type == LayerType.Image
    PanelFrame(rail = PanelRail(onBack = env.onClose, supportsAnimation = false)) {
        FunctionList(Modifier.testTag("layerOptions.panel")) {
            if (visual) {
                BlurToggleRow(
                    stringResource(R.string.editor_camada_ajuste),
                    stringResource(R.string.editor_efeitos_desta_camada_valem_todas_baixo),
                    row.adjustment, Modifier.testTag("layerOptions.adjustment"),
                ) { store.setLayerAdjustment(row.id, it) }
                if (row.adjustment) AdjustmentScopeRows(store, index)
                BlurToggleRow(
                    stringResource(R.string.editor_guia_nao_exporta),
                    stringResource(R.string.editor_aparece_aqui_editor_fica_fora_video),
                    row.guide, Modifier.testTag("layerOptions.guide"),
                ) { store.setLayerGuide(row.id, it) }
                BlurToggleRow(
                    stringResource(R.string.ae_layer_preserve_transparency),
                    stringResource(R.string.ae_layer_preserve_transparency_hint),
                    row.preserveTransparency, Modifier.testTag("layerOptions.preserveTransparency"),
                ) { if (!row.locked) store.setLayerPreserveTransparency(row.id, it) }
            }
            // Linha Magnética: só em trecho com FONTE (vídeo, imagem, áudio ou pré-composição).
            if (media || type == LayerType.Audio || type == LayerType.Group) {
                BlurToggleRow(
                    stringResource(R.string.editor_linha_magnetica),
                    stringResource(R.string.editor_linha_magnetica_detalhe),
                    row.magnetic, Modifier.testTag("layerOptions.magnetic"),
                ) { store.setLayerMagneticTrack(row.id, it) }
            }
            BlurToggleRow(
                stringResource(R.string.ae_layer_shy),
                stringResource(R.string.ae_layer_shy_hint),
                row.shy, Modifier.testTag("layerOptions.shy"),
            ) { store.setLayerShy(row.id, it) }
            // AE layers: Qualidade e amostragem (só a prévia), só no Aurea PC.
            if (visual && com.aurea.aurea.ui.platform.PlatformFeatures.desktopAe) {
                val qualityName = stringResource(
                    when (row.quality) {
                        1 -> R.string.ae_quality_draft
                        2 -> R.string.ae_quality_wireframe
                        else -> R.string.ae_quality_best
                    },
                )
                FunctionAction(
                    stringResource(R.string.ae_layer_quality) + ": " + qualityName,
                    stringResource(R.string.ae_quality_hint), "layerOptions.quality",
                ) { store.setLayerQuality(row.id, (row.quality + 1) % 3, if (row.bicubic) 1 else 0) }
                BlurToggleRow(
                    stringResource(R.string.ae_layer_sampling) + ": " +
                        stringResource(if (row.bicubic) R.string.ae_sampling_bicubic else R.string.ae_sampling_bilinear),
                    stringResource(R.string.ae_sampling_hint),
                    row.bicubic, Modifier.testTag("layerOptions.sampling"),
                ) { store.setLayerQuality(row.id, row.quality, if (it) 1 else 0) }
            }
        }
    }
}

/** "Afeta": todas abaixo → só a logo abaixo → só as escolhidas (com a lista delas). */
@Composable
private fun AdjustmentScopeRows(store: EditorStore, index: Int) {
    val scope = store.adjustmentScope
    FunctionAction(
        stringResource(R.string.la_affected) + " · " + stringResource(
            when (scope) {
                1 -> R.string.la_affected_below
                2 -> R.string.la_affected_chosen
                else -> R.string.la_affected_all
            },
        ),
        stringResource(if (scope == 2) R.string.la_affected_chosen_hint else R.string.la_affected_hint),
        "layerOptions.affected",
    ) { store.changeAdjustmentScope((scope + 1) % 3) }
    if (scope == 2) {
        val below = store.editingLayers.drop(index + 1).filter { LayerType.of(it.kind) != LayerType.Audio }
        if (below.isEmpty()) FunctionNote(stringResource(R.string.la_affected_none_below))
        below.forEach { b ->
            BlurToggleRow(b.name, "", b.id in store.adjustmentTargets, Modifier.padding(start = 12.dp)) {
                store.toggleAdjustmentTarget(b.id)
            }
        }
    }
}

/**
 * 3D DA CAMADA: camada 3D (o atalho da barra continua), virar objeto 3D (imagem
 * e vídeo), prender a um osso (com um modelo 3D com esqueleto na composição),
 * câmera do grupo e aceitar luzes.
 */
@Composable
internal fun Layer3DPanel(env: PanelEnv) {
    val store = env.store
    val (_, row) = primaryRow(store) ?: return
    var showBones by remember(row.id) { mutableStateOf(false) }
    if (showBones) com.aurea.aurea.editor.BoneAttachSheet(store, row.id) { showBones = false }
    val type = LayerType.of(row.kind)
    val intrinsic = row.kind in 8..10
    val threeD = row.isThreeD || intrinsic
    PanelFrame(rail = PanelRail(onBack = env.onClose, supportsAnimation = false)) {
        FunctionList(Modifier.testTag("layer3d.panel")) {
            BlurToggleRow(
                stringResource(R.string.sh_add_tab_3d),
                stringResource(if (intrinsic) R.string.fn_layer3d_intrinsic else R.string.fn_layer3d_hint),
                threeD, Modifier.testTag("layer3d.threeD"),
            ) { if (!row.locked && !intrinsic) store.changeLayer3D(it) }
            if (row.kind == LayerType.Image.kind || row.kind == LayerType.Video.kind) {
                FunctionAction(
                    stringResource(R.string.layer_to_3d_object), stringResource(R.string.layer_to_3d_object_hint), "layer3d.toObject",
                    enabled = !row.locked,
                ) { store.layerTo3DObject(row.id, row.name) }
            }
            if (row.kind != LayerType.Model3D.kind && row.kind != LayerType.Audio.kind &&
                store.editingLayers.any { it.kind == LayerType.Model3D.kind && it.id != row.id }) {
                val bone = store.layerParentBone(row.id)
                val parentModel = store.editingLayers.getOrNull(row.parentIndex)?.takeIf { it.kind == LayerType.Model3D.kind }
                FunctionAction(
                    stringResource(R.string.bone_attach_title),
                    if (bone >= 0 && parentModel != null)
                        stringResource(R.string.bone_attach_current, com.aurea.aurea.editor.boneLabel(store.boneName(parentModel.id, bone), bone))
                    else stringResource(R.string.bone_attach_menu_hint),
                    "layer3d.boneAttach", enabled = !row.locked,
                ) { showBones = true }
            }
            if (type == LayerType.Group) {
                BlurToggleRow(
                    stringResource(R.string.la_group_camera), stringResource(R.string.la_group_camera_hint),
                    store.groupCameraPassThrough == 1, Modifier.testTag("layer3d.groupCamera"),
                ) { store.changeGroupCameraPassThrough(it) }
            }
            if (store.layerAcceptsLights >= 0) {
                BlurToggleRow(
                    stringResource(R.string.la_accepts_lights), stringResource(R.string.la_accepts_lights_hint),
                    store.layerAcceptsLights == 1, Modifier.testTag("layer3d.acceptsLights"),
                ) { store.changeLayerAcceptsLights(it) }
            }
        }
    }
}

// --- Peças ---------------------------------------------------------------------

@Composable
private fun ColumnScope.FunctionList(modifier: Modifier, content: @Composable ColumnScope.() -> Unit) {
    Column(modifier.fillMaxWidth().padding(start = 8.dp, top = 6.dp, end = 12.dp, bottom = 10.dp), content = content)
}

/** Linha de ação (título + dica), toda tocável. */
@Composable
private fun FunctionAction(label: String, hint: String, tag: String, enabled: Boolean = true, onClick: () -> Unit) {
    Column(
        Modifier.fillMaxWidth().testTag(tag)
            .then(if (enabled) Modifier.tocavel(onClick = onClick) else Modifier)
            .padding(vertical = 8.dp).padding(start = 4.dp),
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = PanelMetrics.ChipText,
            color = if (enabled) AureaColors.Accent else AureaColors.Disabled)))
        if (hint.isNotEmpty()) Text(hint, style = AureaType.Base.merge(TextStyle(fontSize = PanelMetrics.Note, lineHeight = 16.sp, color = AureaColors.Muted)))
    }
    Spacer(Modifier.height(2.dp))
}

@Composable
private fun FunctionNote(text: String) {
    Text(text, modifier = Modifier.padding(start = 16.dp, top = 4.dp, bottom = 4.dp),
        style = AureaType.Base.merge(TextStyle(fontSize = PanelMetrics.Note, color = AureaColors.Muted)))
}
