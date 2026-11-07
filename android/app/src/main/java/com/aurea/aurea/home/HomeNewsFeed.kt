package com.aurea.aurea.home

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

/**
 * O Início: "Novidades e notícias". Em cima os avisos do servidor (o mesmo /aviso das
 * faixas), embaixo o que mudou nesta versão. Os avisos não aparecem também como faixa
 * enquanto se está aqui. Par do `HomeNewsFeed` do HomeView.swift.
 */
@Composable
internal fun HomeNewsFeed(service: LiveNotices) {
    val uriHandler = LocalUriHandler.current
    var notesOpen by rememberSaveable { mutableStateOf(false) }
    val notesBody = stringResource(R.string.release_notes_body)
    val (header, pages) = remember(notesBody) { releaseNotePages(notesBody) }
    LazyColumn(
        Modifier.fillMaxSize().testTag("home.news"),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(start = 16.dp, end = 16.dp, top = 8.dp, bottom = 24.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        item {
            Text(stringResource(R.string.home_news_title), style = OmegaHomeType.ListTitle.copy(color = AureaColors.LightText),
                modifier = Modifier.padding(top = 4.dp, bottom = 2.dp).semantics { heading() })
        }
        items(service.visible, key = { it.id }) { notice ->
            val stripe = when (notice.level) {
                "problema" -> Color(0xFFE5484D)
                "atencao" -> Color(0xFFF5A524)
                else -> AureaColors.Accent
            }
            NewsCard(stripe, Modifier.testTag("home.news.notice")) {
                Text(notice.text, style = AureaType.of(15f, color = AureaColors.LightText, lineHeight = 1.4f))
                Row(Modifier.padding(top = 10.dp), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                    notice.link?.let { link ->
                        Text(stringResource(R.string.notice_more), style = AureaType.of(14f, FontWeight.W600, 0f, AureaColors.LightAccentInk),
                            modifier = Modifier.tocavel(shrink = 1f) { runCatching { uriHandler.openUri(link) } })
                    }
                    Text(stringResource(R.string.notice_dismiss), style = AureaType.of(14f, FontWeight.W500, 0f, AureaColors.LightTextMuted),
                        modifier = Modifier.tocavel(shrink = 1f) { service.dismiss(notice.id) })
                }
            }
        }
        item(key = "versao") {
            NewsCard(AureaColors.Accent, Modifier.testTag("home.releaseNotes").tocavel(shrink = 1f) { notesOpen = true }) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    CupertinoIcon(CupertinoGlyph.Sparkles, 18.dp, AureaColors.LightAccentInk)
                    Spacer(Modifier.width(8.dp))
                    Text(stringResource(R.string.home_news_version), style = AureaType.of(16f, FontWeight.W700, 0f, AureaColors.LightText),
                        modifier = Modifier.weight(1f))
                    CupertinoIcon(CupertinoGlyph.ChevronRight, 14.dp, AureaColors.LightTextMuted)
                }
                if (header.isNotEmpty()) Text(header, style = AureaType.of(13f, color = AureaColors.LightTextMuted),
                    modifier = Modifier.padding(top = 4.dp))
                // As três primeiras; o resto na tela cheia das Novidades.
                pages.flatten().take(3).forEach { note ->
                    Row(Modifier.padding(top = 8.dp)) {
                        Text("•", style = AureaType.of(15f, color = AureaColors.LightAccentInk))
                        Spacer(Modifier.width(8.dp))
                        Text(note, style = AureaType.of(14f, color = AureaColors.LightText, lineHeight = 1.4f))
                    }
                }
            }
        }
    }
    if (notesOpen) ReleaseNotesScreen(notesBody) { notesOpen = false }
}

@Composable
private fun NewsCard(stripe: Color, modifier: Modifier = Modifier, content: @Composable () -> Unit) {
    Row(
        modifier
            .fillMaxWidth()
            .height(androidx.compose.foundation.layout.IntrinsicSize.Min)
            .clip(RoundedCornerShape(16.dp))
            .background(AureaColors.LightSurface),
    ) {
        Box(Modifier.width(4.dp).fillMaxHeight().background(stripe))
        Column(Modifier.padding(horizontal = 14.dp, vertical = 14.dp)) { content() }
    }
}
