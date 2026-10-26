package com.aurea.aurea.home

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.os.Build
import android.os.StatFs
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.BuildConfig
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.i18n.AppLanguage
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

/** Uma linha da tabela do Sobre (rótulo do catálogo, valor medido). */
internal data class AboutRow(val label: String, val value: String)

/** A tabela em texto puro (o que vai para a área de transferência). */
internal fun aboutPlainText(rows: List<AboutRow>): String = rows.joinToString("\n") { "${it.label}: ${it.value}" }

/**
 * Omega home · P1-7: a tela Sobre (tela cheia, azul-marinho da Omega): a marca,
 * a tabela rótulo → valor do aparelho (versão, compilação, aparelho,
 * processador, sistema, memória, armazenamento, exportação máxima, idiomas) e
 * o rodapé com a política de privacidade e as licenças. Tocar na tabela copia
 * tudo (para relatar um problema).
 */
@Composable
internal fun AboutScreen(store: EditorStore, onLicenses: () -> Unit, onClose: () -> Unit) {
    val context = LocalContext.current
    val rows = aboutRows(context, store)
    val copied = stringResource(R.string.om_about_copied)
    Dialog(onDismissRequest = onClose, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        Box(Modifier.fillMaxSize().background(AureaColors.EditorPanel).testTag("about.screen")) {
            Column(Modifier.fillMaxSize().safeDrawingPadding()) {
                Box(Modifier.fillMaxWidth().height(56.dp)) {
                    Text(
                        stringResource(R.string.settings_group_about),
                        style = AureaType.of(17f, FontWeight.W700, 0f, AureaColors.Text),
                        modifier = Modifier.align(Alignment.Center).semantics { heading() },
                    )
                    val close = stringResource(R.string.editor_fechar)
                    Box(
                        Modifier.align(Alignment.CenterEnd).padding(end = 8.dp).size(44.dp).testTag("about.close")
                            .semantics { contentDescription = close }.tocavel(onClick = onClose),
                        contentAlignment = Alignment.Center,
                    ) { CupertinoIcon(CupertinoGlyph.Xmark, 20.dp, AureaColors.Text) }
                }
                Column(
                    Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 24.dp, vertical = 16.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                ) {
                    AureaLogo(72.dp)
                    Spacer(Modifier.height(12.dp))
                    Text("AUREA", style = AureaType.of(24f, FontWeight.W700, 2f, AureaColors.Text))
                    Spacer(Modifier.height(4.dp))
                    Text(stringResource(R.string.settings_made_by), style = AureaType.of(12f, color = AureaColors.Muted), textAlign = TextAlign.Center)
                    Spacer(Modifier.height(24.dp))
                    Column(
                        Modifier
                            .widthIn(max = 480.dp)
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(12.dp))
                            .background(AureaColors.Surface)
                            .testTag("about.table")
                            .tocavel(shrink = 1f) {
                                (context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager)
                                    .setPrimaryClip(ClipData.newPlainText("Aurea", aboutPlainText(rows)))
                                store.showToast(copied)
                            }
                            .padding(vertical = 6.dp),
                    ) {
                        rows.forEach { r ->
                            Row(Modifier.fillMaxWidth().heightIn(min = 32.dp).padding(horizontal = 14.dp, vertical = 5.dp),
                                verticalAlignment = Alignment.CenterVertically) {
                                Text(r.label, style = AureaType.of(13f, color = AureaColors.Muted), modifier = Modifier.width(120.dp))
                                Spacer(Modifier.width(8.dp))
                                Text(r.value, style = AureaType.of(13f, FontWeight.W500, 0f, AureaColors.Text), textAlign = TextAlign.End,
                                    modifier = Modifier.weight(1f))
                            }
                        }
                    }
                    Spacer(Modifier.height(8.dp))
                    Text(stringResource(R.string.om_about_copy_hint), style = AureaType.of(11f, color = AureaColors.Subtle))
                    Spacer(Modifier.height(24.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(20.dp)) {
                        FooterLink(stringResource(R.string.settings_privacy_policy), "about.privacy") {
                            runCatching {
                                context.startActivity(android.content.Intent(android.content.Intent.ACTION_VIEW, android.net.Uri.parse(PRIVACY_POLICY_URL)))
                            }
                        }
                        FooterLink(stringResource(R.string.licenses_oss_title), "about.licenses", onLicenses)
                    }
                }
            }
        }
    }
}

@Composable
private fun FooterLink(label: String, tag: String, onClick: () -> Unit) {
    Box(Modifier.heightIn(min = 44.dp).testTag(tag).tocavel(shrink = 1f, onClick = onClick), contentAlignment = Alignment.Center) {
        Text(label, style = AureaType.of(12f, color = AureaColors.Accent).copy(textDecoration = TextDecoration.Underline))
    }
}

/** As linhas da tabela, medidas agora (sem rede). */
@Composable
private fun aboutRows(context: Context, store: EditorStore): List<AboutRow> {
    val report = store.deviceReport
    val version = remember { readVersion(context) }
    val labels = listOf(
        stringResource(R.string.om_about_version), stringResource(R.string.om_about_build), stringResource(R.string.om_about_device),
        stringResource(R.string.om_about_chipset), stringResource(R.string.om_about_system), stringResource(R.string.om_about_memory),
        stringResource(R.string.om_about_storage), stringResource(R.string.om_about_export_max), stringResource(R.string.om_about_languages),
    )
    val cores = stringResource(R.string.om_about_cores, Runtime.getRuntime().availableProcessors())
    val storageTemplate = stringResource(R.string.om_about_storage_value)
    val language = AppLanguage.current(context).let { if (it == AppLanguage.SYSTEM) stringResource(R.string.settings_language_system) else it.display }
    return remember(report, language) {
        val chip = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) Build.SOC_MODEL.takeIf { it.isNotBlank() && it != Build.UNKNOWN } else null
        val stat = runCatching { StatFs(context.filesDir.absolutePath) }.getOrNull()
        val storage = stat?.let { String.format(storageTemplate, formatFileSize(it.availableBytes), formatFileSize(it.totalBytes)) } ?: "—"
        val ram = report?.totalMemoryMb?.takeIf { it > 0 }?.let { formatFileSize(it.toLong() * 1_000_000L) } ?: "—"
        val export = report?.takeIf { it.maxExportWidth > 0 && it.maxExportHeight > 0 }?.let { "${it.maxExportWidth} × ${it.maxExportHeight}" } ?: "—"
        val locales = context.resources.configuration.locales
        val system = (0 until locales.size()).joinToString(", ") { locales[it].toLanguageTag() }
        listOf(
            AboutRow(labels[0], version.name.removeSuffix("-uitest")),
            AboutRow(labels[1], "${version.build}" + if (BuildConfig.DEBUG) " (debug)" else ""),
            AboutRow(labels[2], "${Build.MANUFACTURER} ${Build.MODEL}".trim()),
            AboutRow(labels[3], listOfNotNull(chip ?: Build.HARDWARE.takeIf { it.isNotBlank() }, cores).joinToString(" · ")),
            AboutRow(labels[4], "Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})"),
            AboutRow(labels[5], ram),
            AboutRow(labels[6], storage),
            AboutRow(labels[7], export),
            AboutRow(labels[8], "$language · $system"),
        )
    }
}
