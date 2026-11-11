package com.aurea.aurea.editor

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.LayerType

// =============================================================================
//  "Prender a um osso" (0.5.2) — par do BoneAttachSheet do iOS (ToolbarView.swift).
//
//  A camada segue a posição e o giro de uma junta de um modelo 3D com rig
//  (Engine::set_layer_parent_bone): partículas na mão, um objeto na cabeça.
//  Os ossos de cada modelo com rig da composição, na ordem da hierarquia;
//  escolher um prende (o modelo vira o pai); "Soltar do osso" volta a seguir
//  o modelo inteiro.
// =============================================================================

/** Nome legível do osso: sem o prefixo do Mixamo ("mixamorig:LeftHand" → "LeftHand"). */
internal fun boneLabel(raw: String, node: Int): String {
    val name = raw.substringAfterLast(':').ifBlank { raw }
    return name.ifBlank { "#$node" }
}

@Composable
internal fun BoneAttachSheet(store: EditorStore, layerId: Long, onDismiss: () -> Unit) {
    val rows = store.layers
    val row = rows.firstOrNull { it.id == layerId }
    // Os modelos com rig (o pai atual primeiro), sem a própria camada.
    val models = remember(rows, layerId) {
        val parent = row?.let { rows.getOrNull(it.parentIndex)?.id } ?: 0L
        rows.filter { it.id != layerId && it.kind == LayerType.Model3D.kind }
            .map { it to store.modelAttachBones(it.id) }
            .filter { it.second.isNotEmpty() }
            .sortedBy { if (it.first.id == parent) 0 else 1 }
    }
    val currentModel = row?.let { rows.getOrNull(it.parentIndex)?.id } ?: 0L
    val currentBone = remember(rows, layerId) { store.layerParentBone(layerId) }
    com.aurea.aurea.ui.ds.AureaModalSheet(onDismiss = onDismiss) {
        Column(Modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, bottom = 24.dp).testTag("boneAttach.sheet")) {
            Text(stringResource(R.string.bone_attach_title), style = AureaType.of(16f, FontWeight.W700),
                modifier = Modifier.semantics { heading() })
            Text(stringResource(R.string.bone_attach_hint), style = AureaType.of(13f, color = AureaColors.Muted),
                modifier = Modifier.padding(top = 4.dp, bottom = 12.dp))
            if (models.isEmpty()) {
                Text(stringResource(R.string.bone_attach_no_rig), style = AureaType.of(14f, color = AureaColors.Muted),
                    modifier = Modifier.padding(vertical = 12.dp).testTag("boneAttach.empty"))
                return@Column
            }
            if (currentBone >= 0 && currentModel != 0L) {
                BoneRow(stringResource(R.string.bone_attach_release), selected = false, tag = "boneAttach.release") {
                    store.setLayerParentBone(layerId, currentModel, -1)
                    onDismiss()
                }
            }
            LazyColumn(Modifier.fillMaxWidth().heightIn(max = 420.dp)) {
                models.forEach { (model, bones) ->
                    item(key = "m${model.id}") {
                        Text(model.name.ifBlank { stringResource(R.string.app_layer_type_model) },
                            style = AureaType.of(13f, FontWeight.W700, color = AureaColors.Muted),
                            modifier = Modifier.padding(top = 12.dp, bottom = 4.dp).semantics { heading() })
                    }
                    items(bones.toList(), key = { "b${model.id}.$it" }) { node ->
                        val label = boneLabel(store.boneName(model.id, node), node)
                        BoneRow(label, selected = model.id == currentModel && node == currentBone, tag = "boneAttach.bone.$node") {
                            store.setLayerParentBone(layerId, model.id, node, label)
                            onDismiss()
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun BoneRow(label: String, selected: Boolean, tag: String, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().heightIn(min = 48.dp).clip(RoundedCornerShape(10.dp))
            .background(if (selected) AureaColors.AccentDim else androidx.compose.ui.graphics.Color.Transparent)
            .testTag(tag)
            .semantics { contentDescription = label; role = Role.RadioButton; this.selected = selected }
            .clickable(onClick = onClick)
            .padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, style = AureaType.of(15f, if (selected) FontWeight.W700 else FontWeight.W500,
            color = if (selected) AureaColors.Accent else AureaColors.Text))
    }
}
