package com.aurea.aurea.effects

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.ds.AureaModalSheet
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel

/**
 * PLUGINS DE EFEITO (0.3.0): instalar um .aureaplugin ou um shader ISF (.fs),
 * ver o que está instalado (autor, versão, se funciona neste aparelho) e
 * remover. O guia de criação completo mora em docs/plugins; aqui fica o resumo.
 * Par do iOS: `PluginManagerView` (PluginsView na EffectsView.swift).
 */
@Composable
internal fun PluginManagerSheet(store: EditorStore, onDismiss: () -> Unit) {
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) store.installPlugin(uri)
    }
    LaunchedEffect(Unit) { store.refreshPlugins() }
    var removing by remember { mutableStateOf<PluginTexts.Plugin?>(null) }
    var help by remember { mutableStateOf(false) }
    val tag = LocalConfiguration.current.locales[0].toLanguageTag()
    AureaModalSheet(onDismiss = onDismiss) {
        Column(
            Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(horizontal = 18.dp, vertical = 6.dp).testTag("plugins.sheet"),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(stringResource(R.string.plug_title), style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600)))
            Text(stringResource(R.string.plug_hint), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
            Box(
                Modifier.fillMaxWidth().height(46.dp).clip(RoundedCornerShape(12.dp)).background(AureaColors.Accent)
                    .testTag("plugins.install")
                    .tocavel(enabled = store.busyMessage == null) { picker.launch(arrayOf("application/zip", "application/octet-stream", "text/plain", "*/*")) },
                contentAlignment = Alignment.Center,
            ) {
                Text(stringResource(R.string.plug_install), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600, color = AureaColors.OnAccent)))
            }
            store.busyMessage?.let { Text(it, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted))) }
            if (store.plugins.isEmpty()) {
                Text(stringResource(R.string.plug_empty), modifier = Modifier.padding(vertical = 8.dp),
                    style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
            }
            store.plugins.forEach { p -> PluginRow(p, tag) { removing = p } }
            Text(stringResource(R.string.plug_safety), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
            Text(stringResource(R.string.plug_help_title), modifier = Modifier.padding(top = 6.dp).testTag("plugins.help").tocavel(shrink = 1f) { help = !help },
                style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)))
            if (help) Text(stringResource(R.string.plug_help_body), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, lineHeight = 19.sp)))
            Spacer(Modifier.height(12.dp))
        }
    }
    removing?.let { p ->
        val name = PluginTexts.pick(p.names, tag) ?: p.id
        AureaAlert(
            title = stringResource(R.string.plug_remove_title, name),
            message = stringResource(R.string.plug_remove_body),
            confirmLabel = stringResource(R.string.plug_remove),
            destructive = true,
            onConfirm = { store.removePlugin(p.id) },
            onDismiss = { removing = null },
        )
    }
}

@Composable
private fun PluginRow(p: PluginTexts.Plugin, tag: String, onRemove: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(AureaColors.Chip).padding(12.dp).testTag("plugins.row.${p.id}"),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(PluginTexts.pick(p.names, tag) ?: p.id, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600)))
            Text(stringResource(R.string.plug_by, p.author.ifEmpty { p.id }, p.version) + " · " +
                stringResource(when {
                    p.kind == "stack" -> R.string.plug_kind_stack
                    p.scene3d -> R.string.plug_kind_shader3d
                    else -> R.string.plug_kind_shader
                }),
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
            PluginTexts.pick(p.descriptions, tag)?.takeIf { it.isNotBlank() }?.let {
                Text(it, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Text)))
            }
            if (!p.ok) Text(stringResource(R.string.plug_error, p.error), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Warning)))
            else if (p.testRenderMs > 0) Text(stringResource(R.string.plug_render_ms, String.format(java.util.Locale.ROOT, "%.1f", p.testRenderMs)),
                style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = AureaColors.Muted)))
        }
        Text(stringResource(R.string.plug_remove), modifier = Modifier.padding(start = 10.dp).testTag("plugins.remove.${p.id}").tocavel(onClick = onRemove),
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Danger)))
    }
}
