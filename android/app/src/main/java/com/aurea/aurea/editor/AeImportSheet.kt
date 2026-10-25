package com.aurea.aurea.editor

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
    AureaModalSheet(onDismiss = { store.dismissAeImport() }) {
        Column(
            Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(horizontal = 18.dp, vertical = 6.dp).testTag("ae.sheet"),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            // 0.3.1: script que parou com erro DEPOIS de mudar o projeto = "importado em
            // parte" (contagens + o erro); rodou sem mudar nada = "nada foi importado".
            val partial = !result.ok && (result.hasCounts || result.changes > 0)
            Text(stringResource(when {
                    result.ok -> R.string.ae_done_title
                    partial -> R.string.ae_partial_title
                    else -> R.string.ae_failed_title
                }),
                modifier = Modifier.semantics { heading() },
                style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600)))
            Text(result.fileName, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
            if (result.ok || result.hasCounts) {
                Text(stringResource(R.string.ae_counts, result.effects, result.keyframes, result.layers, result.expressions, result.animators),
                    modifier = Modifier.testTag("ae.counts"), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp)))
            }
            if (result.message.isNotBlank()) Text(result.message, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
            if (result.skipped.isNotEmpty()) {
                Text(stringResource(R.string.ae_skipped_title, result.skipped.size), modifier = Modifier.padding(top = 8.dp),
                    style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Warning)))
                result.skipped.take(60).forEach { ItemRow(it) }
                if (result.skipped.size > 60) Text("…", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
            }
            if (result.notes.isNotEmpty()) {
                Text(stringResource(R.string.ae_notes_title, result.notes.size), modifier = Modifier.padding(top = 8.dp).testTag("ae.notes")
                    .tocavel(shrink = 1f) { showNotes = !showNotes },
                    style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)))
                if (showNotes) result.notes.take(60).forEach { ItemRow(it) }
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
private fun ItemRow(item: EditorStore.AeImportItem) {
    val kind = stringResource(aeKindLabel(item.kind))
    val line = if (item.line > 0) " · " + stringResource(R.string.ae_line, item.line) else ""
    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip).padding(horizontal = 12.dp, vertical = 8.dp)) {
        Text("$kind · ${item.name}$line", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W500)))
        if (item.reason.isNotBlank()) Text(item.reason, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
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
    "mascara" -> R.string.ae_kind_mask
    "script" -> R.string.ae_kind_script
    "chamada" -> R.string.ae_kind_call
    else -> R.string.ae_kind_other
}
