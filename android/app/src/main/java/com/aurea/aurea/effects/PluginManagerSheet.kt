package com.aurea.aurea.effects

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
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
import com.aurea.aurea.engine.EffectCatalogEntry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel

/**
 * PLUGINS DE EFEITO (0.3.0): instalar um .aureaplugin ou um shader ISF (.fs),
 * ver o que está instalado (autor, versão, se funciona neste aparelho) e
 * remover. O guia de criação completo mora em docs/plugins; aqui fica o resumo.
 * Par do iOS: `PluginManagerView` em EffectsBrowser.swift.
 */
@Composable
internal fun PluginManagerPane(
    store: EditorStore,
    modifier: Modifier,
    effects: List<EffectCatalogEntry>,
    onOpenEffect: (EffectCatalogEntry) -> Unit,
    shortcuts: (@Composable () -> Unit)?,
) {
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        // 0.3.4 plugins: lê o pacote primeiro; a folha de consentimento instala.
        if (uri != null) PluginRuntime.beginInstall(uri)
    }
    LaunchedEffect(Unit) { store.refreshPluginsOffMain() }
    var removing by remember { mutableStateOf<PluginTexts.Plugin?>(null) }
    var help by remember { mutableStateOf(false) }
    val tag = LocalConfiguration.current.locales[0].toLanguageTag()
    val byType = remember(effects) { effects.associateBy { it.typeId } }
    LazyColumn(
        modifier.fillMaxWidth().testTag("plugins.sheet"),
        contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        item("plugins:import") {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Box(
                    Modifier.fillMaxWidth().heightIn(min = 46.dp).clip(RoundedCornerShape(12.dp)).background(AureaColors.Accent)
                        .testTag("plugins.install")
                        .tocavel(enabled = store.busyMessage == null) { picker.launch(arrayOf("application/zip", "application/octet-stream", "text/plain", "*/*")) },
                    contentAlignment = Alignment.Center,
                ) {
                    Text(stringResource(R.string.plug_install), style = AureaType.of(15f, FontWeight.SemiBold, color = AureaColors.OnAccent))
                }
                Text(".aureaplugin · .fs · .zip", style = AureaType.of(12f, color = AureaColors.Muted))
                store.busyMessage?.let { Text(it, style = AureaType.of(12f, color = AureaColors.Muted)) }
            }
        }
        item("plugins:safe-mode") {
            Column {
                if (PluginRuntime.safeMode) {
                    Text(stringResource(if (PluginRuntime.safeModeAutomatic) R.string.plugv3_safe_auto else R.string.plugv3_safe_on),
                        modifier = Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(AureaColors.Warning.copy(alpha = 0.14f)).padding(10.dp).testTag("plugins.safeMode.notice"),
                        style = AureaType.of(13f))
                }
                PermissionRow(stringResource(R.string.plugv3_safe_mode), PluginRuntime.safeMode, "plugins.safeMode") { PluginRuntime.changeSafeMode(it) }
            }
        }
        if (shortcuts != null) item("plugins:shortcuts") { shortcuts() }
        if (store.plugins.isEmpty()) {
            item("plugins:empty") {
                Text(stringResource(R.string.plug_empty), modifier = Modifier.padding(vertical = 8.dp),
                    style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
            }
        }
        items(store.plugins, key = { pluginItemKey(it.id) }) { p ->
            val effect = byType[p.effectTypeId]
            PluginRow(p, tag, effect?.let { { onOpenEffect(it) } }) { removing = p }
        }
        item("plugins:help") {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(stringResource(R.string.plug_help_title), modifier = Modifier.testTag("plugins.help").tocavel(shrink = 1f) { help = !help },
                    style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)))
                if (help) Text(stringResource(R.string.plug_help_body), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, lineHeight = 19.sp)))
                Spacer(Modifier.height(12.dp))
            }
        }
    }
    PluginOverlays(store)
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
private fun PluginRow(p: PluginTexts.Plugin, tag: String, onOpenEffect: (() -> Unit)?, onRemove: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(AureaColors.Chip).padding(12.dp).testTag("plugins.row.${p.id}"),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(PluginTexts.pick(p.names, tag) ?: p.id, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600)))
            Text(stringResource(R.string.plug_by, p.author.ifEmpty { p.id }, p.version) + " · " +
                stringResource(when {
                    p.kind == "audio" -> R.string.plug_kind_audio
                    p.kind == "stack" -> R.string.plug_kind_stack
                    p.kind == "panel" -> R.string.pv2_kind_panel
                    p.scene3d -> R.string.plug_kind_shader3d
                    else -> R.string.plug_kind_shader
                }),
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
            PluginTexts.pick(p.descriptions, tag)?.takeIf { it.isNotBlank() }?.let {
                Text(it, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Text)))
            }
            // No modo seguro o aviso do topo já explica: o plugin não está com defeito.
            if (!p.ok && !PluginRuntime.safeMode) Text(stringResource(R.string.plug_error, p.error), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Warning)))
            else if (p.ok && p.testRenderMs > 0) Text(stringResource(if (p.kind == "audio") R.string.plug_audio_ms else R.string.plug_render_ms, String.format(java.util.Locale.ROOT, "%.1f", p.testRenderMs)),
                style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = AureaColors.Muted)))
            // 0.3.4 plugins: ligar/desligar, permissões, painéis e console.
            if (p.ok && (p.hasScript || p.panels.isNotEmpty() || p.actions.isNotEmpty())) PluginV2Controls(p, tag)
            if (onOpenEffect != null) {
                Box(
                    Modifier.heightIn(min = 44.dp).testTag("plugins.effect.${p.id}")
                        .tocavel(shrink = 1f, onClick = onOpenEffect),
                    contentAlignment = Alignment.CenterStart,
                ) {
                    Text(stringResource(R.string.fxo_add), style = AureaType.of(14f, FontWeight.SemiBold, color = AureaColors.Accent))
                }
            }
        }
        Text(stringResource(R.string.plug_remove), modifier = Modifier.padding(start = 10.dp).testTag("plugins.remove.${p.id}").tocavel(onClick = onRemove),
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Danger)))
    }
}

/** 0.3.4 plugins: o que o cartão de um plugin com interface/script ganha. */
private fun replacedPanelName(key: String): Int = when (key) {
    "transform" -> R.string.plugv3_panel_transform
    "effects" -> R.string.plugv3_panel_effects
    "blend" -> R.string.plugv3_panel_blend
    "presets" -> R.string.plugv3_panel_presets
    else -> R.string.plugv3_panel_material
}

@Composable
private fun PluginV2Controls(p: PluginTexts.Plugin, tag: String) {
    Column(Modifier.fillMaxWidth().padding(top = 6.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
        PermissionRow(stringResource(R.string.pv2_enabled), p.enabled, "plugins.enabled.${p.id}") { PluginRuntime.setEnabled(p.id, it) }
        if (p.requested != 0) {
            Text(stringResource(R.string.pv2_permissions), modifier = Modifier.padding(top = 4.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontWeight = FontWeight.W600, color = AureaColors.Muted)))
            if (p.network.isNotEmpty()) PermissionRow(stringResource(R.string.pv2_perm_network, p.network.joinToString(", ")), p.granted and 1 != 0,
                "plugins.perm.network.${p.id}") { PluginRuntime.setPermission(p.id, 1, it) }
            if (p.wantsClipboard) PermissionRow(stringResource(R.string.pv2_perm_clipboard), p.granted and 2 != 0,
                "plugins.perm.clipboard.${p.id}") { PluginRuntime.setPermission(p.id, 2, it) }
            if (p.wantsFiles) PermissionRow(stringResource(R.string.pv2_perm_files), p.granted and 4 != 0,
                "plugins.perm.files.${p.id}") { PluginRuntime.setPermission(p.id, 4, it) }
        }
        if (p.enabled) {
            p.panels.filter { it.replaces.isNotEmpty() }.forEach { panel ->
                PermissionRow(stringResource(R.string.plugv3_replaces, stringResource(replacedPanelName(panel.replaces))),
                    panel.replaces !in PluginRuntime.replacementsOff, "plugins.replace.${panel.replaces}") { PluginRuntime.setReplacementOff(panel.replaces, !it) }
            }
            p.panels.filter { "editor.panel" in it.placement }.forEach { panel ->
                Text(stringResource(R.string.pv2_open_panel, PluginTexts.pick(panel.title, tag).orEmpty()),
                    modifier = Modifier.padding(vertical = 6.dp).testTag("plugins.open.${p.id}.${panel.id}").tocavel(shrink = 1f) { PluginRuntime.openPanel(p.id, panel.id) },
                    style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Accent)))
            }
            if (p.hasScript) Text(stringResource(R.string.pv2_console), modifier = Modifier.padding(vertical = 6.dp).testTag("plugins.console.${p.id}")
                .tocavel(shrink = 1f) { PluginRuntime.console = p.id },
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Accent)))
        }
    }
}
