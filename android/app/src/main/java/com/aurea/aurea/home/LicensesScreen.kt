package com.aurea.aurea.home

import android.content.res.AssetManager
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
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
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.selection.SelectionContainer
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.R
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

// =============================================================================
//  "Licenças de código aberto" (0.3.2). Lista os avisos que vão DENTRO do app
//  e mostra o texto do próprio arquivo — nada é redigitado aqui:
//    licenses/oss/           engine/assets/licenses (THIRD_PARTY_HYPER.md e
//                            afins), hyper/<modelo>/LICENSE + manifest, ai/,
//                            rotobrush/ (gradle: stageOssLicenses);
//    licenses/native-export/ FFmpeg, OpenH264, Kvazaar (stageNativeExportLicenses);
//    licenses/*.txt, licenses/real-camera/ (glslang, ARCore);
//    LICENSE-*.txt na raiz   as fontes (Roboto, Noto).
//  O par do iOS: LicensesView.swift (mesmas pastas no bundle).
//  Tags: licencas.fechar · licencas.lista · licencas.item.<n> · licencas.texto
// =============================================================================

internal data class LicenseFile(val path: String, val group: Int)

private val LicenseExtensions = listOf(".txt", ".md", ".json")

/** Os arquivos de aviso do APK, em ordem: Hyper, export, IA, demais, fontes. */
internal fun listLicenseFiles(assets: AssetManager): List<LicenseFile> {
    val out = ArrayList<LicenseFile>()
    fun walk(dir: String) {
        val children = runCatching { assets.list(dir) }.getOrNull().orEmpty()
        for (name in children.sorted()) {
            val path = if (dir.isEmpty()) name else "$dir/$name"
            val nested = runCatching { assets.list(path) }.getOrNull().orEmpty()
            if (nested.isNotEmpty()) walk(path)
            else if (LicenseExtensions.any { name.endsWith(it, ignoreCase = true) } ||
                name.startsWith("LICENSE", ignoreCase = true) || name.startsWith("NOTICE", ignoreCase = true) ||
                name.startsWith("COPYING", ignoreCase = true)) out += LicenseFile(path, licenseGroup(path))
        }
    }
    walk("licenses")
    runCatching { assets.list("") }.getOrNull().orEmpty().filter { it.startsWith("LICENSE-") }.sorted()
        .forEach { out += LicenseFile(it, 4) }
    return out.sortedWith(compareBy({ it.group }, { it.path }))
}

internal fun licenseGroup(path: String): Int = when {
    path.startsWith("licenses/oss/hyper") || path.contains("HYPER", ignoreCase = true) -> 0
    path.startsWith("licenses/native-export") -> 1
    path.startsWith("licenses/oss/ai") || path.startsWith("licenses/oss/rotobrush") -> 2
    path.startsWith("LICENSE-") -> 4
    else -> 3
}

/** "licenses/oss/hyper/span/LICENSE" → "span · LICENSE". */
internal fun licenseTitle(path: String): String {
    val parts = path.removePrefix("licenses/").removePrefix("oss/").removePrefix("native-export/").split('/')
    return parts.joinToString(" · ") { it.substringBeforeLast('.').ifBlank { it } }
}

@Composable
fun LicensesScreen(onDismiss: () -> Unit) {
    val context = LocalContext.current
    var files by remember { mutableStateOf<List<LicenseFile>?>(null) }
    var open by remember { mutableStateOf<LicenseFile?>(null) }
    var text by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(Unit) { files = withContext(Dispatchers.IO) { listLicenseFiles(context.assets) } }
    LaunchedEffect(open) {
        val f = open ?: return@LaunchedEffect
        text = null
        text = withContext(Dispatchers.IO) {
            runCatching { context.assets.open(f.path).bufferedReader(Charsets.UTF_8).use { it.readText() } }.getOrNull()
        } ?: context.getString(R.string.licenses_read_failed)
    }
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        BackHandler { if (open != null) open = null else onDismiss() }
        Column(Modifier.fillMaxSize().background(AureaColors.Background).safeDrawingPadding()) {
            val back = stringResource(R.string.common_back)
            Row(Modifier.fillMaxWidth().height(44.dp).padding(horizontal = 6.dp), verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(40.dp).testTag("licencas.fechar").semantics { contentDescription = back }
                    .tocavel(onClick = { if (open != null) open = null else onDismiss() }), contentAlignment = Alignment.Center) {
                    CupertinoIcon(CupertinoGlyph.ChevronLeft, 22.dp, AureaColors.Text)
                }
                Text(open?.let { licenseTitle(it.path) } ?: stringResource(R.string.licenses_oss_title), style = AureaType.TitleMedium,
                    maxLines = 1, modifier = Modifier.padding(start = 4.dp).semantics { heading() })
            }
            val current = open
            if (current != null) {
                SelectionContainer(Modifier.weight(1f).fillMaxWidth()) {
                    Text(text ?: "…", style = AureaType.Base.merge(androidx.compose.ui.text.TextStyle(fontFamily = FontFamily.Monospace,
                        fontSize = 12.sp, color = AureaColors.Text)),
                        modifier = Modifier.verticalScroll(rememberScrollState()).padding(16.dp).testTag("licencas.texto"))
                }
            } else {
                val list = files
                LazyColumn(Modifier.weight(1f).fillMaxWidth().testTag("licencas.lista")) {
                    item {
                        Text(stringResource(R.string.licenses_oss_intro), style = AureaType.of(13f, color = AureaColors.Muted),
                            modifier = Modifier.padding(horizontal = 18.dp, vertical = 10.dp))
                    }
                    if (list != null && list.isEmpty()) item {
                        Text(stringResource(R.string.licenses_none), style = AureaType.of(14f), modifier = Modifier.padding(18.dp))
                    }
                    itemsIndexed(list.orEmpty()) { i, f ->
                        val previous = list?.getOrNull(i - 1)
                        if (previous == null || previous.group != f.group) {
                            Text(stringResource(when (f.group) {
                                0 -> R.string.licenses_group_hyper
                                1 -> R.string.licenses_group_export
                                2 -> R.string.licenses_group_ai
                                4 -> R.string.licenses_group_fonts
                                else -> R.string.licenses_group_other
                            }), style = AureaType.of(12f, FontWeight.W700, color = AureaColors.Muted),
                                modifier = Modifier.padding(start = 18.dp, end = 18.dp, top = 16.dp, bottom = 6.dp).semantics { heading() })
                        }
                        val title = licenseTitle(f.path)
                        Column(Modifier.fillMaxWidth().heightIn(min = 52.dp).testTag("licencas.item.$i")
                            .semantics { contentDescription = title }
                            .tocavel(onClick = { open = f }).padding(horizontal = 18.dp, vertical = 8.dp)) {
                            Text(title, style = AureaType.of(15f, FontWeight.W500))
                            Text(f.path, style = AureaType.of(11f, color = AureaColors.Muted), maxLines = 1)
                        }
                    }
                    item { Spacer(Modifier.height(24.dp)) }
                }
            }
        }
    }
}
