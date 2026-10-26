package com.aurea.aurea.home

import android.content.Context
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.os.Build
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.core.content.pm.PackageInfoCompat
import com.aurea.aurea.R
import com.aurea.aurea.diagnostics.StressSheet
import com.aurea.aurea.engine.DeviceProfile
import com.aurea.aurea.engine.ProxyMode
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ProxyState
import com.aurea.aurea.ui.ds.AnchoredMenu
import com.aurea.aurea.ui.ds.AureaDialog
import com.aurea.aurea.ui.ds.AureaDialogDefaults
import com.aurea.aurea.ui.ds.DialogAction
import com.aurea.aurea.ui.ds.DialogRole
import com.aurea.aurea.ui.ds.MenuEntry
import com.aurea.aurea.ui.i18n.AppLanguage
import com.aurea.aurea.ui.i18n.AppText
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

/** Rótulo curto de resolução (720p, 1080p, 4K). */
private fun shortResolution(r: Int) = when (r) {
    720 -> "720p"
    1080 -> "1080p"
    1440 -> "1440p"
    2160 -> "4K"
    else -> "${r}p"
}

/** Versão real do APK. */
internal class AppVersion(val name: String, val build: Long)

internal fun readVersion(context: Context): AppVersion = try {
    val pm = context.packageManager
    val info: PackageInfo = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        pm.getPackageInfo(context.packageName, PackageManager.PackageInfoFlags.of(0))
    } else {
        @Suppress("DEPRECATION")
        pm.getPackageInfo(context.packageName, 0)
    }
    AppVersion(info.versionName ?: "?", PackageInfoCompat.getLongVersionCode(info))
} catch (_: Exception) {
    AppVersion("?", 0)
}

/** Os perfis do criador (os mesmos da tela de boas-vindas da MainActivity). */
internal val CreatorProfiles = listOf(
    Triple("TikTok @ruanzitwo", "https://www.tiktok.com/@ruanzitwo", "home.drawer.follow.tiktok"),
    Triple("Instagram @ofruanzitwo", "https://www.instagram.com/ofruanzitwo/", "home.drawer.follow.instagram"),
)

/**
 * Omega home · P0-3: a GAVETA DE AJUSTES (280 dp, à esquerda) no lugar da
 * antiga aba Ajustes. Título "Ajustes" (17 negrito, 50 dp) e cartões de raio 6:
 *  1. Criar (D4): Importar mídia · Importar arquivo do projeto · AureaHyper;
 *  2. Opções da referência que o AUREA tem (Prévia leve, P2-1);
 *  3. os grupos do AUREA (padrões, idioma, tema, legendas, desempenho, geral);
 *  4. Sobre · Suporte · Acompanhe o criador;
 *  5. doação, licenças, política, faixa beta e ferramentas de desenvolvedor.
 * Abre pelo ☰ e pelo arrasto da borda; fecha no véu, no arrasto e no Voltar.
 */
@Composable
internal fun SettingsDrawer(
    store: EditorStore,
    vm: HomeViewModel,
    onImportMedia: () -> Unit,
    onImportProject: () -> Unit,
    onHyper: () -> Unit,
    onAbout: () -> Unit,
    onSupport: () -> Unit,
    onLicenses: () -> Unit,
) {
    var keyDialog by remember { mutableStateOf(false) }
    var languageMenu by remember { mutableStateOf(false) }
    var stressSheet by remember { mutableStateOf(false) }
    val context = LocalContext.current
    val version = remember { readVersion(context) }
    var taps by remember { mutableIntStateOf(0) }
    if (keyDialog) GroqKeyDialog(store, onDismiss = { keyDialog = false })
    if (stressSheet) {
        StressSheet(store, version.name to version.build, onDismiss = { stressSheet = false })
    }
    val openUrl = { url: String ->
        runCatching { context.startActivity(android.content.Intent(android.content.Intent.ACTION_VIEW, android.net.Uri.parse(url))) }
            .onFailure { store.showToast(context.getString(R.string.social_open_error)) }
        Unit
    }

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(start = 6.dp, end = 6.dp, bottom = 24.dp),
        verticalArrangement = Arrangement.spacedBy(15.dp),
    ) {
        item(key = "titulo") {
            // "Ajustes": 17 negrito numa faixa de 50 dp, 5 dp abaixo do topo, a 11 dp da borda.
            Box(Modifier.padding(start = 5.dp, top = 5.dp).height(50.dp), contentAlignment = Alignment.CenterStart) {
                Text(
                    stringResource(R.string.home_title_settings),
                    style = OmegaHomeType.DrawerTitle,
                    modifier = Modifier.testTag("home.drawer.title").semantics { heading() },
                )
            }
        }
        item(key = "criar") {
            Group {
                TileRow(leading = { DrawerGlyph(CupertinoGlyph.PhotoOnRectangle) }, title = stringResource(R.string.home_import_media),
                    onClick = onImportMedia, tag = "home.drawer.importMedia")
                GroupDivider()
                TileRow(leading = { DrawerGlyph(CupertinoGlyph.ArrowDownToLine) }, title = stringResource(R.string.project_file_import),
                    onClick = onImportProject, tag = "home.drawer.importProject")
                GroupDivider()
                TileRow(leading = { DrawerGlyph(CupertinoGlyph.Bolt) }, title = stringResource(R.string.hyper_title),
                    onClick = onHyper, tag = "home.drawer.hyper")
            }
        }
        item(key = "opcoes") {
            Group {
                SwitchRow(
                    stringResource(R.string.om_drawer_light_preview),
                    stringResource(R.string.om_drawer_light_preview_note),
                    vm.lightPreview,
                    vm::changeLightPreview,
                    tag = "home.drawer.lowPreview",
                )
            }
        }
        item(key = "padroes") {
            GroupHeader(stringResource(R.string.settings_group_defaults))
            Group {
                SegmentedRow(stringResource(R.string.settings_aspect), ProjectPresets.aspects.map { it.key }, vm.defaultAspectKey, { it }, vm::changeDefaultAspect)
                GroupDivider()
                SegmentedRow(stringResource(R.string.settings_resolution), ProjectPresets.resolutions, vm.defaultResolution, ::shortResolution, vm::changeDefaultResolution)
                GroupDivider()
                SegmentedRow(stringResource(R.string.settings_fps), ProjectPresets.fpsOptions, vm.defaultFps, { "$it" }, vm::changeDefaultFps)
                GroupNote(stringResource(R.string.settings_defaults_note))
            }
        }
        item(key = "idioma") {
            GroupHeader(stringResource(R.string.settings_group_language))
            Group {
                val current = AppLanguage.current(context)
                Box {
                    TapRow(
                        stringResource(R.string.settings_language),
                        if (current == AppLanguage.SYSTEM) stringResource(R.string.settings_language_system) else current.display,
                        tag = "home.drawer.language",
                    ) { languageMenu = true }
                    // A lista de idiomas no menu ancorado da fundação. Cada nome
                    // aparece no PRÓPRIO idioma; só "Padrão do sistema" é traduzido.
                    AnchoredMenu(
                        expanded = languageMenu,
                        onDismiss = { languageMenu = false },
                        tag = "home.drawer.language.menu",
                        entries = AppLanguage.entries.map { lang ->
                            MenuEntry.Item(
                                if (lang == AppLanguage.SYSTEM) stringResource(R.string.settings_language_system) else lang.display,
                                checked = lang == current,
                            ) {
                                if (lang != AppLanguage.current(context)) {
                                    AppLanguage.select(context, lang)
                                    // O idioma entra no `attachBaseContext`: recriar aplica a escolha.
                                    (context as? android.app.Activity ?: context.findActivity())?.recreate()
                                }
                            }
                        },
                    )
                }
                GroupNote(stringResource(R.string.settings_language_note))
            }
        }
        item(key = "tema") {
            GroupHeader(stringResource(R.string.settings_group_theme))
            Group {
                ThemePicker(AureaColors.palette.id, vm::changeTheme)
                GroupNote(stringResource(R.string.settings_theme_note))
            }
        }
        item(key = "legendas") {
            GroupHeader(stringResource(R.string.settings_group_captions))
            Group {
                TapRow(
                    stringResource(R.string.settings_groq_key),
                    stringResource(if (store.captions.hasGroqKey) R.string.settings_groq_set else R.string.settings_groq_unset),
                ) { keyDialog = true }
                GroupNote(stringResource(R.string.settings_groq_note))
            }
        }
        item(key = "desempenho") {
            GroupHeader(stringResource(R.string.settings_group_device))
            Group {
                DeviceRow(store)
                GroupNote(stringResource(R.string.settings_device_auto_note))
                GroupDivider()
                ProxyGroup(store)
            }
        }
        item(key = "geral") {
            GroupHeader(stringResource(R.string.settings_group_general))
            Group {
                TapRow(stringResource(R.string.settings_clear_cache), stringResource(R.string.settings_clear_cache_note)) {
                    store.clearCache()
                    store.effectPreviews?.clear()
                }
                GroupDivider()
                // Prévias de efeitos: desligado, os cartões de efeito mostram só a
                // cartela com o ícone da categoria e o motor não renderiza prévia.
                SwitchRow(
                    stringResource(R.string.settings_effect_previews),
                    stringResource(R.string.settings_effect_previews_note),
                    store.effectPrefs.previewsEnabled,
                    store.effectPrefs::updatePreviewsEnabled,
                )
                GroupDivider()
                TapRow(stringResource(R.string.settings_clear_recents), stringResource(R.string.settings_clear_recents_note)) {
                    store.effectPrefs.clearRecents()
                    store.showToast(context.getString(R.string.settings_recents_cleared))
                }
                GroupDivider()
                // O teste de estresse fica aqui, sem esconderijo: quem está com o
                // app travando precisa achar isso sozinho e mandar o relatório.
                TapRow(stringResource(R.string.settings_stress), stringResource(R.string.settings_stress_note)) {
                    stressSheet = true
                }
                // Consentimento de anúncios (UMP/GDPR): só onde a região exige.
                if (remember { com.aurea.aurea.ads.UmpConsent.opcoesDePrivacidadeExigidas(context) }) {
                    GroupDivider()
                    TapRow(stringResource(R.string.settings_ad_privacy), stringResource(R.string.settings_ad_privacy_note)) {
                        (context.findActivity())?.let { com.aurea.aurea.ads.UmpConsent.mostrarOpcoesDePrivacidade(it) }
                    }
                }
            }
        }
        item(key = "sobre") {
            Group { TapRow(stringResource(R.string.settings_group_about), null, tag = "home.drawer.about", onClick = onAbout) }
        }
        item(key = "suporte") {
            Group {
                TapRow(stringResource(R.string.om_drawer_support), stringResource(R.string.report_title), tag = "home.drawer.support", onClick = onSupport)
            }
        }
        item(key = "siga") {
            Group {
                Text(
                    stringResource(R.string.social_title),
                    style = OmegaHomeType.DrawerNote.copy(fontWeight = FontWeight.W600),
                    modifier = Modifier.padding(start = 15.dp, end = 15.dp, top = 12.dp, bottom = 4.dp).semantics { heading() },
                )
                CreatorProfiles.forEachIndexed { i, (label, url, tag) ->
                    if (i > 0) GroupDivider()
                    TileRow(
                        leading = {
                            Box(Modifier.size(38.dp).clip(CircleShape).background(AureaColors.LightInk), contentAlignment = Alignment.Center) {
                                CupertinoIcon(if (i == 0) CupertinoGlyph.MusicNote else CupertinoGlyph.Camera, 18.dp, AureaColors.LightOnInk)
                            }
                        },
                        title = label,
                        trailing = { CupertinoIcon(CupertinoGlyph.ChevronRight, 14.dp, AureaColors.LightTextMuted, Modifier.mirrorInRtl()) },
                        onClick = { openUrl(url) },
                        tag = tag,
                    )
                }
            }
        }
        item(key = "doacoes") { DonationCard() }
        item(key = "mais") {
            Group {
                TapRow(stringResource(R.string.licenses_oss_title), "AureaHyper · FFmpeg · ncnn · MiDaS · U2-Net", onClick = onLicenses)
                GroupDivider()
                // Google Play: a política publicada também fica acessível dentro do app.
                TapRow(stringResource(R.string.settings_privacy_policy), stringResource(R.string.settings_privacy_policy_note)) {
                    openUrl(PRIVACY_POLICY_URL)
                }
            }
        }
        item(key = "beta") {
            BetaBanner(version, onTap = {
                taps++
                if (taps >= 7) {
                    taps = 0
                    val on = vm.toggleDevTools()
                    store.showToast(context.getString(if (on) R.string.settings_dev_on else R.string.settings_dev_off))
                }
            })
            if (vm.devTools) {
                Spacer(Modifier.height(15.dp))
                Group {
                    TileRow(
                        leading = { DrawerGlyph(CupertinoGlyph.Wrench) },
                        title = stringResource(R.string.settings_dev_tools),
                        subtitle = stringResource(R.string.app_settings_version_build, version.name, version.build),
                    )
                }
            }
            Spacer(Modifier.height(15.dp))
            Text(stringResource(R.string.settings_made_by), style = OmegaHomeType.DrawerNote, textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth())
        }
    }
}

/** O ícone de 22 dp das linhas da gaveta (texto principal). */
@Composable
private fun DrawerGlyph(glyph: Char) = CupertinoIcon(glyph, 22.dp, AureaColors.LightText)

/**
 * Título 12 negrito + subtítulo 11 e o interruptor (linha de 44 dp). A linha
 * inteira é o alvo (papel Switch); o desenho do interruptor não repete o nó.
 */
@Composable
private fun SwitchRow(title: String, subtitle: String, checked: Boolean, onChange: (Boolean) -> Unit, tag: String? = null) {
    Row(
        Modifier
            .fillMaxWidth()
            .heightIn(min = OmegaHomeDims.DrawerRow)
            .then(if (tag != null) Modifier.testTag(tag) else Modifier)
            .toggleable(value = checked, role = Role.Switch, onValueChange = onChange)
            .semantics { contentDescription = title }
            .padding(horizontal = 15.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, style = OmegaHomeType.DrawerLabel)
            Spacer(Modifier.height(1.dp))
            Text(subtitle, style = OmegaHomeType.DrawerNote)
        }
        Spacer(Modifier.width(8.dp))
        Box(Modifier.clearAndSetSemantics { }) { AureaSwitch(checked, onChange) }
    }
}

/**
 * O aparelho, medido — não um "otimizado!" sem número atrás. A decisão é do
 * MOTOR; aqui só se mostra o que ele decidiu e se deixa forçar nova medição.
 */
@Composable
private fun DeviceRow(store: EditorStore) {
    val context = LocalContext.current
    val report = store.deviceReport
    TapRow(
        stringResource(R.string.settings_device_auto),
        report?.summary(context) ?: stringResource(R.string.settings_device_auto),
    ) {
        DeviceProfile.forget(context)
        store.showToast(context.getString(R.string.settings_device_analysed))
    }
}

/**
 * "Otimizar vídeos pesados" (0.2.5): o modo do proxy de edição e a limpeza da
 * mídia otimizada. O QUE otimizar é decisão do motor; aqui só a escolha.
 */
@Composable
private fun ProxyGroup(store: EditorStore) {
    val context = LocalContext.current
    val proxies = store.proxies
    LaunchedEffect(Unit) { proxies.refreshCacheSize() }
    val labels = mapOf(
        ProxyMode.AUTOMATIC to stringResource(R.string.proxy_mode_auto),
        ProxyMode.ALWAYS to stringResource(R.string.proxy_mode_always),
        ProxyMode.OFF to stringResource(R.string.proxy_mode_off),
    )
    SegmentedRow(stringResource(R.string.proxy_settings_title), ProxyMode.entries, proxies.mode, { labels.getValue(it) }, proxies::select)
    GroupDivider()
    val bytes = proxies.cacheBytes
    TapRow(
        if (bytes >= 0) stringResource(R.string.proxy_clear_title, ProxyState.megabytes(bytes))
        else stringResource(R.string.proxy_clear_title_unknown),
        stringResource(R.string.proxy_clear_note),
    ) {
        proxies.clearCache { mb -> store.showToast(AppText.get(context, R.string.proxy_cleared, mb)) }
    }
    GroupNote(stringResource(R.string.proxy_settings_note))
}

/**
 * A faixa beta com a versão. Sete toques ligam as ferramentas de
 * desenvolvedor — é o único jeito de chegar nelas.
 */
@Composable
private fun BetaBanner(version: AppVersion, onTap: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(6.dp))
            .background(AureaColors.BetaFill)
            .border(1.dp, AureaColors.BetaBorder, RoundedCornerShape(6.dp))
            .clickable(interactionSource = null, indication = null, onClick = onTap)
            .padding(horizontal = 15.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(CupertinoGlyph.ExclamationmarkTriangle, 18.dp, AureaColors.Beta)
        Spacer(Modifier.width(10.dp))
        Column(Modifier.weight(1f)) {
            Text(stringResource(R.string.settings_beta_title, version.name), style = OmegaHomeType.DrawerLabel)
            Spacer(Modifier.height(2.dp))
            Text(stringResource(R.string.settings_beta_body), style = OmegaHomeType.DrawerNote)
        }
    }
}

/** Colar/trocar/remover a chave da Groq (guardada cifrada, nunca mostrada de volta). */
@Composable
private fun GroqKeyDialog(store: EditorStore, onDismiss: () -> Unit) {
    var key by remember { mutableStateOf("") }
    AureaDialog(
        title = stringResource(R.string.settings_groq_key),
        message = stringResource(R.string.settings_groq_note),
        onDismiss = onDismiss,
        tag = "home.drawer.groq",
        actions = buildList {
            add(DialogAction(stringResource(R.string.common_save), DialogRole.Positive, enabled = key.isNotBlank()) {
                store.captions.setGroqKey(key)
            })
            if (store.captions.hasGroqKey) add(DialogAction(stringResource(R.string.common_remove), DialogRole.Neutral, destructive = true) {
                store.captions.clearGroqKey()
            })
            add(DialogAction(stringResource(R.string.common_cancel), DialogRole.Negative))
        },
    ) {
        SecretDialogField(key, { key = it }, placeholder = "gsk_…")
    }
}

/**
 * O campo do diálogo claro para segredos (mesmo desenho do `AureaDialogField`:
 * 40 dp, raio 6, `LightSurface`), com o texto mascarado.
 */
@Composable
private fun SecretDialogField(value: String, onValueChange: (String) -> Unit, placeholder: String) {
    val style = AureaType.of(15f, color = AureaColors.LightText)
    Box(
        Modifier
            .fillMaxWidth()
            .height(AureaDialogDefaults.FieldHeight)
            .clip(RoundedCornerShape(AureaDialogDefaults.FieldRadius))
            .background(AureaColors.LightSurface)
            .padding(horizontal = 10.dp),
        contentAlignment = Alignment.CenterStart,
    ) {
        if (value.isEmpty()) Text(placeholder, style = style.copy(color = AureaColors.LightTextFaint), maxLines = 1)
        androidx.compose.foundation.text.BasicTextField(
            value = value,
            onValueChange = onValueChange,
            singleLine = true,
            textStyle = style,
            cursorBrush = androidx.compose.ui.graphics.SolidColor(AureaColors.LightAccentInk),
            visualTransformation = PasswordVisualTransformation(),
            modifier = Modifier.fillMaxWidth().testTag("dialog.field"),
        )
    }
}

/** Nome de cada tema (o id é o salvo nos Ajustes). */
@Composable
private fun themeName(id: String): String = stringResource(when (id) {
    "midnight" -> R.string.theme_midnight
    "graphite" -> R.string.theme_graphite
    "emerald" -> R.string.theme_emerald
    "amethyst" -> R.string.theme_amethyst
    "sunset" -> R.string.theme_sunset
    else -> R.string.theme_aurea
})

/**
 * Temas como amostras: o fundo do tema com a cor de destaque por cima, o nome
 * embaixo. Tocar aplica na hora (o app inteiro redesenha), sem confirmar.
 */
@Composable
private fun ThemePicker(current: String, onPick: (String) -> Unit) {
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 12.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        com.aurea.aurea.ui.theme.AureaPalette.all.forEach { palette ->
            val on = palette.id == current
            val name = themeName(palette.id)
            Column(
                Modifier.width(56.dp).semantics { contentDescription = name; selected = on }
                    .tocavel(haptic = true) { onPick(palette.id) },
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Box(
                    Modifier.size(44.dp).clip(CircleShape).background(palette.background)
                        .border(if (on) 3.dp else 1.dp, if (on) AureaColors.LightAccentInk else AureaColors.LightDivider, CircleShape),
                    contentAlignment = Alignment.Center,
                ) {
                    Box(Modifier.size(18.dp).clip(CircleShape).background(palette.accent))
                }
                Spacer(Modifier.height(4.dp))
                Text(name, style = AureaType.of(10f, if (on) FontWeight.W700 else FontWeight.Normal, 0f,
                    if (on) AureaColors.LightText else AureaColors.LightTextMuted), maxLines = 1)
            }
        }
    }
}

/** A política de privacidade publicada (discovery/paginas.js); a mesma URL vai no Play Console. */
internal const val PRIVACY_POLICY_URL = "https://aurea-ai-discovery.aureaapp.workers.dev/privacidade"
