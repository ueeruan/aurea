package com.aurea.aurea.editor

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaModalSheet
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel

/**
 * IMPORTAR DO AFTER EFFECTS (0.3.0): o que entrou (efeitos, keyframes, camadas,
 * expressões, animadores de texto) e, sem esconder nada, o que ficou de fora e o
 * que entrou aproximado — com a linha do script. Par do iOS: `AeImportSheet`
 * (EditorView.swift).
 */
@Composable
internal fun AeImportSheet(store: EditorStore, result: EditorStore.AeImportResult) {
    var showNotes by remember { mutableStateOf(false) }
    var repairLayer by remember { mutableStateOf(0L) }
    val link = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null && repairLayer != 0L && store.primary == repairLayer) {
            store.replaceMedia(repairLayer, uri)
            store.dismissAeImport()
        }
        repairLayer = 0L
    }
    val open: (EditorStore.AeImportItem) -> Unit = { item ->
        if (store.focusReportedLayer(item.composition, item.layer)) {
            if (item.kind == "midia" && store.layers.firstOrNull { it.id == item.layer }?.kind in listOf(1, 2)) {
                repairLayer = item.layer
                link.launch(arrayOf(if (store.layers.firstOrNull { it.id == item.layer }?.kind == 1) "video/*" else "image/*"))
            } else store.dismissAeImport()
        }
    }
    AureaModalSheet(onDismiss = { store.dismissAeImport() }) {
        Column(
            Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(horizontal = 18.dp, vertical = 6.dp).testTag("ae.sheet"),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            val partial = if (result.ok) {
                result.kind == "aep" && result.skipped.isNotEmpty()
            } else {
                result.hasCounts || result.changes > 0
            }
            Text(stringResource(when {
                    partial -> R.string.ae_partial_title
                    result.ok -> R.string.ae_done_title
                    else -> R.string.ae_failed_title
                }),
                modifier = Modifier.semantics { heading() },
                style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600)))
            Text(result.fileName, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
            if (result.ok || result.hasCounts) {
                Text(stringResource(R.string.ae_counts, result.effects, result.keyframes, result.layers, result.expressions, result.animators),
                    modifier = Modifier.testTag("ae.counts"), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp)))
            }
            // 1.0.8: o código do motor na língua do app; o texto em pt-BR fica de reserva.
            val message = aeReportText(result.messageKey, result.messageArgs) ?: result.message
            if (message.isNotBlank()) Text(message, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
            if (result.skipped.isNotEmpty()) {
                Text(stringResource(R.string.ae_skipped_title, result.skipped.size), modifier = Modifier.padding(top = 8.dp),
                    style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Warning)))
                ImportItems(result.skipped, open)
                if (result.skipped.size > 60) Text("…", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
            }
            if (result.notes.isNotEmpty()) {
                Text(stringResource(R.string.ae_notes_title, result.notes.size), modifier = Modifier.padding(top = 8.dp).testTag("ae.notes")
                    .tocavel(shrink = 1f) { showNotes = !showNotes },
                    style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)))
                if (showNotes) ImportItems(result.notes, open)
            }
            if (result.ok || partial) {
                Text(stringResource(R.string.ae_undo_hint), modifier = Modifier.padding(top = 6.dp),
                    style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
            }
            Spacer(Modifier.height(6.dp))
            Box(
                Modifier.fillMaxWidth().height(46.dp).clip(RoundedCornerShape(12.dp)).background(AureaColors.Accent)
                    .testTag("ae.ok").tocavel { store.dismissAeImport() },
                contentAlignment = Alignment.Center,
            ) {
                Text(stringResource(R.string.common_ok), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600, color = AureaColors.OnAccent)))
            }
            Spacer(Modifier.height(10.dp))
        }
    }
}

@Composable
private fun ImportItems(items: List<EditorStore.AeImportItem>, open: (EditorStore.AeImportItem) -> Unit) {
    items.take(60).groupBy { Triple(it.composition, it.layer, it.layerName) }.forEach { (group, rows) ->
        if (group.third.isNotBlank()) {
            Text(group.third, Modifier.padding(top = 6.dp).semantics { heading() },
                style = AureaType.Base.merge(TextStyle(fontWeight = FontWeight.W600)))
        }
        rows.forEach { ItemRow(it) }
        val target = rows.first()
        if (target.layer != 0L) TextButton(onClick = { open(target) }) {
            Text(stringResource(if (target.kind == "midia") R.string.motion_link_media else R.string.motion_view_layer))
        }
    }
}

@Composable
private fun ItemRow(item: EditorStore.AeImportItem) {
    val kind = stringResource(aeKindLabel(item.kind))
    val line = if (item.line > 0) " · " + stringResource(R.string.ae_line, item.line) else ""
    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip).padding(horizontal = 12.dp, vertical = 8.dp)) {
        Text("$kind · ${item.name}$line", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W500)))
        val reason = aeReportText(item.reasonKey, item.reasonArgs) ?: item.reason
        if (reason.isNotBlank()) Text(reason, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
    }
}

/** Os tipos que o motor usa no relatório (`kind`): efeito, parametro, propriedade, texto… */
private fun aeKindLabel(kind: String): Int = when (kind) {
    "efeito" -> R.string.ae_kind_effect
    "parametro" -> R.string.ae_kind_param
    "propriedade" -> R.string.ae_kind_property
    "texto" -> R.string.ae_kind_text
    "seletor" -> R.string.ae_kind_selector
    "expressao" -> R.string.ae_kind_expression
    "camada" -> R.string.ae_kind_layer
    "composicao" -> R.string.ae_kind_composition
    "midia" -> R.string.ae_kind_media
    "mascara" -> R.string.ae_kind_mask
    "script" -> R.string.ae_kind_script
    "chamada" -> R.string.ae_kind_call
    else -> R.string.ae_kind_other
}

/**
 * 1.0.8: códigos estáveis do relatório do motor (`messageKey`, `reasonKey`) →
 * texto na língua do app. Nulo = código desconhecido (versão nova do motor):
 * quem chama mostra o texto em pt-BR que veio junto. Par do iOS: AeImportSheet.
 */
@Composable
internal fun aeReportText(key: String, args: List<String>): String? {
    val id = aeReportString(key) ?: return null
    return if (args.isEmpty()) stringResource(id) else stringResource(id, *args.toTypedArray())
}

private fun aeReportString(key: String): Int? = when (key) {
    "ae_msg_ok_aep" -> R.string.ae_msg_ok_aep
    "ae_msg_ok_jsx" -> R.string.ae_msg_ok_jsx
    "ae_msg_ok_ffx" -> R.string.ae_msg_ok_ffx
    "ae_err_nothing" -> R.string.ae_err_nothing
    "ae_err_save_failed" -> R.string.ae_err_save_failed
    "ae_err_name_taken" -> R.string.ae_err_name_taken
    "ae_err_choose_project" -> R.string.ae_err_choose_project
    "ae_err_choose_file" -> R.string.ae_err_choose_file
    "ae_err_aep_unreadable" -> R.string.ae_err_aep_unreadable
    "ae_err_ffx_unreadable" -> R.string.ae_err_ffx_unreadable
    "ae_err_too_big" -> R.string.ae_err_too_big
    "ae_err_open_failed" -> R.string.ae_err_open_failed
    "ae_err_no_project" -> R.string.ae_err_no_project
    "ae_err_project_changed" -> R.string.ae_err_project_changed
    "ae_err_comp_limit" -> R.string.ae_err_comp_limit
    "ae_err_aepx" -> R.string.ae_err_aepx
    "ae_err_jsxbin" -> R.string.ae_err_jsxbin
    "ae_err_archive_no_aep" -> R.string.ae_err_archive_no_aep
    "ae_err_archive_compression" -> R.string.ae_err_archive_compression
    "ae_err_archive_invalid" -> R.string.ae_err_archive_invalid
    "ae_err_archive_password" -> R.string.ae_err_archive_password
    "ae_err_archive_corrupt" -> R.string.ae_err_archive_corrupt
    "ae_err_no_space" -> R.string.ae_err_no_space
    "ae_err_internal" -> R.string.ae_err_internal
    "ae_reason_effect_unknown" -> R.string.ae_reason_effect_unknown
    "ae_reason_param_unknown" -> R.string.ae_reason_param_unknown
    "ae_reason_looks_invalid" -> R.string.ae_reason_looks_invalid
    "ae_reason_looks_missing" -> R.string.ae_reason_looks_missing
    "ae_reason_looks_tool_partial" -> R.string.ae_reason_looks_tool_partial
    "ae_reason_media_missing" -> R.string.ae_reason_media_missing
    "ae_reason_text_document" -> R.string.ae_reason_text_document
    "ae_reason_params_approx" -> R.string.ae_reason_params_approx
    "ae_reason_params_doc_order" -> R.string.ae_reason_params_doc_order
    "ae_reason_curve_defaults" -> R.string.ae_reason_curve_defaults
    "ae_reason_layer_null" -> R.string.ae_reason_layer_null
    "ae_reason_comp_limit" -> R.string.ae_reason_comp_limit
    "ae_reason_comp_cycle" -> R.string.ae_reason_comp_cycle
    "ae_reason_media_variant" -> R.string.ae_reason_media_variant
    else -> null
}
