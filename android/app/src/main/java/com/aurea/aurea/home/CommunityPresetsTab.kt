package com.aurea.aurea.home

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.captions.CaptionPresetStore
import com.aurea.aurea.ui.theme.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.launch
import org.json.JSONObject

/** Browses the real caption community; downloaded styles are available in the editor. */
@Composable
internal fun CommunityPresetsTab() {
    val context = LocalContext.current
    val library = remember { CaptionPresetStore(context.applicationContext) }
    val scope = rememberCoroutineScope()
    var offline by remember { mutableStateOf(false) }
    var query by remember { mutableStateOf("") }
    var revision by remember { mutableIntStateOf(0) }
    var items by remember { mutableStateOf<List<JSONObject>>(emptyList()) }
    var loading by remember { mutableStateOf(true) }
    var downloading by remember { mutableStateOf<String?>(null) }
    var failed by remember { mutableStateOf(false) }
    var message by remember { mutableStateOf<String?>(null) }
    LaunchedEffect(offline, revision) {
        loading = true
        message = null
        failed = false
        try {
            items = withContext(Dispatchers.IO) { if (offline) library.local(false, query) else library.community(query, false) }
        } catch (cancelled: kotlinx.coroutines.CancellationException) { throw cancelled }
        catch (_: Exception) { failed = true; items = emptyList(); message = context.getString(R.string.home_community_unavailable) }
        finally { loading = false }
    }
    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(20.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        item {
            Text(stringResource(R.string.home_community_title), style = AureaType.HeadlineLarge)
            Text(stringResource(R.string.home_community_caption_hint), style = AureaType.Body, color = AureaColors.Muted,
                modifier = Modifier.padding(top = 8.dp))
        }
        item {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilterChip(selected = !offline, onClick = { offline = false }, label = { Text(stringResource(R.string.home_presets_short)) })
                FilterChip(selected = offline, onClick = { offline = true }, label = { Text(stringResource(R.string.home_downloaded)) })
            }
            Row {
                OutlinedTextField(value = query, onValueChange = { query = it.take(80) }, singleLine = true,
                    label = { Text(stringResource(R.string.home_search_presets)) }, modifier = Modifier.weight(1f))
                TextButton(enabled = !loading, onClick = { revision++ }, modifier = Modifier.heightIn(min = 56.dp)) {
                    Text(stringResource(R.string.home_search_action))
                }
            }
        }
        if (loading) item { LinearProgressIndicator(modifier = Modifier.fillMaxWidth()) }
        message?.let { text -> item {
            Text(text, color = AureaColors.Muted)
            if (failed) TextButton(onClick = { revision++ }) { Text(stringResource(R.string.home_retry)) }
        } }
        if (!loading && items.isEmpty() && message == null) item {
            Text(stringResource(R.string.home_no_presets), color = AureaColors.Muted, modifier = Modifier.padding(vertical = 32.dp))
        }
        items(items, key = { it.getString("id") }) { entry ->
            val id = entry.getString("id")
            Column(Modifier.fillMaxWidth().background(AureaColors.Surface, RoundedCornerShape(20.dp)).padding(20.dp)) {
                Text(entry.optString("name"), style = AureaType.TitleMedium)
                Text(entry.optString("author", "Aurea"), style = AureaType.BodySmall, color = AureaColors.Muted)
                TextButton(enabled = downloading == null && !offline, onClick = {
                    downloading = id
                    scope.launch {
                        try {
                            withContext(Dispatchers.IO) { library.download(entry) }
                            failed = false; message = context.getString(R.string.home_preset_saved)
                        } catch (cancelled: kotlinx.coroutines.CancellationException) { throw cancelled }
                        catch (_: Exception) { failed = true; message = context.getString(R.string.home_community_unavailable) }
                        finally { downloading = null }
                    }
                }) { Text(stringResource(if (offline) R.string.home_downloaded else if (downloading == id) R.string.home_downloading else R.string.home_download)) }
            }
        }
    }
}
