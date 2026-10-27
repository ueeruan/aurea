package com.aurea.aurea.effects

import android.graphics.BitmapFactory
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.editor.panels.PanelFrame
import com.aurea.aurea.editor.panels.PanelRail
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.ds.AureaModalSheet
import com.aurea.aurea.ui.ds.AureaSlider
import com.aurea.aurea.ui.ds.AureaToggle
import com.aurea.aurea.ui.ds.ChoiceChips
import com.aurea.aurea.ui.ds.ColorPickerSheet
import com.aurea.aurea.ui.ds.ColorWell
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import org.json.JSONArray
import org.json.JSONObject
import java.util.Locale

/**
 * 0.3.4 PLUGINS: os painéis declarados no plugin.json desenhados com os
 * componentes do AUREA (nada de HTML/webview) e os pontos onde eles aparecem:
 * painel no editor (moldura [PanelFrame]), aba da Home, menu de plugins do
 * editor, ⋮ e barra da camada; mais a folha de consentimento, o console e os
 * alertas do script. Ids iguais aos do iOS (PluginsV2.swift):
 * `plugin.panel.<plugin>.<painel>`, `plugin.ctl.<controle>`, `plugin.action.<plugin>.<ação>`.
 */

/** Os nomes de ícone do manifesto → glifos do AUREA (a mesma lista do iOS e do motor). */
internal fun pluginGlyph(name: String): Char = when (name) {
    "sparkles" -> CupertinoGlyph.Sparkles
    "text" -> CupertinoGlyph.Textformat
    "star" -> CupertinoGlyph.Star
    "bolt" -> CupertinoGlyph.Bolt
    "cloud" -> CupertinoGlyph.CloudDownload
    "music" -> CupertinoGlyph.MusicNote
    "photo" -> CupertinoGlyph.Photo
    "film" -> CupertinoGlyph.Film
    "layers" -> CupertinoGlyph.SquareStack3dUp
    "gear" -> CupertinoGlyph.Gear
    "brush" -> CupertinoGlyph.Paintbrush
    "clock" -> CupertinoGlyph.Clock
    "list" -> CupertinoGlyph.LineHorizontal3
    "cube" -> CupertinoGlyph.Cube
    "lightbulb" -> CupertinoGlyph.Lightbulb
    "wrench" -> CupertinoGlyph.Wrench
    "book" -> CupertinoGlyph.Book
    "flag" -> CupertinoGlyph.Flag
    "grid" -> CupertinoGlyph.Grid
    "camera" -> CupertinoGlyph.Camera
    "chart" -> CupertinoGlyph.ChartBarAltFill
    "sliders" -> CupertinoGlyph.SliderHorizontal3
    "eye" -> CupertinoGlyph.Eye
    "folder" -> CupertinoGlyph.Folder
    "link" -> CupertinoGlyph.Link
    "speed" -> CupertinoGlyph.Speedometer
    "waveform" -> CupertinoGlyph.Waveform
    else -> CupertinoGlyph.WandStars
}

@Composable
private fun localeTag(): String = LocalConfiguration.current.locales[0].toLanguageTag()

private fun texts(o: JSONObject?): Map<String, String> {
    if (o == null) return emptyMap()
    val out = LinkedHashMap<String, String>()
    o.keys().forEach { out[it] = o.optString(it) }
    return out
}

private fun hexColor(hex: String): Color = runCatching {
    val h = hex.removePrefix("#")
    val v = h.toLong(16)
    if (h.length == 8) Color(((v and 0xFF) shl 24 or (v ushr 8)).toInt()) else Color((0xFF000000 or v).toInt())
}.getOrDefault(Color.White)

private fun hexOf(r: Float, g: Float, b: Float, a: Float): String {
    fun c(x: Float) = (x.coerceIn(0f, 1f) * 255f + 0.5f).toInt()
    return if (a < 0.999f) String.format(Locale.ROOT, "#%02X%02X%02X%02X", c(r), c(g), c(b), c(a))
    else String.format(Locale.ROOT, "#%02X%02X%02X", c(r), c(g), c(b))
}

private fun numberText(v: Double, step: Double): String {
    val decimals = when {
        step <= 0.0 -> 2
        step >= 1.0 -> 0
        else -> (-kotlin.math.log10(step)).let { kotlin.math.ceil(it).toInt().coerceIn(1, 4) }
    }
    return String.format(Locale.ROOT, "%.${decimals}f", v)
}

private fun strArray(a: JSONArray?): Set<String> = if (a == null) emptySet() else (0 until a.length()).map { a.optString(it) }.toSet()

// =============================================================================
// O painel
// =============================================================================

/** Os controles de um painel do plugin [plugin] (o estado vem do motor). */
@Composable
internal fun PluginPanelContent(plugin: PluginTexts.Plugin, panel: PluginTexts.Panel, modifier: Modifier = Modifier) {
    val tag = localeTag()
    LaunchedEffect(plugin.id) { if (PluginRuntime.states[plugin.id] == null) PluginRuntime.refreshState(plugin.id) }
    val state = PluginRuntime.states[plugin.id]
    val values = state?.optJSONObject("values")
    val disabled = strArray(state?.optJSONArray("disabled"))
    val hidden = strArray(state?.optJSONArray("hidden"))
    val busy = PluginRuntime.busy[plugin.id] == true
    val error = PluginRuntime.errors[plugin.id]
    Column(
        modifier.testTag("plugin.panel.${plugin.id}.${panel.id}"),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        if (busy) {
            val progress = state?.opt("progress") as? Number
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                if (progress != null) LinearProgressIndicator(progress = { progress.toFloat() }, modifier = Modifier.weight(1f), color = AureaColors.Accent)
                else LinearProgressIndicator(modifier = Modifier.weight(1f), color = AureaColors.Accent)
                Text(stringResource(R.string.pv2_cancel_run), modifier = Modifier.testTag("plugin.cancel").tocavel { PluginRuntime.cancel(plugin.id) },
                    style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Danger)))
            }
            state?.optString("progressText")?.takeIf { it.isNotEmpty() }?.let {
                Text(it, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
            }
        }
        if (error != null) {
            Column(
                Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp)).background(AureaColors.Danger.copy(alpha = 0.12f)).padding(10.dp)
                    .testTag("plugin.error"),
                verticalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                Text(stringResource(R.string.pv2_error_title), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600, color = AureaColors.Danger)))
                Text(error, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp)))
                Text(stringResource(R.string.pv2_see_console), modifier = Modifier.tocavel(shrink = 1f) { PluginRuntime.console = plugin.id },
                    style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Accent)))
            }
        }
        panel.controls.forEach { c ->
            if (c.id in hidden) return@forEach
            val enabled = !busy && c.id !in disabled
            val label = state?.optJSONObject("labels")?.optString(c.id)?.takeIf { it.isNotEmpty() } ?: PluginTexts.pick(c.label, tag).orEmpty()
            Box(Modifier.fillMaxWidth().alpha(if (enabled) 1f else 0.4f).testTag("plugin.ctl.${c.id}")) {
                PluginControl(plugin.id, c, label, values, state, enabled, tag)
            }
        }
    }
}

@Composable
private fun ControlLabel(text: String, trailing: String? = null) {
    if (text.isEmpty() && trailing == null) return
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(text, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Muted)))
        if (trailing != null) Text(trailing, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
    }
}

@Composable
private fun PluginControl(
    id: String,
    c: PluginTexts.Control,
    label: String,
    values: JSONObject?,
    state: JSONObject?,
    enabled: Boolean,
    tag: String,
) {
    when (c.type) {
        "button" -> {
            val bg = when (c.style) { "primary" -> AureaColors.Accent; else -> AureaColors.Chip }
            val ink = when (c.style) { "primary" -> AureaColors.OnAccent; "danger" -> AureaColors.Danger; else -> AureaColors.Text }
            Box(
                Modifier.fillMaxWidth().heightIn(min = 44.dp).clip(RoundedCornerShape(12.dp)).background(bg)
                    .tocavel(enabled = enabled) { PluginRuntime.event(id, c.id, "click") },
                contentAlignment = Alignment.Center,
            ) {
                Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600, color = ink)))
            }
        }
        "slider" -> {
            val stored = values?.optDouble(c.id, c.min) ?: c.min
            var live by remember(c.id) { mutableStateOf<Float?>(null) }
            val shown = live?.toDouble() ?: stored
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                ControlLabel(label, numberText(shown, c.step) + if (c.unit.isNotEmpty()) " ${c.unit}" else "")
                AureaSlider(
                    value = shown.toFloat(), min = c.min.toFloat(), max = c.max.toFloat(), step = c.step.toFloat(),
                    origin = c.min.toFloat(), snap = null,
                    onStart = {}, onValue = { live = it },
                    onEnd = {
                        live?.let { PluginRuntime.event(id, c.id, "change", it.toString()) }
                        live = null
                    },
                    enabled = enabled, a11yLabel = label, a11yValue = numberText(shown, c.step),
                )
            }
        }
        "ruler" -> {
            val stored = values?.optDouble(c.id, 0.0) ?: 0.0
            var live by remember(c.id) { mutableStateOf<Double?>(null) }
            val shown = live ?: stored
            val storedNow by rememberUpdatedState(stored)
            val perDp = if (c.step > 0) c.step / 4.0 else 0.05
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                ControlLabel(label, numberText(shown, c.step) + if (c.unit.isNotEmpty()) " ${c.unit}" else "")
                Box(
                    Modifier.fillMaxWidth().height(44.dp).clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip)
                        .semantics { contentDescription = label }
                        .pointerInput(c.id, enabled) {
                            if (!enabled) return@pointerInput
                            var acc = 0.0
                            detectHorizontalDragGestures(
                                onDragStart = { acc = storedNow },
                                onDragEnd = { live?.let { PluginRuntime.event(id, c.id, "change", it.toString()) }; live = null },
                                onDragCancel = { live = null },
                            ) { _, dx ->
                                acc = (acc - dx / density * perDp).coerceIn(c.min, c.max)
                                live = acc
                            }
                        },
                    contentAlignment = Alignment.Center,
                ) {
                    com.aurea.aurea.ui.ds.TickRuler(value = { (shown / perDp).toFloat() }, unitsPerDp = 1f, active = live != null,
                        modifier = Modifier.fillMaxSize())
                    Box(Modifier.width(2.dp).fillMaxSize().background(AureaColors.Accent))
                }
            }
        }
        "color" -> {
            val hex = values?.optString(c.id).orEmpty().ifEmpty { "#FFFFFF" }
            var picking by remember(c.id) { mutableStateOf(false) }
            Row(Modifier.fillMaxWidth().heightIn(min = 44.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(label, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp)))
                ColorWell(hexColor(hex)) { if (enabled) picking = true }
            }
            if (picking) {
                val color = hexColor(hex)
                var chosen by remember { mutableStateOf(hex) }
                ColorPickerSheet(
                    initial = floatArrayOf(color.red, color.green, color.blue, color.alpha),
                    onChange = { r, g, b, a -> chosen = hexOf(r, g, b, a) },
                    onDone = {
                        picking = false
                        PluginRuntime.event(id, c.id, "change", JSONObject.quote(chosen))
                    },
                )
            }
        }
        "choice" -> {
            val selected = values?.optInt(c.id, 0) ?: 0
            Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                ControlLabel(label)
                ChoiceChips(
                    options = c.options.map { PluginTexts.pick(it, tag).orEmpty() },
                    selected = selected,
                    onSelect = { if (enabled) PluginRuntime.event(id, c.id, "change", it.toString()) },
                )
            }
        }
        "text" -> {
            val stored = values?.optString(c.id).orEmpty()
            var draft by remember(c.id, stored) { mutableStateOf(stored) }
            val send = { if (draft != stored) PluginRuntime.event(id, c.id, "change", JSONObject.quote(draft)) }
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                ControlLabel(label)
                Box(
                    Modifier.fillMaxWidth().heightIn(min = 44.dp).clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip)
                        .padding(horizontal = 12.dp, vertical = 10.dp),
                    contentAlignment = Alignment.CenterStart,
                ) {
                    if (draft.isEmpty()) Text(PluginTexts.pick(c.placeholder, tag).orEmpty(),
                        style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Muted)))
                    BasicTextField(
                        value = draft,
                        onValueChange = { if (it.length <= c.maxLength) draft = it },
                        enabled = enabled,
                        singleLine = !c.multiline,
                        textStyle = AureaType.Base.merge(TextStyle(fontSize = 15.sp)),
                        cursorBrush = SolidColor(AureaColors.Accent),
                        keyboardOptions = KeyboardOptions(imeAction = if (c.multiline) ImeAction.Default else ImeAction.Done),
                        keyboardActions = KeyboardActions(onDone = { send() }),
                        modifier = Modifier.fillMaxWidth().onFocusChanged { if (!it.isFocused) send() },
                    )
                }
            }
        }
        "list" -> {
            val items = state?.optJSONObject("items")?.optJSONArray(c.id) ?: JSONArray()
            val selected = values?.opt(c.id) as? String
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                ControlLabel(label)
                for (i in 0 until items.length()) {
                    val item = items.optJSONObject(i) ?: continue
                    val itemId = item.optString("id")
                    val itemLabel = (item.opt("label") as? JSONObject)?.let { PluginTexts.pick(texts(it), tag) } ?: item.optString("label")
                    val detail = (item.opt("detail") as? JSONObject)?.let { PluginTexts.pick(texts(it), tag) } ?: item.optString("detail")
                    val on = itemId == selected
                    Column(
                        Modifier.fillMaxWidth().heightIn(min = 44.dp).clip(RoundedCornerShape(10.dp))
                            .background(if (on) AureaColors.Accent.copy(alpha = 0.18f) else AureaColors.Chip)
                            .then(if (on) Modifier.border(1.dp, AureaColors.Accent, RoundedCornerShape(10.dp)) else Modifier)
                            .testTag("plugin.item.${c.id}.$itemId")
                            .tocavel(enabled = enabled) { PluginRuntime.event(id, c.id, "select", JSONObject.quote(itemId)) }
                            .padding(horizontal = 12.dp, vertical = 8.dp),
                        verticalArrangement = Arrangement.Center,
                    ) {
                        Text(itemLabel.orEmpty(), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp)))
                        if (!detail.isNullOrEmpty()) Text(detail, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
                    }
                }
            }
        }
        "image" -> {
            val path = state?.optJSONObject("images")?.optString(c.id).orEmpty()
            val bitmap = remember(path) { if (path.isEmpty()) null else runCatching { BitmapFactory.decodeFile(path)?.asImageBitmap() }.getOrNull() }
            Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                ControlLabel(label)
                Box(
                    Modifier.fillMaxWidth().aspectRatio(if (c.aspect > 0f) c.aspect else 16f / 9f).clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip),
                    contentAlignment = Alignment.Center,
                ) {
                    if (bitmap != null) Image(bitmap, contentDescription = label, contentScale = ContentScale.Fit, modifier = Modifier.fillMaxSize())
                    else CupertinoIcon(CupertinoGlyph.Photo, 24.dp, AureaColors.Muted)
                }
            }
        }
        "label" -> {
            val texts = state?.optJSONObject("texts")
            val text = if (texts != null && texts.has(c.id)) texts.optString(c.id) else PluginTexts.pick(c.text, tag).orEmpty()
            if (label.isNotEmpty()) ControlLabel(label)
            Text(text, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, lineHeight = 18.sp, color = AureaColors.Text)))
        }
        "toggle" -> {
            val on = values?.optBoolean(c.id) == true
            Row(Modifier.fillMaxWidth().heightIn(min = 44.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(label, modifier = Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp)))
                AureaToggle(checked = on, onCheckedChange = { PluginRuntime.event(id, c.id, "change", it.toString()) }, enabled = enabled)
            }
        }
    }
}

// =============================================================================
// Onde os painéis aparecem
// =============================================================================

/**
 * O painel aberto no editor, por cima da zona inferior, dentro da moldura
 * comum dos painéis. Chamado em EditorScreen (`// 0.3.4 plugins:`).
 */
@Composable
internal fun PluginEditorPanelHost(store: EditorStore, modifier: Modifier = Modifier) {
    PluginOverlays(store)
    val open = PluginRuntime.editorPanel ?: return
    val plugin = store.plugins.firstOrNull { it.id == open.first && it.ok && it.enabled }
    val panel = plugin?.panels?.firstOrNull { it.id == open.second }
    if (plugin == null || panel == null) {
        LaunchedEffect(open) { PluginRuntime.closeEditorPanel() }
        return
    }
    val tag = localeTag()
    Box(modifier.fillMaxWidth().height(300.dp).background(AureaColors.Background)) {
        PanelFrame(rail = PanelRail(onBack = { PluginRuntime.closeEditorPanel() }, supportsAnimation = false)) {
            Column(Modifier.padding(horizontal = 14.dp, vertical = 10.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    CupertinoIcon(pluginGlyph(panel.icon), 18.dp, AureaColors.Accent)
                    Text(PluginTexts.pick(panel.title, tag).orEmpty(), modifier = Modifier.weight(1f),
                        style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600)))
                }
                PluginPanelContent(plugin, panel)
            }
        }
    }
}

/** Alertas, console e o seletor de arquivo pedidos pelos scripts (em qualquer tela com plugins). */
@Composable
internal fun PluginOverlays(@Suppress("UNUSED_PARAMETER") store: EditorStore) {
    PluginRuntime.alert?.let { text ->
        AureaAlert(title = text, showCancel = false, onConfirm = {}, onDismiss = { PluginRuntime.alert = null })
    }
    PluginRuntime.console?.let { id -> PluginConsoleSheet(id) { PluginRuntime.console = null } }
    PluginRuntime.pendingInstall?.let { (_, info) -> PluginConsentSheet(info) }
    val request = PluginHost.pickRequest
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        PluginHost.pickRequest?.let { PluginHost.completePick(it, uri) }
    }
    LaunchedEffect(request) {
        if (request != null) runCatching { picker.launch(PluginHost.mimeTypes(request.types)) }.onFailure { PluginHost.completePick(request, null) }
    }
}

/** As entradas do menu de plugins do editor (painéis e ações "editor.menu"). */
@Composable
internal fun pluginEditorMenuEntries(store: EditorStore): List<MenuEntry> {
    val tag = localeTag()
    val out = ArrayList<MenuEntry>()
    store.plugins.filter { it.ok && it.enabled }.forEach { p ->
        p.panels.filter { "editor.panel" in it.placement }.forEach { panel ->
            out += MenuEntry.Item(PluginTexts.pick(panel.title, tag).orEmpty(), glyph = pluginGlyph(panel.icon),
                sublabel = PluginTexts.pick(p.names, tag), tag = "plugin.open.${p.id}.${panel.id}") { PluginRuntime.openPanel(p.id, panel.id) }
        }
        p.actions.filter { "editor.menu" in it.placement && it.panel.isEmpty() }.forEach { a ->
            out += MenuEntry.Item(PluginTexts.pick(a.title, tag).orEmpty(), glyph = pluginGlyph(a.icon),
                sublabel = PluginTexts.pick(p.names, tag), tag = "plugin.action.${p.id}.${a.id}") { PluginRuntime.runAction(p.id, a) }
        }
    }
    return out
}

/** As ações "layer.menu" no ⋮ da camada (Menus.kt). */
@Composable
internal fun pluginLayerMenuEntries(store: EditorStore): List<MenuEntry> {
    val tag = localeTag()
    val out = ArrayList<MenuEntry>()
    store.plugins.filter { it.ok && it.enabled }.forEach { p ->
        p.actions.filter { "layer.menu" in it.placement }.forEach { a ->
            out += MenuEntry.Item(PluginTexts.pick(a.title, tag).orEmpty(), glyph = pluginGlyph(a.icon),
                tag = "plugin.action.${p.id}.${a.id}") { PluginRuntime.runAction(p.id, a) }
        }
    }
    if (out.isNotEmpty()) out.add(0, MenuEntry.Divider)
    return out
}

/** O botão de quebra-cabeça do topo do editor: o menu de plugins. Só aparece com plugin de painel/ação ligado. */
@Composable
internal fun PluginEditorMenuButton(store: EditorStore, height: androidx.compose.ui.unit.Dp) {
    val entries = pluginEditorMenuEntries(store)
    if (entries.isEmpty()) return
    var open by remember { mutableStateOf(false) }
    val label = stringResource(R.string.pv2_menu)
    Box {
        Box(
            Modifier.size(width = 44.dp, height = height).testTag("plugin.menu").semantics { contentDescription = label }
                .tocavel { if (store.playing) store.pause(); open = true },
            contentAlignment = Alignment.Center,
        ) {
            CupertinoIcon(CupertinoGlyph.Wrench, 20.dp, AureaColors.Text)
        }
        com.aurea.aurea.ui.ds.AnchoredMenu(expanded = open, onDismiss = { open = false }, entries = entries, alignEnd = true, tag = "plugin.menu.list")
    }
}

/** Botões "layer.toolbar" na barra da camada (TopBars.kt). */
@Composable
internal fun PluginLayerBarButtons(store: EditorStore, height: androidx.compose.ui.unit.Dp) {
    val tag = localeTag()
    store.plugins.filter { it.ok && it.enabled }.forEach { p ->
        p.actions.filter { "layer.toolbar" in it.placement }.forEach { a ->
            val title = PluginTexts.pick(a.title, tag).orEmpty()
            Box(
                Modifier.size(width = 44.dp, height = height).testTag("plugin.action.${p.id}.${a.id}")
                    .semantics { contentDescription = title }
                    .tocavel(enabled = PluginRuntime.busy[p.id] != true) { PluginRuntime.runAction(p.id, a) },
                contentAlignment = Alignment.Center,
            ) {
                CupertinoIcon(pluginGlyph(a.icon), 20.dp, AureaColors.Text)
            }
        }
    }
}

/** A aba de plugins da Home: o painel de uma aba "home.tab" (seletor no topo se houver várias). */
@Composable
internal fun PluginHomeTab(store: EditorStore) {
    PluginOverlays(store)
    val tag = localeTag()
    val tabs = store.plugins.filter { it.ok && it.enabled }.flatMap { p -> p.panels.filter { "home.tab" in it.placement }.map { p to it } }
    if (tabs.isEmpty()) {
        Box(Modifier.fillMaxSize().padding(24.dp), contentAlignment = Alignment.Center) {
            Text(stringResource(R.string.pv2_menu_empty), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Muted)))
        }
        return
    }
    val current = tabs.firstOrNull { (p, panel) -> PluginRuntime.homePanel == p.id to panel.id } ?: tabs.first()
    LaunchedEffect(current.first.id, current.second.id) {
        if (PluginRuntime.homePanel != current.first.id to current.second.id) PluginRuntime.openPanel(current.first.id, current.second.id, inEditor = false)
    }
    Column(Modifier.fillMaxSize().background(AureaColors.Background).testTag("home.plugins")) {
        if (tabs.size > 1) {
            ChoiceChips(
                options = tabs.map { (_, panel) -> PluginTexts.pick(panel.title, tag).orEmpty() },
                selected = tabs.indexOf(current),
                onSelect = { i -> tabs.getOrNull(i)?.let { (p, panel) -> PluginRuntime.openPanel(p.id, panel.id, inEditor = false) } },
                modifier = Modifier.padding(horizontal = 16.dp),
            )
        }
        Column(Modifier.weight(1f).verticalScroll(rememberScrollState()).padding(16.dp)) {
            Text(PluginTexts.pick(current.second.title, tag).orEmpty(), style = AureaType.Base.merge(TextStyle(fontSize = 20.sp, fontWeight = FontWeight.W600)))
            Spacer(Modifier.height(12.dp))
            PluginPanelContent(current.first, current.second)
        }
    }
}

/** Título e ícone do item do dock (a única aba de plugin dá o nome; várias = "Plugins"). */
@Composable
internal fun pluginHomeTabLabel(store: EditorStore): Pair<String, Char>? {
    val tag = localeTag()
    val tabs = store.plugins.filter { it.ok && it.enabled }.flatMap { p -> p.panels.filter { "home.tab" in it.placement } }
    if (tabs.isEmpty()) return null
    if (tabs.size == 1) return (PluginTexts.pick(tabs[0].title, tag) ?: "") to pluginGlyph(tabs[0].icon)
    return stringResource(R.string.pv2_menu) to CupertinoGlyph.Wrench
}

/** Id da aba de plugins no dock da Home (fora dos ids do HomeViewModel). */
internal const val PLUGIN_HOME_TAB = 77

// =============================================================================
// Consentimento e console
// =============================================================================

@Composable
private fun PluginConsentSheet(info: JSONObject) {
    val tag = localeTag()
    val perms = info.optJSONObject("permissions") ?: JSONObject()
    val network = perms.optJSONArray("network")?.let { a -> List(a.length()) { a.optString(it) } } ?: emptyList()
    var allowNetwork by remember(info) { mutableStateOf(true) }
    var allowClip by remember(info) { mutableStateOf(true) }
    var allowFiles by remember(info) { mutableStateOf(true) }
    val name = PluginTexts.pick(texts(info.optJSONObject("name")), tag) ?: info.optString("id")
    AureaModalSheet(onDismiss = { PluginRuntime.cancelInstall() }) {
        Column(
            Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(horizontal = 18.dp, vertical = 8.dp).testTag("plugins.consent"),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text(stringResource(R.string.pv2_consent_title, name), style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600)))
            Text(stringResource(R.string.plug_by, info.optString("author").ifEmpty { info.optString("id") }, info.optString("version")),
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
            PluginTexts.pick(texts(info.optJSONObject("description")), tag)?.takeIf { it.isNotBlank() }?.let {
                Text(it, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
            }
            info.optString("installedVersion").takeIf { it.isNotEmpty() }?.let {
                Text(stringResource(R.string.pv2_consent_update, it), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Warning)))
            }
            val any = network.isNotEmpty() || perms.optBoolean("clipboard") || perms.optBoolean("files")
            Text(stringResource(if (any) R.string.pv2_consent_body else R.string.pv2_consent_none),
                style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600)))
            if (network.isNotEmpty()) PermissionRow(stringResource(R.string.pv2_perm_network, network.joinToString(", ")), allowNetwork, "plugins.consent.network") { allowNetwork = it }
            if (perms.optBoolean("clipboard")) PermissionRow(stringResource(R.string.pv2_perm_clipboard), allowClip, "plugins.consent.clipboard") { allowClip = it }
            if (perms.optBoolean("files")) PermissionRow(stringResource(R.string.pv2_perm_files), allowFiles, "plugins.consent.files") { allowFiles = it }
            if (info.optBoolean("hasScript")) Text(stringResource(R.string.pv2_consent_script), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Box(
                    Modifier.weight(1f).height(46.dp).clip(RoundedCornerShape(12.dp)).background(AureaColors.Chip).testTag("plugins.consent.cancel")
                        .tocavel { PluginRuntime.cancelInstall() },
                    contentAlignment = Alignment.Center,
                ) { Text(stringResource(R.string.common_cancel), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp))) }
                Box(
                    Modifier.weight(1f).height(46.dp).clip(RoundedCornerShape(12.dp)).background(AureaColors.Accent).testTag("plugins.consent.install")
                        .tocavel {
                            val mask = (if (allowNetwork && network.isNotEmpty()) 1 else 0) or
                                (if (allowClip && perms.optBoolean("clipboard")) 2 else 0) or (if (allowFiles && perms.optBoolean("files")) 4 else 0)
                            PluginRuntime.confirmInstall(mask)
                        },
                    contentAlignment = Alignment.Center,
                ) { Text(stringResource(R.string.pv2_install), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600, color = AureaColors.OnAccent))) }
            }
            Spacer(Modifier.height(8.dp))
        }
    }
}

@Composable
internal fun PermissionRow(text: String, on: Boolean, testTag: String, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().heightIn(min = 44.dp).testTag(testTag), verticalAlignment = Alignment.CenterVertically) {
        Text(text, modifier = Modifier.weight(1f).padding(end = 10.dp), style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)))
        AureaToggle(checked = on, onCheckedChange = onChange)
    }
}

@Composable
private fun PluginConsoleSheet(id: String, onDismiss: () -> Unit) {
    val tag = localeTag()
    var version by remember { mutableFloatStateOf(0f) }
    val lines = remember(id, version) { PluginRuntime.consoleLines(id) }
    val name = PluginRuntime.plugin(id)?.let { PluginTexts.pick(it.names, tag) } ?: id
    AureaModalSheet(onDismiss = onDismiss) {
        Column(Modifier.fillMaxWidth().padding(horizontal = 18.dp, vertical = 8.dp).testTag("plugins.console"), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.pv2_console_title, name), modifier = Modifier.weight(1f),
                    style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, fontWeight = FontWeight.W600)))
                Text(stringResource(R.string.pv2_console_clear), modifier = Modifier.testTag("plugins.console.clear").tocavel {
                    PluginRuntime.clearConsole(id)
                    version += 1f
                }, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Accent)))
            }
            Column(
                Modifier.fillMaxWidth().heightIn(min = 120.dp, max = 420.dp).clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip)
                    .verticalScroll(rememberScrollState()).padding(10.dp),
                verticalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                if (lines.isEmpty()) Text(stringResource(R.string.pv2_console_empty), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
                lines.forEach { (level, text) ->
                    Text(text, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace,
                        color = when (level) { 3 -> AureaColors.Danger; 2 -> AureaColors.Warning; else -> AureaColors.Text })))
                }
            }
            Text(stringResource(R.string.pv2_reload), modifier = Modifier.testTag("plugins.console.reload").tocavel {
                PluginRuntime.reload(id)
                version += 1f
            }, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Accent)))
            Spacer(Modifier.height(12.dp))
        }
    }
}
