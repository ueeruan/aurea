package com.aurea.aurea.home

import androidx.compose.foundation.Image
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
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.size
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
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.state.ProjectEntry
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

@Composable
internal fun HomeWorkspace(
    service: LiveNotices,
    projects: List<ProjectEntry>,
    projectsLoaded: Boolean,
    thumbnails: HomeThumbnails,
    onCreate: () -> Unit,
    onProjects: () -> Unit,
    onOpen: (ProjectEntry) -> Unit,
    onMural: () -> Unit,
) {
    val uriHandler = LocalUriHandler.current
    var notesOpen by rememberSaveable { mutableStateOf(false) }
    val recent = remember(projects) { projects.take(3) }
    LazyColumn(
        Modifier.fillMaxSize().testTag("home.news"),
        contentPadding = androidx.compose.foundation.layout.PaddingValues(start = 16.dp, end = 16.dp, top = 12.dp, bottom = 24.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        item(key = "create") {
            HomeWorkspaceAction(CupertinoGlyph.Plus, stringResource(R.string.home_new_project),
                "home.workspace.create", primary = true, onClick = onCreate)
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
        item(key = "projects-heading") {
            Row(
                Modifier.fillMaxWidth().padding(top = 12.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(stringResource(R.string.om_home_my_projects), Modifier.weight(1f).semantics { heading() },
                    style = AureaType.of(20f, FontWeight.Bold, color = AureaColors.LightText))
                Row(
                    Modifier.heightIn(min = 48.dp).testTag("home.workspace.projects").tocavel(onClick = onProjects)
                        .padding(start = 10.dp, end = 4.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    Text(stringResource(R.string.om_add_view_all), style = AureaType.of(13f, FontWeight.SemiBold, color = AureaColors.LightAccentInk))
                    CupertinoIcon(CupertinoGlyph.ChevronRight, 12.dp, AureaColors.LightAccentInk)
                }
            }
        }
        items(recent, key = { "project:" + it.path }) { entry ->
            HomeRecentProject(entry, thumbnails) { onOpen(entry) }
        }
        if (projectsLoaded && recent.isEmpty()) item(key = "empty") {
            Text(stringResource(R.string.om_home_empty_body, stringResource(R.string.home_new_project)),
                Modifier.padding(horizontal = 4.dp, vertical = 8.dp), style = AureaType.of(14f, color = AureaColors.LightTextMuted))
        }
        item(key = "mural") {
            HomeWorkspaceAction(CupertinoGlyph.RectangleStack, stringResource(R.string.mural_title),
                "home.mural", onClick = onMural)
        }
        item(key = "version") {
            HomeWorkspaceAction(CupertinoGlyph.Sparkles, stringResource(R.string.home_news_version),
                "home.releaseNotes", onClick = { notesOpen = true })
        }
    }
    if (notesOpen) ReleaseNotesScreen(stringResource(R.string.release_notes_body)) { notesOpen = false }
}

@Composable
private fun HomeWorkspaceAction(glyph: Char, label: String, tag: String, primary: Boolean = false, onClick: () -> Unit) {
    val shape = RoundedCornerShape(14.dp)
    val ink = if (primary) AureaColors.OnAccent else AureaColors.LightText
    Row(
        Modifier.fillMaxWidth().heightIn(min = if (primary) 60.dp else 52.dp).clip(shape)
            .background(if (primary) AureaColors.Accent else AureaColors.LightSurface)
            .testTag(tag).tocavel(onClick = onClick).padding(horizontal = 14.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        CupertinoIcon(glyph, 20.dp, if (primary) ink else AureaColors.LightAccentInk)
        Text(label, Modifier.weight(1f), style = AureaType.of(if (primary) 17f else 14f, FontWeight.SemiBold, color = ink))
        CupertinoIcon(CupertinoGlyph.ChevronRight, 13.dp, ink)
    }
}

@Composable
private fun HomeRecentProject(entry: ProjectEntry, thumbnails: HomeThumbnails, onClick: () -> Unit) {
    val shape = RoundedCornerShape(14.dp)
    Row(
        Modifier.fillMaxWidth().heightIn(min = 66.dp).clip(shape).background(AureaColors.LightSurface)
            .testTag("home.workspace.project").tocavel(onClick = onClick).padding(horizontal = 10.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        val thumbnail = rememberProjectThumbnail(thumbnails, entry)
        Box(Modifier.size(width = 60.dp, height = 44.dp).clip(RoundedCornerShape(8.dp))
            .background(AureaColors.HomeThumb), contentAlignment = Alignment.Center) {
            if (thumbnail != null) Image(thumbnail, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
            else CupertinoIcon(CupertinoGlyph.Film, 18.dp, AureaColors.LightTextMuted)
        }
        Column(Modifier.weight(1f)) {
            Text(entry.title, maxLines = 1, overflow = TextOverflow.Ellipsis,
                style = AureaType.of(15f, FontWeight.SemiBold, color = AureaColors.LightText))
            if (entry.width > 0 && entry.height > 0) {
                Text("${entry.width} × ${entry.height}", style = AureaType.of(12f, color = AureaColors.LightTextMuted))
            }
        }
        CupertinoIcon(CupertinoGlyph.ChevronRight, 13.dp, AureaColors.LightTextMuted)
    }
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
