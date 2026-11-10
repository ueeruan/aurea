package com.aurea.aurea.home

import android.text.format.Formatter
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.state.AppStorage
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.StorageClean
import com.aurea.aurea.state.StorageLine
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.ds.AureaDialog
import com.aurea.aurea.ui.ds.AureaDialogDefaults
import com.aurea.aurea.ui.ds.DialogAction
import com.aurea.aurea.ui.ds.DialogRole
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.tocavel

/**
 * Armazenamento (0.5.2) — par do StorageView.swift. Quanto cada coisa ocupa,
 * medido agora (fora da main: [EditorStore.refreshStorage]), e "Limpar" em
 * cada linha que pode sair. Cache sai direto; mídia sem projeto e exportações
 * guardadas não são cache: pedem confirmação, com o tamanho e o aviso.
 */
@Composable
internal fun StorageDialog(store: EditorStore, onDismiss: () -> Unit) {
    val context = LocalContext.current
    LaunchedEffect(Unit) { store.refreshStorage() }
    val lines = store.storageLines
    var confirm by remember { mutableStateOf<StorageLine?>(null) }
    fun size(bytes: Long) = Formatter.formatShortFileSize(context, bytes)
    AureaDialog(
        title = stringResource(R.string.storage_title),
        message = if (lines == null) stringResource(R.string.storage_measuring)
            else stringResource(R.string.storage_total, size(lines.sumOf { it.bytes })),
        actions = listOf(DialogAction(stringResource(R.string.common_close), DialogRole.Positive, tag = "storage.close")),
        onDismiss = onDismiss,
        tag = "home.drawer.storage",
    ) {
        lines?.forEachIndexed { i, line ->
            if (i > 0) HorizontalDivider(thickness = AureaDialogDefaults.Hairline, color = AureaColors.LightDivider)
            StorageRow(line, size(line.bytes)) {
                if (line.clean == StorageClean.CONFIRM) confirm = line else store.cleanStorage(line.id)
            }
        }
    }
    confirm?.let { line ->
        val orphans = line.id == AppStorage.ORPHAN_MEDIA
        AureaAlert(
            title = stringResource(if (orphans) R.string.storage_confirm_orphans_title else R.string.storage_confirm_exports_title),
            message = if (orphans) stringResource(R.string.storage_confirm_orphans_message, line.files, size(line.bytes))
                else stringResource(R.string.storage_confirm_exports_message, size(line.bytes)),
            confirmLabel = stringResource(R.string.common_delete),
            destructive = true,
            onConfirm = { store.cleanStorage(line.id) },
            onDismiss = { confirm = null },
        )
    }
}

/** Título, detalhe e tamanho; "Limpar" à direita quando a linha pode sair. */
@Composable
private fun StorageRow(line: StorageLine, size: String, onClean: () -> Unit) {
    val canClean = line.clean != StorageClean.NONE && line.bytes > 0
    val rowLabel = stringResource(R.string.storage_row_a11y, line.title, size)
    Row(
        Modifier.fillMaxWidth().heightIn(min = 52.dp).padding(vertical = 8.dp).testTag("storage.row.${line.id}"),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f).semantics(mergeDescendants = true) { contentDescription = rowLabel }) {
            Text(line.title, style = AureaDialogDefaults.bodyStyle.copy(fontWeight = FontWeight.W500),
                modifier = Modifier.clearAndSetSemantics { })
            Text(size, style = AureaDialogDefaults.bodyStyle, modifier = Modifier.clearAndSetSemantics { })
            Text(line.detail, style = AureaDialogDefaults.noteStyle, modifier = Modifier.clearAndSetSemantics { })
        }
        if (canClean) {
            Spacer(Modifier.width(8.dp))
            val cleanLabel = stringResource(R.string.storage_clean_a11y, line.title)
            Text(
                stringResource(R.string.storage_clean),
                style = AureaDialogDefaults.bodyStyle.copy(
                    color = if (line.clean == StorageClean.CONFIRM) AureaColors.LightDanger else AureaColors.LightAccentInk,
                    fontWeight = FontWeight.W500,
                ),
                modifier = Modifier
                    .testTag("storage.clean.${line.id}")
                    .semantics { contentDescription = cleanLabel }
                    .tocavel(onClick = onClean)
                    .padding(horizontal = 8.dp, vertical = 10.dp),
            )
        }
    }
}
