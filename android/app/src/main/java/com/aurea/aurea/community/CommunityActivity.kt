package com.aurea.aurea.community

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.ChatBubbleOutline
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.ui.theme.*
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject

@Composable
internal fun CommunityActivityButton(unread: Int, onClick: () -> Unit) {
    BadgedBox(badge = { if (unread > 0) Badge { Text(if (unread > 99) "99+" else unread.toString()) } }) {
        IconButton(onClick = onClick, modifier = Modifier.testTag("social.activity")) {
            Icon(Icons.Outlined.ChatBubbleOutline, stringResource(R.string.social_activity_unread, unread),
                tint = AureaColors.LightText)
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun CommunityActivity(api: CommunityApi, onUnread: (Int) -> Unit, onOpen: (CommunityPost) -> Unit, onDismiss: () -> Unit) {
    val scope = rememberCoroutineScope()
    var entries by remember { mutableStateOf(emptyList<CommunityNotice>()) }
    var cursor by remember { mutableStateOf<String?>(null) }
    var through by remember { mutableLongStateOf(0L) }
    var busy by remember { mutableStateOf(true) }
    var failed by remember { mutableStateOf(false) }
    fun run(action: suspend () -> Unit) {
        busy = true; failed = false
        scope.launch {
            try { action() } catch (e: CancellationException) { throw e }
            catch (_: Exception) { failed = true } finally { busy = false }
        }
    }
    suspend fun load(more: Boolean = false) {
        val query = if (more) cursor?.let { "?cursor=${CommunityApi.query(it)}" }.orEmpty() else ""
        val page = withContext(Dispatchers.IO) { api.request("/notifications$query") }
        val next = page.optJSONArray("items").mapObjects { it.notice() }
        entries = if (more) (entries + next).distinctBy { it.id } else next
        cursor = page.nextCursor()
        if (!more) through = page.optLong("through")
        onUnread(page.optInt("unread"))
    }
    LaunchedEffect(api) { run { load() } }
    ModalBottomSheet(onDismissRequest = onDismiss, containerColor = AureaColors.LightBackground) {
        Column(Modifier.fillMaxWidth().fillMaxHeight(0.8f).padding(horizontal = 20.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.social_activity), style = AureaType.ScreenTitle,
                    color = AureaColors.LightText, modifier = Modifier.weight(1f))
                if (entries.any { it.unread }) TextButton(enabled = !busy, onClick = { run {
                    withContext(Dispatchers.IO) { api.request("/notifications/read", "PUT", JSONObject().put("through", through)) }
                    load()
                } }) { Text(stringResource(R.string.social_activity_mark_read)) }
            }
            if (busy) LinearProgressIndicator(Modifier.fillMaxWidth())
            if (failed) TextButton(onClick = { run { load() } }) { Text(stringResource(R.string.social_refresh)) }
            LazyColumn(Modifier.weight(1f), contentPadding = PaddingValues(vertical = 12.dp),
                verticalArrangement = Arrangement.spacedBy(5.dp)) {
                if (!busy && !failed && entries.isEmpty()) item {
                    Text(stringResource(R.string.social_activity_empty), color = AureaColors.LightTextMuted)
                }
                items(entries, key = { it.id }) { entry ->
                    val event = when (entry.kind) {
                        "reply" -> R.string.social_activity_reply
                        "announcement" -> R.string.social_activity_announcement
                        else -> R.string.social_activity_comment
                    }
                    Row(Modifier.fillMaxWidth().clickable(enabled = !busy) { run {
                        val post = withContext(Dispatchers.IO) { api.request("/posts/${entry.post}").post() }
                        onOpen(post)
                    } }.padding(vertical = 13.dp), verticalAlignment = Alignment.CenterVertically) {
                        Box(Modifier.size(10.dp).background(
                            if (entry.unread) AureaColors.LightAccentInk else AureaColors.LightSurface, CircleShape))
                        Column(Modifier.weight(1f).padding(horizontal = 12.dp)) {
                            Text(stringResource(event, entry.name), color = AureaColors.LightText)
                            Text(android.text.format.DateUtils.getRelativeTimeSpanString(entry.createdAt).toString(),
                                style = AureaType.Note, color = AureaColors.LightTextMuted)
                        }
                        CupertinoIcon(CupertinoGlyph.ChevronRight, 15.dp, AureaColors.LightTextMuted)
                    }
                }
                if (cursor != null) item {
                    TextButton(enabled = !busy, onClick = { run { load(true) } }) { Text(stringResource(R.string.social_more)) }
                }
            }
        }
    }
}
