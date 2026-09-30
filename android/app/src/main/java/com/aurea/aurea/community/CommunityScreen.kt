package com.aurea.aurea.community

import android.graphics.Bitmap
import android.net.Uri
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.*
import androidx.compose.animation.animateContentSize
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.conta.ContaViewModel
import com.aurea.aurea.engine.AureaEngine
import com.aurea.aurea.home.CommunityPresetsTab
import com.aurea.aurea.presets.PresetKind
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ProjectFile
import com.aurea.aurea.ui.theme.*
import kotlinx.coroutines.*
import org.json.JSONObject
import java.io.File
import java.util.UUID

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun CommunityScreen(store: EditorStore, conta: ContaViewModel, profileMode: Boolean,
    suppliedApi: CommunityApi? = null) {
    val context = LocalContext.current
    val token = conta.sessao()?.token.orEmpty()
    val api = remember(token, suppliedApi) { suppliedApi ?: CommunityApi(token) }
    val scope = rememberCoroutineScope()
    var me by remember(api) { mutableStateOf<CommunityProfile?>(null) }
    var canVerify by remember(api) { mutableStateOf(false) }
    var profile by remember(api) { mutableStateOf<CommunityProfile?>(null) }
    var feed by remember(api) { mutableStateOf(emptyList<CommunityPost>()) }
    var cursor by remember(api) { mutableStateOf<String?>(null) }
    var following by remember { mutableStateOf(false) }
    var busy by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    var search by remember { mutableStateOf("") }
    var results by remember { mutableStateOf(emptyList<CommunityProfile>()) }
    var edit by remember { mutableStateOf(false) }
    var composing by remember { mutableStateOf(false) }
    var legacy by remember { mutableStateOf(false) }
    var commentsFor by remember { mutableStateOf<CommunityPost?>(null) }
    var verifying by remember { mutableStateOf(false) }
    var deleting by remember { mutableStateOf<CommunityPost?>(null) }
    var people by remember { mutableStateOf<String?>(null) }
    var peopleList by remember { mutableStateOf(emptyList<CommunityProfile>()) }
    var peopleCursor by remember { mutableStateOf<String?>(null) }
    fun failure(e: Exception) { error = context.getString(when ((e as? CommunityFailure)?.code) {
        "username_taken" -> R.string.social_username_taken
        "profile_required" -> R.string.social_profile_required
        "invalid_profile" -> R.string.social_profile_hint
        "file_too_large" -> R.string.social_file_limit
        "unauthorized" -> R.string.social_session_error
        else -> R.string.social_error
    }) }
    fun work(action: suspend () -> Unit) {
        if (busy) return
        scope.launch { busy = true; error = null
            try { action() } catch (e: CancellationException) { throw e } catch (e: Exception) { failure(e) }
            finally { busy = false }
        }
    }
    suspend fun loadFeed(more: Boolean = false) {
        val suffix = "?following=${if (following && profile == null) 1 else 0}" +
            (profile?.let { "&user=${it.id}" } ?: "") + (if (more && cursor != null) "&cursor=${CommunityApi.query(cursor!!)}" else "")
        val r = withContext(Dispatchers.IO) { api.request("/posts$suffix") }
        val items = r.optJSONArray("items").mapObjects { it.post() }
        feed = if (more) (feed + items).distinctBy { it.id } else items; cursor = r.nextCursor()
    }
    suspend fun refresh() {
        val r = withContext(Dispatchers.IO) { api.request("/me") }
        me = r.getJSONObject("profile").profile(); canVerify = r.optBoolean("canVerify")
        if (profileMode && profile == null || profile?.id == me?.id) profile = me
        loadFeed()
    }
    fun showProfile(id: String) = work {
        val r = withContext(Dispatchers.IO) { api.request("/profiles/$id") }
        profile = r.getJSONObject("profile").profile(); search = ""; results = emptyList(); loadFeed()
    }
    fun listPeople(type: String, more: Boolean = false) = work {
        val p = profile ?: return@work
        val r = withContext(Dispatchers.IO) { api.request("/profiles/${p.id}/$type" + if (more && peopleCursor != null) "?cursor=${peopleCursor}" else "") }
        val items = r.optJSONArray("items").mapObjects { it.profile() }
        peopleList = if (more) (peopleList + items).distinctBy { it.id } else items
        peopleCursor = r.nextCursor(); people = type
    }
    LaunchedEffect(api, profileMode) {
        profile = null; search = ""; feed = emptyList(); busy = true
        try { refresh() } catch (e: CancellationException) { throw e } catch (e: Exception) { failure(e) } finally { busy = false }
    }
    LaunchedEffect(search) {
        if (search.trim().length < 2) { results = emptyList(); return@LaunchedEffect }
        delay(300)
        try { results = withContext(Dispatchers.IO) { api.request("/profiles?q=${CommunityApi.query(search.trim().removePrefix("@"))}") }.optJSONArray("items").mapObjects { it.profile() } }
        catch (e: CancellationException) { throw e } catch (e: Exception) { failure(e) }
    }
    val nestedProfile = profile != null && (!profileMode || profile?.id != me?.id)
    BackHandler(nestedProfile && !busy) { work { profile = if (profileMode) me else null; loadFeed() } }
    Column(Modifier.fillMaxSize().background(AureaColors.Background).testTag("social.screen")) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 20.dp), verticalAlignment = Alignment.CenterVertically) {
            if (nestedProfile) TextButton(enabled = !busy, onClick = { work { profile = if (profileMode) me else null; loadFeed() } }) { Text("‹", fontSize = 28.sp) }
            Text(stringResource(if (profile != null) R.string.social_profile else R.string.social_community), style = AureaType.ScreenTitle, modifier = Modifier.weight(1f))
            TextButton(enabled = !busy, onClick = { work { refresh() } }) { Text(stringResource(R.string.social_refresh)) }
        }
        if (busy) LinearProgressIndicator(Modifier.fillMaxWidth(), color = AureaColors.Accent)
        error?.let { Text(it, color = AureaColors.Danger, modifier = Modifier.padding(horizontal = 20.dp, vertical = 8.dp).testTag("social.error")) }
        LazyColumn(Modifier.weight(1f).testTag("social.feed"), contentPadding = PaddingValues(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            val p = profile
            if (p == null) {
                item("intro") { Text(stringResource(R.string.social_intro), color = AureaColors.Muted, style = AureaType.BodySmall) }
                item("search") { OutlinedTextField(search, { search = it.take(25) }, label = { Text(stringResource(R.string.social_search)) }, singleLine = true, modifier = Modifier.fillMaxWidth().testTag("social.search")) }
                items(results, key = { "search-${it.id}" }) { result -> CommunityAuthor(result, api, Modifier.fillMaxWidth().clickable(enabled = !busy) { showProfile(result.id) }) }
                item("filters") {
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        FilterChip(!following, enabled = !busy, onClick = { work { following = false; loadFeed() } }, label = { Text(stringResource(R.string.social_all)) })
                        FilterChip(following, enabled = !busy, onClick = { work { following = true; loadFeed() } }, label = { Text(stringResource(R.string.social_following)) })
                        Spacer(Modifier.weight(1f))
                        TextButton(onClick = { legacy = true }) { Text(stringResource(R.string.pn_caption)) }
                    }
                }
            } else {
                item("profile") {
                    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(24.dp)).background(AureaColors.Surface).padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                        CommunityAuthor(p, api, large = true)
                        if (p.bio.isNotBlank()) Text(p.bio, style = AureaType.Body)
                        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                            TextButton(enabled = !busy, onClick = { listPeople("followers") }) { Text("${p.followers} " + stringResource(R.string.social_followers)) }
                            TextButton(enabled = !busy, onClick = { listPeople("following") }) { Text("${p.following} " + stringResource(R.string.social_following)) }
                        }
                        if (p.id == me?.id) Button(onClick = { edit = true }, enabled = !busy, modifier = Modifier.fillMaxWidth().testTag("social.edit")) { Text(stringResource(R.string.social_edit_profile)) }
                        else Button(enabled = !busy, onClick = { work {
                            profile = withContext(Dispatchers.IO) { api.request("/profiles/${p.id}/follow", if (p.followed) "DELETE" else "PUT") }.getJSONObject("profile").profile()
                        } }, modifier = Modifier.fillMaxWidth()) { Text(stringResource(if (p.followed) R.string.social_unfollow else R.string.social_follow)) }
                        if (canVerify) OutlinedButton(onClick = { verifying = true }, enabled = !busy, modifier = Modifier.fillMaxWidth()) { Text(stringResource(R.string.social_verification)) }
                    }
                }
            }
            if (p == null || p.id == me?.id) item("compose") {
                Button(onClick = { if (me?.username.isNullOrBlank()) edit = true else composing = true }, enabled = me != null && !busy,
                    modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp).testTag("social.compose")) { Text(stringResource(R.string.social_create_post)) }
            }
            if (!busy && feed.isEmpty()) item("empty") { Text(stringResource(R.string.social_empty), color = AureaColors.Muted, modifier = Modifier.padding(vertical = 24.dp)) }
            items(feed, key = { it.id }) { post ->
                CommunityPostCard(post, api, busy, me?.id == post.author.id,
                    onProfile = { showProfile(post.author.id) }, onComments = { commentsFor = post }, onDelete = { deleting = post },
                    onLike = { work {
                        val r = withContext(Dispatchers.IO) { api.request("/posts/${post.id}/like", if (post.liked) "DELETE" else "PUT") }
                        feed = feed.map { if (it.id == post.id) it.copy(likes = r.getInt("likes"), liked = r.getBoolean("liked")) else it }
                    } }, onDownload = { work {
                        val asset = post.asset ?: return@work
                        val target = File(context.cacheDir, "community-${UUID.randomUUID()}.${if (asset.kind == "project") "aureaproj" else "json"}")
                        withContext(Dispatchers.IO) { api.download(asset, target) }
                        if (asset.kind == "project") store.importProjectFile(Uri.fromFile(target))
                        else {
                            try {
                                val source = withContext(Dispatchers.IO) { target.readText() }; val obj = JSONObject(source)
                                val kind = when (obj.getString("kind")) { "effects" -> PresetKind.Effects; "text" -> PresetKind.Text; "animation" -> PresetKind.Animation; "caption" -> PresetKind.Caption; "curve" -> PresetKind.Curve; else -> throw CommunityFailure("invalid_preset") }
                                val name = obj.optString("name").ifBlank { asset.name.removeSuffix(".json") }
                                var free = name; var n = 2
                                while (store.presets.exists(kind, free)) { free = "$name ($n)"; n++ }
                                if (store.presets.save(kind, free, source) == null) throw CommunityFailure("invalid_file")
                                store.showToast(context.getString(R.string.social_saved))
                            } finally { target.delete() }
                        }
                    } })
            }
            if (cursor != null) item("more") { TextButton(enabled = !busy, onClick = { work { loadFeed(true) } }, modifier = Modifier.fillMaxWidth()) { Text(stringResource(R.string.social_more)) } }
        }
    }
    if (edit && me != null) CommunityEditProfile(me!!, api, onError = ::failure,
        onDismiss = { edit = false }, onSaved = { me = it; if (profileMode || profile?.id == it.id) profile = it; edit = false })
    if (composing) CommunityCompose(store, api, onError = ::failure, onDismiss = { composing = false }, onPosted = { composing = false; work { refresh() } })
    commentsFor?.let { post -> CommunityComments(post, me?.id, api, ::failure, { commentsFor = null; work { loadFeed() } }, { id -> commentsFor = null; showProfile(id) }) }
    if (legacy) ModalBottomSheet(onDismissRequest = { legacy = false }, containerColor = AureaColors.Background) { Box(Modifier.fillMaxHeight(.88f)) { CommunityPresetsTab() } }
    if (verifying && profile != null) AlertDialog(onDismissRequest = { verifying = false }, title = { Text(stringResource(R.string.social_verification)) }, text = {
        Column { for ((badge, label) in badgeOptions) TextButton(enabled = !busy, onClick = { val id = profile!!.id; verifying = false; work {
            profile = withContext(Dispatchers.IO) { api.request("/profiles/$id/verification", "PUT", JSONObject().put("verification", badge)) }.getJSONObject("profile").profile(); loadFeed()
        } }) { CommunityBadge(badge); Text(stringResource(label), modifier = Modifier.padding(start = 8.dp)) } }
    }, confirmButton = { TextButton(onClick = { verifying = false }) { Text(stringResource(R.string.editor_fechar)) } })
    deleting?.let { post -> AlertDialog(onDismissRequest = { deleting = null }, title = { Text(stringResource(R.string.social_delete_post)) },
        text = { Text(post.body.take(160)) }, confirmButton = { TextButton(onClick = { deleting = null; work { withContext(Dispatchers.IO) { api.request("/posts/${post.id}", "DELETE") }; loadFeed() } }) { Text(stringResource(R.string.social_delete)) } },
        dismissButton = { TextButton(onClick = { deleting = null }) { Text(stringResource(R.string.editor_fechar)) } }) }
    people?.let { type -> ModalBottomSheet(onDismissRequest = { people = null }, containerColor = AureaColors.Background) {
        Text(stringResource(if (type == "followers") R.string.social_followers else R.string.social_following), style = AureaType.ScreenTitle, modifier = Modifier.padding(20.dp))
        LazyColumn(Modifier.fillMaxWidth().heightIn(max = 500.dp), contentPadding = PaddingValues(20.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
            items(peopleList, key = { it.id }) { p -> CommunityAuthor(p, api, Modifier.fillMaxWidth().clickable(enabled = !busy) { people = null; showProfile(p.id) }) }
            if (peopleList.isEmpty()) item { Text(stringResource(R.string.social_empty)) }
            if (peopleCursor != null) item { TextButton(enabled = !busy, onClick = { listPeople(type, true) }) { Text(stringResource(R.string.social_more)) } }
        }
    } }
}

private val badgeOptions = listOf("blue" to R.string.social_badge_blue, "green" to R.string.social_badge_green, "gold" to R.string.social_badge_gold, "" to R.string.social_badge_none)
@Composable internal fun CommunityBadge(badge: String) {
    val option = badgeOptions.firstOrNull { it.first == badge && badge.isNotEmpty() } ?: return
    val label = stringResource(option.second)
    val color = when (badge) { "blue" -> Color(0xFF70B8FF); "green" -> Color(0xFF6CDBAA); else -> Color(0xFFFFD16A) }
    Box(Modifier.size(22.dp).semantics { contentDescription = label }, contentAlignment = Alignment.Center) { CupertinoIcon(CupertinoGlyph.CheckmarkSeal, 19.dp, color) }
}
@Composable private fun CommunityAuthor(p: CommunityProfile, api: CommunityApi, modifier: Modifier = Modifier, large: Boolean = false) {
    val photo by produceState<Bitmap?>(null, p.avatar) { value = p.avatar?.let { withContext(Dispatchers.IO) { runCatching { api.avatar(it) }.getOrNull() } } }
    Row(modifier.heightIn(min = 48.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Box(Modifier.size(if (large) 64.dp else 44.dp).clip(CircleShape).background(AureaColors.Accent.copy(alpha = .14f)), contentAlignment = Alignment.Center) {
            if (photo != null) Image(photo!!.asImageBitmap(), null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
            else Text((p.name.ifBlank { p.username }).take(1).uppercase().ifBlank { "A" }, color = AureaColors.Accent, fontSize = if (large) 26.sp else 18.sp, fontWeight = FontWeight.Bold)
        }
        Column(Modifier.weight(1f)) {
            Row(verticalAlignment = Alignment.CenterVertically) { Text(p.name.ifBlank { stringResource(R.string.social_profile) }, style = AureaType.Body, fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f, fill = false)); CommunityBadge(p.verification) }
            Text(if (p.username.isEmpty()) stringResource(R.string.social_profile_required) else "@${p.username}", color = AureaColors.Muted, style = AureaType.BodySmall)
        }
    }
}
@Composable private fun CommunityPostCard(post: CommunityPost, api: CommunityApi, busy: Boolean, owned: Boolean,
    onProfile: () -> Unit, onComments: () -> Unit, onDelete: () -> Unit, onLike: () -> Unit, onDownload: () -> Unit) {
    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(22.dp)).background(AureaColors.Surface).padding(16.dp).animateContentSize(), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        CommunityAuthor(post.author, api, Modifier.fillMaxWidth().clickable(enabled = !busy, onClick = onProfile))
        Text(android.text.format.DateUtils.getRelativeTimeSpanString(post.createdAt).toString(), style = AureaType.Note, color = AureaColors.Muted)
        if (post.body.isNotEmpty()) Text(post.body, style = AureaType.Body)
        post.asset?.let { a -> OutlinedButton(enabled = !busy, onClick = onDownload, modifier = Modifier.fillMaxWidth()) {
            CupertinoIcon(if (a.kind == "project") CupertinoGlyph.RectangleStack else CupertinoGlyph.Sparkles, 22.dp, AureaColors.Accent)
            Column(Modifier.weight(1f).padding(horizontal = 10.dp)) { Text(a.name, maxLines = 2); Text("${a.bytes / 1024} KB", style = AureaType.Note) }
            Text(stringResource(R.string.social_download))
        } }
        Row(verticalAlignment = Alignment.CenterVertically) {
            val likeLabel = "${post.likes} " + stringResource(R.string.social_likes)
            TextButton(enabled = !busy, onClick = onLike, modifier = Modifier.testTag("social.like.${post.id}").semantics { contentDescription = likeLabel }) { Text(if (post.liked) "♥" else "♡", color = if (post.liked) AureaColors.Accent else AureaColors.Muted, fontSize = 22.sp); Text(" ${post.likes}") }
            TextButton(enabled = !busy, onClick = onComments) { Text("${post.comments} " + stringResource(R.string.social_comments)) }
            Spacer(Modifier.weight(1f))
            if (owned) IconButton(enabled = !busy, onClick = onDelete) { val label = stringResource(R.string.social_delete_post); Box(Modifier.semantics { contentDescription = label }) { CupertinoIcon(CupertinoGlyph.Trash, 19.dp, AureaColors.Muted) } }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable private fun CommunityEditProfile(profile: CommunityProfile, api: CommunityApi, onError: (Exception) -> Unit, onDismiss: () -> Unit, onSaved: (CommunityProfile) -> Unit) {
    var username by remember { mutableStateOf(profile.username) }; var name by remember { mutableStateOf(profile.name) }; var bio by remember { mutableStateOf(profile.bio) }
    var avatar by remember { mutableStateOf(profile.avatar) }; var photo by remember { mutableStateOf<Bitmap?>(null) }; var busy by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    val scope = rememberCoroutineScope(); val context = LocalContext.current
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri ->
        if (uri != null) scope.launch { busy = true
            try { photo = withContext(Dispatchers.IO) {
                val bitmap = AureaEngine.decodeBitmapRgba(context, uri) ?: throw CommunityFailure("invalid_image")
                val factor = 512f / maxOf(bitmap.width, bitmap.height); val result = Bitmap.createScaledBitmap(bitmap, (bitmap.width * factor).toInt().coerceAtLeast(1), (bitmap.height * factor).toInt().coerceAtLeast(1), true)
                if (result !== bitmap) bitmap.recycle(); result
            } } catch (e: Exception) { error = context.getString(R.string.social_error) } finally { busy = false }
        }
    }
    ModalBottomSheet(onDismissRequest = { if (!busy) onDismiss() }, containerColor = AureaColors.Background) {
        Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).imePadding().padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(stringResource(R.string.social_edit_profile), style = AureaType.ScreenTitle)
            Row(verticalAlignment = Alignment.CenterVertically) {
                photo?.let { Image(it.asImageBitmap(), null, Modifier.size(64.dp).clip(CircleShape), contentScale = ContentScale.Crop) }
                TextButton(enabled = !busy, onClick = { picker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly)) }) { Text(stringResource(R.string.social_photo)) }
            }
            OutlinedTextField(username, { username = it.take(24) }, singleLine = true, label = { Text(stringResource(R.string.social_username)) }, modifier = Modifier.fillMaxWidth().testTag("social.username"))
            Text(stringResource(R.string.social_profile_hint), style = AureaType.Note, color = AureaColors.Muted)
            OutlinedTextField(name, { name = it.take(50) }, singleLine = true, label = { Text(stringResource(R.string.social_name)) }, modifier = Modifier.fillMaxWidth().testTag("social.name"))
            OutlinedTextField(bio, { bio = it.take(240) }, label = { Text(stringResource(R.string.social_bio)) }, supportingText = { Text("${bio.length}/240") }, modifier = Modifier.fillMaxWidth())
            error?.let { Text(it, color = AureaColors.Danger) }
            Button(enabled = !busy && username.matches(Regex("[a-zA-Z0-9_]{3,24}")) && name.isNotBlank(), modifier = Modifier.fillMaxWidth().testTag("social.save"), onClick = {
                scope.launch { busy = true; error = null
                    try {
                        val value = withContext(Dispatchers.IO) {
                            photo?.let { bitmap -> val temp = File(context.cacheDir, "avatar-${UUID.randomUUID()}.jpg")
                                try { temp.outputStream().use { bitmap.compress(Bitmap.CompressFormat.JPEG, 85, it) }; avatar = api.upload(temp, "avatar").id } finally { temp.delete() } }
                            api.request("/me", "PUT", JSONObject().put("username", username).put("name", name).put("bio", bio).put("avatar", avatar ?: JSONObject.NULL))
                        }
                        onSaved(value.getJSONObject("profile").profile())
                    } catch (e: CancellationException) { throw e } catch (e: Exception) {
                        error = context.getString(if ((e as? CommunityFailure)?.code == "username_taken") R.string.social_username_taken else R.string.social_error)
                    } finally { busy = false }
                }
            }) { if (busy) CircularProgressIndicator(Modifier.size(20.dp)) else Text(stringResource(R.string.social_save)) }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable private fun CommunityCompose(store: EditorStore, api: CommunityApi, onError: (Exception) -> Unit, onDismiss: () -> Unit, onPosted: () -> Unit) {
    val context = LocalContext.current; val scope = rememberCoroutineScope()
    var body by remember { mutableStateOf("") }; var selected by remember { mutableStateOf<Pair<File, String>?>(null) }
    var picker by remember { mutableStateOf<String?>(null) }; var busy by remember { mutableStateOf(false) }; var error by remember { mutableStateOf<String?>(null) }
    val files = remember { mutableListOf<File>() }
    DisposableEffect(Unit) { onDispose { files.forEach { it.delete() } } }
    ModalBottomSheet(onDismissRequest = { if (!busy) onDismiss() }, containerColor = AureaColors.Background) {
        Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).imePadding().padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(stringResource(R.string.social_create_post), style = AureaType.ScreenTitle)
            OutlinedTextField(body, { body = it.take(2000) }, label = { Text(stringResource(R.string.social_post_hint)) }, minLines = 3, maxLines = 6, modifier = Modifier.fillMaxWidth().testTag("social.post.body"))
            Row { TextButton(enabled = !busy, onClick = { picker = "preset" }) { Text(stringResource(R.string.social_attach_preset)) }; TextButton(enabled = !busy, onClick = { picker = "project" }) { Text(stringResource(R.string.social_attach_project)) } }
            selected?.let { item -> Row(verticalAlignment = Alignment.CenterVertically) { Text(item.first.name, Modifier.weight(1f)); TextButton(enabled = !busy, onClick = { selected = null }) { Text(stringResource(R.string.social_remove)) } } }
            Text(stringResource(R.string.social_file_limit), style = AureaType.Note, color = AureaColors.Muted)
            error?.let { Text(it, color = AureaColors.Danger) }
            Button(enabled = !busy && (body.isNotBlank() || selected != null), modifier = Modifier.fillMaxWidth().testTag("social.publish"), onClick = {
                scope.launch { busy = true; error = null
                    try { withContext(Dispatchers.IO) {
                        val attachment = selected?.let { api.upload(it.first, it.second) }
                        api.request("/posts", "POST", JSONObject().put("body", body).put("asset", attachment?.id ?: JSONObject.NULL))
                    }; onPosted() } catch (e: CancellationException) { throw e } catch (e: Exception) { error = context.getString(R.string.social_error) } finally { busy = false }
                }
            }) { if (busy) CircularProgressIndicator(Modifier.size(20.dp)) else Text(stringResource(R.string.social_publish)) }
        }
    }
    picker?.let { type -> AlertDialog(onDismissRequest = { picker = null }, title = { Text(stringResource(if (type == "preset") R.string.social_attach_preset else R.string.social_attach_project)) }, text = {
        LazyColumn(Modifier.heightIn(max = 400.dp)) {
            if (type == "preset") {
                val presets = store.presets.all().filter { it.textPreset == null }
                items(presets, key = { it.key }) { p -> TextButton(onClick = {
                    val json = store.presets.jsonOf(p) ?: return@TextButton
                    val dir = File(context.cacheDir, "community-${UUID.randomUUID()}").apply { mkdirs() }
                    val file = File(dir, p.name.replace(Regex("[^\\p{L}\\p{N} _.-]"), "_").take(60) + ".json")
                    file.writeText(json); files.add(file); selected = file to "preset"; picker = null
                }) { Text(p.name) } }
                if (presets.isEmpty()) item { Text(stringResource(R.string.social_no_files)) }
            } else {
                items(store.projects, key = { it.path }) { p -> TextButton(enabled = !busy, onClick = {
                    picker = null; busy = true; scope.launch {
                        try {
                            val dir = File(context.cacheDir, "community-${UUID.randomUUID()}").apply { mkdirs() }
                            val file = File(dir, p.title.replace(Regex("[^\\p{L}\\p{N} _.-]"), "_").take(60) + ".aureaproj"); files.add(file)
                            val outcome = store.prepareCommunityProject(p.path, Uri.fromFile(file))
                            if (outcome.code != 0 || outcome.skipped > 0) throw CommunityFailure("incomplete_project")
                            if (file.length() > 50L * 1024 * 1024) throw CommunityFailure("file_too_large")
                            selected = file to "project"
                        } catch (e: Exception) { error = context.getString(if ((e as? CommunityFailure)?.code == "file_too_large") R.string.social_file_limit else R.string.social_error) } finally { busy = false }
                    }
                }) { Text(p.title) } }
                if (store.projects.isEmpty()) item { Text(stringResource(R.string.social_no_files)) }
            }
        }
    }, confirmButton = { TextButton(onClick = { picker = null }) { Text(stringResource(R.string.editor_fechar)) } }) }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable private fun CommunityComments(post: CommunityPost, ownId: String?, api: CommunityApi, onError: (Exception) -> Unit, onDismiss: () -> Unit, onProfile: (String) -> Unit) {
    var items by remember { mutableStateOf(emptyList<CommunityComment>()) }; var cursor by remember { mutableStateOf<String?>(null) }
    var text by remember { mutableStateOf("") }; var busy by remember { mutableStateOf(false) }; var error by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    suspend fun load(more: Boolean = false) {
        val r = withContext(Dispatchers.IO) { api.request("/posts/${post.id}/comments" + if (more && cursor != null) "?cursor=${CommunityApi.query(cursor!!)}" else "") }
        val values = r.optJSONArray("items").mapObjects { it.comment() }
        items = if (more) (items + values).distinctBy { it.id } else values; cursor = r.nextCursor()
    }
    fun work(action: suspend () -> Unit) { if (!busy) scope.launch { busy = true; error = false
        try { action() } catch (e: CancellationException) { throw e } catch (e: Exception) { error = true } finally { busy = false } } }
    LaunchedEffect(post.id) { work { load() } }
    ModalBottomSheet(onDismissRequest = { if (!busy) onDismiss() }, containerColor = AureaColors.Background) {
        Column(Modifier.fillMaxWidth().fillMaxHeight(.9f).imePadding().padding(horizontal = 20.dp)) {
            Text(stringResource(R.string.social_comments), style = AureaType.ScreenTitle)
            if (busy) LinearProgressIndicator(Modifier.fillMaxWidth())
            if (error) TextButton(onClick = { work { load() } }) { Text(stringResource(R.string.social_error)) }
            LazyColumn(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(16.dp), contentPadding = PaddingValues(vertical = 16.dp)) {
                if (items.isEmpty() && !busy) item { Text(stringResource(R.string.social_no_comments), color = AureaColors.Muted) }
                items(items, key = { it.id }) { c -> Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    CommunityAuthor(c.author, api, Modifier.fillMaxWidth().clickable(enabled = !busy) { onProfile(c.author.id) }); Text(c.body)
                    if (c.author.id == ownId) TextButton(enabled = !busy, onClick = { work { withContext(Dispatchers.IO) { api.request("/comments/${c.id}", "DELETE") }; load() } }) { Text(stringResource(R.string.social_delete)) }
                } }
                if (cursor != null) item { TextButton(enabled = !busy, onClick = { work { load(true) } }) { Text(stringResource(R.string.social_more)) } }
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                OutlinedTextField(text, { text = it.take(1000) }, label = { Text(stringResource(R.string.social_comment_hint)) }, modifier = Modifier.weight(1f).testTag("social.comment.body"), maxLines = 3)
                TextButton(enabled = !busy && text.isNotBlank(), onClick = { work {
                    withContext(Dispatchers.IO) { api.request("/posts/${post.id}/comments", "POST", JSONObject().put("body", text)) }; text = ""; load()
                } }) { Text(stringResource(R.string.social_send)) }
            }
            Spacer(Modifier.height(24.dp))
        }
    }
}
