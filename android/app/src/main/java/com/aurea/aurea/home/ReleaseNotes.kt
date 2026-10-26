package com.aurea.aurea.home

import android.content.Context
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
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
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.BuildConfig
import com.aurea.aurea.R
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

/**
 * As Novidades: o botão ✦ da barra superior (Omega home · P0-1, no lugar do
 * botão extra da referência) e a tela cheia escura (P2-2). Abre sozinha uma vez
 * por versão; a marca de "lida" só é gravada quando a pessoa fecha.
 */
@Composable
internal fun ReleaseNotesEntry() {
    val context = LocalContext.current
    val prefs = remember(context) { context.getSharedPreferences("aurea.releaseNotes", Context.MODE_PRIVATE) }
    val edition = "${BuildConfig.VERSION_NAME}:${BuildConfig.VERSION_CODE}"
    var showing by rememberSaveable(edition) { mutableStateOf(prefs.getString("read", null) != edition) }
    val label = stringResource(R.string.release_notes_title)
    Box(
        Modifier
            .size(44.dp)
            .testTag("home.releaseNotes")
            .semantics { contentDescription = label }
            .tocavel { showing = true },
        contentAlignment = Alignment.Center,
    ) {
        CupertinoIcon(CupertinoGlyph.Sparkles, 22.dp, AureaColors.LightText)
    }
    if (!showing && AureaDonations.launchPromptPending) {
        AureaAlert(
            title = stringResource(R.string.donation_title),
            message = stringResource(R.string.donation_launch_question),
            confirmLabel = stringResource(R.string.donation_launch_yes),
            cancelLabel = stringResource(R.string.donation_launch_later),
            onConfirm = {
                AureaDonations.launchPromptPending = false
                try {
                    context.startActivity(android.content.Intent(android.content.Intent.ACTION_VIEW,
                        android.net.Uri.parse(AureaDonations.PAYPAL)))
                } catch (_: android.content.ActivityNotFoundException) {
                    (context.getSystemService(Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager)
                        .setPrimaryClip(android.content.ClipData.newPlainText("Aurea", AureaDonations.PAYPAL))
                    android.widget.Toast.makeText(context, R.string.donation_copied, android.widget.Toast.LENGTH_SHORT).show()
                }
            },
            onDismiss = { AureaDonations.launchPromptPending = false },
        )
    }
    if (showing) {
        ReleaseNotesScreen(stringResource(R.string.release_notes_body)) {
            prefs.edit().putString("read", edition).apply()
            showing = false
        }
    }
}

/**
 * As notas paginadas por seção: a primeira linha (versão e data) fica no alto
 * de toda página e os itens vão de [NOTES_PER_PAGE] em [NOTES_PER_PAGE].
 */
internal fun releaseNotePages(body: String): Pair<String, List<List<String>>> {
    val lines = body.split('\n').map { it.trim() }.filter { it.isNotEmpty() }
    val header = lines.firstOrNull().orEmpty()
    val items = lines.drop(1).map { it.removePrefix("•").trim() }
    val pages = if (items.isEmpty()) listOf(emptyList()) else items.chunked(NOTES_PER_PAGE)
    return header to pages
}

private const val NOTES_PER_PAGE = 4

@Composable
private fun ReleaseNotesScreen(body: String, onClose: () -> Unit) {
    val (header, pages) = remember(body) { releaseNotePages(body) }
    Dialog(onDismissRequest = onClose, properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        Box(Modifier.fillMaxSize().background(AureaColors.Background).testTag("releaseNotes.screen")) {
            Column(Modifier.fillMaxSize().safeDrawingPadding()) {
                Box(Modifier.fillMaxWidth().height(56.dp)) {
                    Text(
                        stringResource(R.string.release_notes_title),
                        style = AureaType.of(17f, FontWeight.W700, 0f, AureaColors.Text),
                        modifier = Modifier.align(Alignment.Center).semantics { heading() },
                        maxLines = 1,
                    )
                    val close = stringResource(R.string.editor_fechar)
                    Box(
                        Modifier
                            .align(Alignment.CenterEnd)
                            .padding(end = 8.dp)
                            .size(44.dp)
                            .testTag("releaseNotes.close")
                            .semantics { contentDescription = close }
                            .tocavel(onClick = onClose),
                        contentAlignment = Alignment.Center,
                    ) {
                        CupertinoIcon(CupertinoGlyph.Xmark, 20.dp, AureaColors.Text)
                    }
                }
                // A versão e a data ficam no alto, fora das páginas (um nó só).
                Text(header, style = AureaType.of(13f, color = AureaColors.Muted), textAlign = TextAlign.Center,
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 24.dp))
                val pager = rememberPagerState { pages.size }
                HorizontalPager(pager, Modifier.weight(1f).fillMaxWidth()) { page ->
                    Column(
                        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 24.dp, vertical = 16.dp),
                        verticalArrangement = Arrangement.spacedBy(14.dp),
                    ) {
                        pages[page].forEach { note ->
                            Row {
                                Text("•", style = AureaType.of(15f, color = AureaColors.Accent))
                                Spacer(Modifier.width(10.dp))
                                Text(note, style = AureaType.of(15f, color = AureaColors.Text, lineHeight = 1.4f))
                            }
                        }
                    }
                }
                // Pontos 30 × 10 a 30 dp da base (um por página).
                if (pages.size > 1) {
                    Row(
                        Modifier.align(Alignment.CenterHorizontally).padding(bottom = 30.dp, top = 8.dp).height(10.dp),
                        horizontalArrangement = Arrangement.spacedBy(6.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        repeat(pages.size) { i ->
                            Box(Modifier.size(if (i == pager.currentPage) 10.dp else 6.dp).clip(CircleShape)
                                .background(if (i == pager.currentPage) AureaColors.Accent else AureaColors.Muted))
                        }
                    }
                } else {
                    Spacer(Modifier.height(30.dp))
                }
            }
        }
    }
}
