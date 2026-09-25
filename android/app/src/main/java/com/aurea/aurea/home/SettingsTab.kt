package com.aurea.aurea.home

import android.content.Context
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.os.Build
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.core.content.pm.PackageInfoCompat
import com.aurea.aurea.R
import com.aurea.aurea.diagnostics.StressSheet
import com.aurea.aurea.engine.DeviceProfile
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaActionSheet
import com.aurea.aurea.ui.ds.SheetAction
import com.aurea.aurea.ui.i18n.AppLanguage
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaDims
import com.aurea.aurea.ui.theme.AureaShape
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon

private val LayerSeconds = listOf(2, 3, 5)

/** Rótulo curto de resolução (720p, 1080p, 4K). */
private fun shortResolution(r: Int) = when (r) {
    720 -> "720p"
    1080 -> "1080p"
    1440 -> "1440p"
    2160 -> "4K"
    else -> "${r}p"
}

/** Versão real do APK. */
private class AppVersion(val name: String, val build: Long)

private fun readVersion(context: Context): AppVersion = try {
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

/**
 * A ABA AJUSTES: o que o app realmente configura, mais o Sobre no fim.
 *
 * Grupos: Padrões de novos projetos · Idioma · Legendas · Desempenho · Geral ·
 * Sobre. As linhas que só existiam para parecer opção saíram — eram botões que
 * avisavam "em breve". A "Qualidade 3D" era uma delas: virou a linha de
 * Desempenho, que mostra o que o motor MEDIU neste aparelho.
 *
 * Todo o texto vem do catálogo (Fase 8.1): aqui não mora string de interface.
 */
@Composable
internal fun SettingsTab(store: EditorStore, vm: HomeViewModel, listState: LazyListState, bottomBar: Dp) {
    var keyDialog by remember { mutableStateOf(false) }
    var languageSheet by remember { mutableStateOf(false) }
    var stressSheet by remember { mutableStateOf(false) }
    var licenses by remember { mutableStateOf(false) }
    if (licenses) androidx.compose.material3.AlertDialog(
        onDismissRequest = { licenses = false },
        title = { Text(stringResource(R.string.licenses_title)) },
        text = { Text(stringResource(R.string.licenses_ai_body),
            modifier = Modifier.height(420.dp).verticalScroll(rememberScrollState())) },
        confirmButton = { androidx.compose.material3.TextButton(onClick = { licenses = false }) {
            Text(stringResource(R.string.editor_fechar))
        } },
    )
    val context = LocalContext.current
    val version = remember { readVersion(context) }
    var taps by remember { mutableIntStateOf(0) }
    if (keyDialog) GroqKeyDialog(store, onDismiss = { keyDialog = false })
    if (stressSheet) {
        StressSheet(store, version.name to version.build, onDismiss = { stressSheet = false })
    }
    if (languageSheet) {
        LanguageSheet(
            current = AppLanguage.current(context),
            onPick = { chosen ->
                languageSheet = false
                if (chosen != AppLanguage.current(context)) {
                    AppLanguage.select(context, chosen)
                    // O idioma entra no `attachBaseContext`, que só roda na
                    // criação da Activity: recriar é o que aplica a escolha.
                    (context as? android.app.Activity)?.recreate()
                }
            },
            onDismiss = { languageSheet = false },
        )
    }

    LazyColumn(
        state = listState,
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(start = AureaDims.Gutter, top = AureaDims.S3, end = AureaDims.Gutter, bottom = AureaDims.ListEndSpace + bottomBar - AureaDims.TabBarHeight),
    ) {
        item(key = "titulo") {
            Text(stringResource(R.string.home_title_settings), style = AureaType.HeadlineLarge)
            Spacer(Modifier.height(AureaDims.S5))
        }
        item(key = "padroes") {
            GroupHeader(stringResource(R.string.settings_group_defaults))
            Group {
                SegmentedRow(stringResource(R.string.settings_aspect), ProjectPresets.aspects.map { it.key }, vm.defaultAspectKey, { it }, vm::changeDefaultAspect)
                GroupDivider()
                SegmentedRow(stringResource(R.string.settings_resolution), ProjectPresets.resolutions, vm.defaultResolution, ::shortResolution, vm::changeDefaultResolution)
                GroupDivider()
                SegmentedRow(stringResource(R.string.settings_fps), ProjectPresets.fpsOptions, vm.defaultFps, { "$it" }, vm::changeDefaultFps)
            }
            GroupNote(stringResource(R.string.settings_defaults_note))
        }
        item(key = "idioma") {
            Spacer(Modifier.height(AureaDims.S5))
            GroupHeader(stringResource(R.string.settings_group_language))
            Group {
                val current = AppLanguage.current(context)
                TapRow(
                    stringResource(R.string.settings_language),
                    if (current == AppLanguage.SYSTEM) stringResource(R.string.settings_language_system) else current.display,
                ) { languageSheet = true }
            }
            GroupNote(stringResource(R.string.settings_language_note))
        }
        item(key = "legendas") {
            Spacer(Modifier.height(AureaDims.S5))
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
            Spacer(Modifier.height(AureaDims.S5))
            GroupHeader(stringResource(R.string.settings_group_device))
            Group { DeviceRow(store) }
            GroupNote(stringResource(R.string.settings_device_auto_note))
        }
        item(key = "geral") {
            Spacer(Modifier.height(AureaDims.S5))
            GroupHeader(stringResource(R.string.settings_group_general))
            Group {
                TapRow(stringResource(R.string.settings_clear_cache), stringResource(R.string.settings_clear_cache_note)) {
                    store.clearCache()
                    store.effectPreviews?.clear()
                }
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
            }
        }
        item(key = "doacoes") { DonationCard() }
        item(key = "sobre") {
            Spacer(Modifier.height(AureaDims.S5))
            GroupHeader(stringResource(R.string.settings_group_about))
            Group {
                TileRow(
                    leading = { CupertinoIcon(CupertinoGlyph.Bolt, 21.dp, AureaColors.Accent) },
                    title = stringResource(R.string.settings_technology),
                    subtitle = stringResource(R.string.settings_technology_value),
                )
                GroupDivider()
                TileRow(
                    leading = { CupertinoIcon(CupertinoGlyph.PersonCropCircle, 21.dp, AureaColors.Accent) },
                    title = stringResource(R.string.settings_creator),
                    subtitle = stringResource(R.string.home_ruanzitwo_ofruanzitwo_tiktok_ruanzitwo),
                )
                GroupDivider()
                TapRow(stringResource(R.string.licenses_title), "Real-ESRGAN · Tencent/ncnn") { licenses = true }
            }
            Spacer(Modifier.height(AureaDims.S4))
            BetaBanner(version, onTap = {
                taps++
                if (taps >= 7) {
                    taps = 0
                    val on = vm.toggleDevTools()
                    store.showToast(context.getString(if (on) R.string.settings_dev_on else R.string.settings_dev_off))
                }
            })
            if (vm.devTools) {
                Spacer(Modifier.height(AureaDims.S4))
                Group {
                    TileRow(
                        leading = { CupertinoIcon(CupertinoGlyph.Wrench, 21.dp, AureaColors.Accent) },
                        title = stringResource(R.string.settings_dev_tools),
                        subtitle = "Versão ${version.name} · build ${version.build}",
                    )
                }
            }
            Spacer(Modifier.height(AureaDims.S5))
            Text(stringResource(R.string.settings_made_by), style = AureaType.Footer, textAlign = TextAlign.Center, modifier = Modifier.fillMaxWidth())
        }
    }
}

/**
 * O aparelho, medido — não um "otimizado!" sem número atrás.
 *
 * A decisão é do MOTOR (é ele que tem a sondagem e as regras); aqui só se mostra
 * o que ele decidiu e se deixa forçar uma nova medição. Enquanto o motor não
 * subiu a linha repete o título: um número inventado seria pior que nenhum.
 */
@Composable
private fun DeviceRow(store: EditorStore) {
    val context = LocalContext.current
    val report = store.deviceReport
    TapRow(
        stringResource(R.string.settings_device_auto),
        report?.summary() ?: stringResource(R.string.settings_device_auto),
    ) {
        DeviceProfile.forget(context)
        store.showToast(context.getString(R.string.settings_device_analysed))
    }
}

/**
 * A lista de idiomas. Cada nome aparece no PRÓPRIO idioma — "Русский" se escreve
 * assim em qualquer lugar, e é o único jeito de quem fala russo achar o russo
 * numa lista escrita em árabe. Só "Padrão do sistema" é texto traduzido.
 */
@Composable
private fun LanguageSheet(current: AppLanguage, onPick: (AppLanguage) -> Unit, onDismiss: () -> Unit) {
    val actions = AppLanguage.entries.map { lang ->
        val label = if (lang == AppLanguage.SYSTEM) stringResource(R.string.settings_language_system) else lang.display
        SheetAction(if (lang == current) "✓  $label" else label) { onPick(lang) }
    }
    AureaActionSheet(
        title = stringResource(R.string.settings_language),
        actions = actions,
        onDismiss = onDismiss,
    )
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
            .clip(AureaShape.Md)
            .background(AureaColors.BetaFill)
            .border(1.dp, AureaColors.BetaBorder, AureaShape.Md)
            .clickable(interactionSource = null, indication = null, onClick = onTap)
            .padding(14.dp),
        verticalAlignment = androidx.compose.ui.Alignment.CenterVertically,
    ) {
        CupertinoIcon(CupertinoGlyph.ExclamationmarkTriangle, AureaDims.IconMd, AureaColors.Beta)
        Spacer(Modifier.width(10.dp))
        Column(Modifier.weight(1f)) {
            Text(stringResource(R.string.settings_beta_title, version.name), style = AureaType.BetaTitle)
            Spacer(Modifier.height(3.dp))
            Text(stringResource(R.string.settings_beta_body), style = AureaType.BetaBody)
        }
        Spacer(Modifier.width(AureaDims.S2))
        CupertinoIcon(CupertinoGlyph.ChevronRight, 14.dp, AureaColors.Beta)
    }
}

/** Colar/trocar/remover a chave da Groq (guardada cifrada, nunca mostrada de volta). */
@Composable
private fun GroqKeyDialog(store: EditorStore, onDismiss: () -> Unit) {
    var key by remember { mutableStateOf("") }
    androidx.compose.material3.AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.settings_groq_key)) },
        text = {
            Column {
                Text(stringResource(R.string.settings_groq_note), style = AureaType.Note)
                Spacer(Modifier.height(AureaDims.S3))
                androidx.compose.material3.OutlinedTextField(
                    value = key,
                    onValueChange = { key = it },
                    singleLine = true,
                    placeholder = { Text("gsk_…") },
                    visualTransformation = androidx.compose.ui.text.input.PasswordVisualTransformation(),
                )
            }
        },
        confirmButton = {
            androidx.compose.material3.TextButton(enabled = key.isNotBlank(), onClick = { store.captions.setGroqKey(key); onDismiss() }) {
                Text(stringResource(R.string.common_save))
            }
        },
        dismissButton = {
            Row {
                if (store.captions.hasGroqKey) {
                    androidx.compose.material3.TextButton(onClick = { store.captions.clearGroqKey(); onDismiss() }) {
                        Text(stringResource(R.string.common_remove))
                    }
                }
                androidx.compose.material3.TextButton(onClick = onDismiss) { Text(stringResource(R.string.common_cancel)) }
            }
        },
    )
}
