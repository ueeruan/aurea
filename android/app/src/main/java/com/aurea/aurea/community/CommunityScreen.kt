package com.aurea.aurea.community

import android.app.Activity
import android.content.Intent
import android.graphics.Bitmap
import android.net.Uri
import android.os.Build
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Favorite
import androidx.compose.material.icons.outlined.ChatBubbleOutline
import androidx.compose.material.icons.outlined.FavoriteBorder
import androidx.compose.material.icons.outlined.Share
import androidx.compose.material3.*
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.repeatOnLifecycle
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
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

private enum class CommunityFeedTab(val title: Int) {
    All(R.string.social_all), Following(R.string.social_following), Official(R.string.social_official_tab)
}

private enum class CommunitySort(val wire: String, val title: Int) {
    Newest("newest", R.string.social_sort_newest), Popular("popular", R.string.social_sort_popular),
    Oldest("oldest", R.string.social_sort_oldest)
}

private enum class CommunityCategory(val wire: String, val title: Int) {
    All("", R.string.social_all_categories), Text("text", R.string.social_category_text),
    Showcase("showcase", R.string.social_category_showcase), Project("project", R.string.social_category_project),
    Preset("preset", R.string.social_category_preset), Official("announcement", R.string.social_category_announcement)
}

@Composable
private fun <T> CommunityChoice(label: String, value: T, options: List<T>, title: (T) -> Int,
    enabled: Boolean, tag: String, onSelect: (T) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    val current = stringResource(title(value))
    Box {
        OutlinedButton(onClick = { expanded = true }, enabled = enabled, modifier = Modifier.heightIn(min = 48.dp)
            .testTag(tag).semantics { contentDescription = "$label, $current" }) {
            Text(current, maxLines = 1)
            Spacer(Modifier.width(6.dp))
            CupertinoIcon(CupertinoGlyph.ChevronDown, 10.dp, AureaColors.LightTextMuted)
        }
        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            for (option in options) DropdownMenuItem(text = { Text(stringResource(title(option))) }, onClick = {
                expanded = false
                onSelect(option)
            })
        }
    }
}

/**
 * Omega home · P1-4: a aba Início (D1) e o Perfil (D2) com o casco da
 * referência — clara (papéis claros, componentes Material no claro), puxar
 * para atualizar no lugar do botão, o estado sem rede centralizado e o
 * indicador central enquanto carrega sem itens. O conteúdo social não muda.
 */
@Composable
internal fun CommunityScreen(store: EditorStore, conta: ContaViewModel, profileMode: Boolean,
    suppliedApi: CommunityApi? = null, onSignIn: (() -> Unit)? = null) {
    com.aurea.aurea.home.HomeMaterial { CommunityScreenContent(store, conta, profileMode, suppliedApi, onSignIn) }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun CommunityScreenContent(store: EditorStore, conta: ContaViewModel, profileMode: Boolean,
    suppliedApi: CommunityApi?, onSignIn: (() -> Unit)?) {
    val context = LocalContext.current
    val token = conta.sessao()?.token.orEmpty()
    val api = remember(token, suppliedApi) { suppliedApi ?: CommunityApi(token) }
    // Profile/account navigation invalidates work started by buttons too, not
    // only the initial LaunchedEffect refresh.
    val scope = remember(api, profileMode) { CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate) }
    DisposableEffect(scope) { onDispose { scope.cancel() } }
    var me by remember(api) { mutableStateOf<CommunityProfile?>(null) }
    var canVerify by remember(api) { mutableStateOf(false) }
    var profile by remember(api) { mutableStateOf<CommunityProfile?>(null) }
    var feed by remember(api) { mutableStateOf(emptyList<CommunityPost>()) }
    var cursor by remember(api) { mutableStateOf<String?>(null) }
    var feedTab by remember(api) { mutableStateOf(CommunityFeedTab.All) }
    var feedSort by remember(api) { mutableStateOf(CommunitySort.Newest) }
    var feedCategory by remember(api) { mutableStateOf(CommunityCategory.All) }
    var feedFilters by remember(api) { mutableStateOf(false) }
    var mediaPosts by remember(api) { mutableStateOf(false) }
    var activityAvailable by remember(api) { mutableStateOf(false) }
    var activityOpen by remember(api) { mutableStateOf(false) }
    var unread by remember(api) { mutableIntStateOf(0) }
    var feedTools by rememberSaveable { mutableStateOf(false) }
    val feedList = rememberLazyListState()
    var savingMedia by remember { mutableStateOf<String?>(null) }
    var pendingSavePath by rememberSaveable { mutableStateOf<String?>(null) }
    var busy by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    var search by remember { mutableStateOf("") }
    var results by remember { mutableStateOf(emptyList<CommunityProfile>()) }
    var edit by remember { mutableStateOf(false) }
    var composing by remember { mutableStateOf(false) }
    var legacy by remember { mutableStateOf(false) }
    var storeOpen by remember { mutableStateOf(false) }
    var storeSoon by remember { mutableStateOf(false) }
    // Ligada pelo servidor (GET /store/config): sem armazenamento, "Em breve".
    var storeReady by remember { mutableStateOf(false) }
    LaunchedEffect(api) { storeReady = kotlinx.coroutines.withContext(kotlinx.coroutines.Dispatchers.IO) { api.storeReady() } }
    var commentsFor by remember { mutableStateOf<CommunityPost?>(null) }
    var verifying by remember { mutableStateOf(false) }
    var deleting by remember { mutableStateOf<CommunityPost?>(null) }
    var people by remember { mutableStateOf<String?>(null) }
    var peopleList by remember { mutableStateOf(emptyList<CommunityProfile>()) }
    var peopleCursor by remember { mutableStateOf<String?>(null) }
    // Moderação: alvo da denúncia (tipo, id) e perfil esperando a confirmação do bloqueio.
    var reporting by remember { mutableStateOf<Pair<String, String>?>(null) }
    var blocking by remember { mutableStateOf<CommunityProfile?>(null) }
    // Omega home: sem rede (IOException do cliente) = o estado sem rede da referência.
    var offline by remember(api) { mutableStateOf(false) }
    var refreshing by remember { mutableStateOf(false) }
    val chooseSaveLocation = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        val temp = pendingSavePath?.let(::File)
        pendingSavePath = null
        val destination = result.data?.data
        if (temp != null && result.resultCode == Activity.RESULT_OK && destination != null) {
            scope.launch {
                try {
                    withContext(Dispatchers.IO) {
                        val bytes = context.contentResolver.openOutputStream(destination, "wt")?.use { out ->
                            temp.inputStream().use { it.copyTo(out) }
                        }
                        if (bytes != temp.length()) throw CommunityFailure("invalid_file")
                    }
                    store.showToast(context.getString(R.string.social_media_saved_file))
                } catch (e: CancellationException) { throw e }
                catch (_: Exception) {
                    withContext(Dispatchers.IO) { runCatching { context.contentResolver.delete(destination, null, null) } }
                    store.showToast(context.getString(R.string.social_media_save_failed))
                } finally { temp.delete() }
            }
        } else temp?.delete()
    }
    fun saveMedia(asset: CommunityAsset) {
        if (savingMedia != null || pendingSavePath != null) return
        savingMedia = asset.id
        scope.launch {
            var temp: File? = null
            try {
                val file = File.createTempFile("community-save-", ".tmp", context.cacheDir)
                temp = file
                val mime = withContext(Dispatchers.IO) { api.download(asset, file) }
                val name = mediaFileName(asset, mime)
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    withContext(Dispatchers.IO) { saveCommunityMediaToGallery(context, file, name, mime) }
                    store.showToast(context.getString(R.string.social_media_saved_gallery))
                } else {
                    pendingSavePath = file.absolutePath
                    try {
                        chooseSaveLocation.launch(Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                            .setType(mime).putExtra(Intent.EXTRA_TITLE, name))
                        temp = null
                    } catch (e: Exception) {
                        pendingSavePath = null
                        throw e
                    }
                }
            } catch (e: CancellationException) { throw e }
            catch (_: Exception) { store.showToast(context.getString(R.string.social_media_save_failed)) }
            finally { temp?.delete(); savingMedia = null }
        }
    }
    fun failure(e: Exception) { offline = e is java.io.IOException && e !is CommunityFailure; error = context.getString(when ((e as? CommunityFailure)?.code) {
        "username_taken" -> R.string.social_username_taken
        "profile_required" -> R.string.social_profile_required
        "invalid_profile" -> R.string.social_profile_hint
        "file_too_large" -> R.string.social_file_limit
        "unauthorized" -> R.string.social_session_error
        "rate_limited" -> R.string.app_community_limit
        else -> R.string.social_error
    }) }
    fun work(action: suspend () -> Unit) {
        if (busy) return
        scope.launch { busy = true; error = null
            try { action(); offline = false } catch (e: CancellationException) { throw e } catch (e: Exception) { failure(e) }
            finally { if (scope.isActive) { busy = false; refreshing = false } }
        }
    }
    suspend fun loadFeed(more: Boolean = false) {
        val suffix = buildString {
            append("?following=${if (feedTab == CommunityFeedTab.Following && profile == null) 1 else 0}")
            if (activityAvailable) append("&compact=1")
            profile?.let { append("&user=${it.id}") }
            if (feedFilters && profile == null) {
                append("&sort=${feedSort.wire}")
                val category = if (feedTab == CommunityFeedTab.Official) CommunityCategory.Official else feedCategory
                if (category != CommunityCategory.All) append("&category=${category.wire}")
            }
            if (more) cursor?.let { append("&cursor=${CommunityApi.query(it)}") }
        }
        val r = withContext(Dispatchers.IO) { api.request("/posts$suffix") }
        val items = r.optJSONArray("items").mapObjects { it.post() }
        feed = if (more) (feed + items).distinctBy { it.id } else items; cursor = r.nextCursor()
    }
    suspend fun resetFeed() {
        feed = emptyList(); cursor = null
        loadFeed()
    }
    suspend fun refresh(initial: Boolean = false) {
        val bundled = initial && !profileMode
        val r = withContext(Dispatchers.IO) { api.request(if (bundled) "/me?includeFeed=1" else "/me") }
        me = r.getJSONObject("profile").profile(); canVerify = r.optBoolean("canVerify"); feedFilters = r.optBoolean("feedFilters")
        mediaPosts = r.optBoolean("mediaPosts")
        activityAvailable = r.optBoolean("activity")
        if (!feedFilters && feedTab == CommunityFeedTab.Official) feedTab = CommunityFeedTab.All
        if (profileMode && profile == null || profile?.id == me?.id) profile = me
        val page = if (bundled) r.optJSONObject("feed") else null
        if (page == null) loadFeed() else {
            feed = page.optJSONArray("items").mapObjects { it.post() }
            cursor = page.nextCursor()
        }
    }
    fun showProfile(id: String) = work {
        val r = withContext(Dispatchers.IO) { api.request("/profiles/$id") }
        profile = r.getJSONObject("profile").profile(); feedTools = false; search = ""; results = emptyList(); resetFeed()
    }
    fun listPeople(type: String, more: Boolean = false) = work {
        val p = profile ?: return@work
        val r = withContext(Dispatchers.IO) { api.request("/profiles/${p.id}/$type" + if (more && peopleCursor != null) "?cursor=${peopleCursor}" else "") }
        val items = r.optJSONArray("items").mapObjects { it.profile() }
        peopleList = if (more) (peopleList + items).distinctBy { it.id } else items
        peopleCursor = r.nextCursor(); people = type
    }
    LaunchedEffect(api, profileMode) {
        profile = null; search = ""; feed = emptyList(); cursor = null; busy = true
        try { refresh(initial = true); offline = false } catch (e: CancellationException) { throw e } catch (e: Exception) { failure(e) }
        finally { if (currentCoroutineContext().isActive) busy = false }
    }
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    LaunchedEffect(api, activityAvailable, activityOpen, lifecycle) {
        if (!activityAvailable || activityOpen) return@LaunchedEffect
        lifecycle.repeatOnLifecycle(Lifecycle.State.STARTED) {
            while (isActive) {
                try { unread = withContext(Dispatchers.IO) { api.request("/notifications?summary=1").optInt("unread") } }
                catch (e: CancellationException) { throw e }
                catch (_: Exception) { }
                delay(45_000)
            }
        }
    }
    LaunchedEffect(api, profileMode, search) {
        if (search.trim().length < 2) { results = emptyList(); return@LaunchedEffect }
        delay(300)
        try { results = withContext(Dispatchers.IO) { api.request("/profiles?q=${CommunityApi.query(search.trim().removePrefix("@"))}") }.optJSONArray("items").mapObjects { it.profile() } }
        catch (e: CancellationException) { throw e } catch (e: Exception) { failure(e) }
    }
    LaunchedEffect(feedTools) { if (feedTools) feedList.scrollToItem(0) }
    val nestedProfile = profile != null && (!profileMode || profile?.id != me?.id)
    val official = profile == null && feedTab == CommunityFeedTab.Official
    BackHandler(nestedProfile && !busy) { work { profile = if (profileMode) me else null; resetFeed() } }
    BackHandler(profile == null && feedTools) { feedTools = false; search = "" }
    Column(Modifier.fillMaxSize().background(AureaColors.LightBackground).testTag("social.screen")) {
        Row(Modifier.fillMaxWidth().heightIn(min = 56.dp).padding(start = 16.dp, end = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            if (nestedProfile) TextButton(enabled = !busy, onClick = { work { profile = if (profileMode) me else null; resetFeed() } }) { Text("‹", fontSize = 28.sp) }
            Text(stringResource(if (official) R.string.social_official_tab else if (profile != null) R.string.social_profile else R.string.social_community),
                style = LTitle, maxLines = 1, overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
            if (profile == null) {
                CommunityIconButton(CupertinoGlyph.SliderHorizontal3, stringResource(R.string.social_feed_options), "social.feed.options",
                    tint = if (feedTools) AureaColors.LightAccentInk else AureaColors.LightText) {
                    feedTools = !feedTools
                    if (!feedTools) search = ""
                }
                if (!official || canVerify) {
                    CommunityIconButton(CupertinoGlyph.PlusSquare, stringResource(if (official) R.string.social_create_announcement else R.string.social_create_post),
                        "social.compose", enabled = me != null && !busy) { if (me?.username.isNullOrBlank()) edit = true else composing = true }
                }
            }
            if (activityAvailable) CommunityActivityButton(unread) { activityOpen = true }
            val owner = me
            if (profile == null && owner != null) {
                val label = stringResource(R.string.social_my_profile)
                IconButton(enabled = !busy, onClick = { showProfile(owner.id) }, modifier = Modifier.testTag("social.myProfile")
                    .semantics { contentDescription = label }) { CommunityAvatar(owner, api, 32) }
            } else if (profile == null && token.isBlank() && onSignIn != null) {
                CommunityIconButton(CupertinoGlyph.PersonCropCircle, stringResource(R.string.conta_entrar), "social.signIn", onClick = onSignIn)
            }
        }
        HorizontalDivider(color = AureaColors.LightTextMuted.copy(alpha = 0.15f))
        // Com itens na tela, uma barra fina; sem itens, o indicador fica no meio (abaixo).
        if (busy && !refreshing && feed.isNotEmpty()) LinearProgressIndicator(Modifier.fillMaxWidth(), color = AureaColors.LightAccentInk)
        if (!offline) error?.let { message ->
            Column(Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 8.dp)) {
                Text(message, color = AureaColors.LightDanger, modifier = Modifier.testTag("social.error"))
                if (feed.isEmpty()) TextButton(enabled = !busy, onClick = { work { refresh() } }) {
                    Text(stringResource(R.string.social_refresh))
                }
            }
        }
        PullToRefreshBox(
            isRefreshing = refreshing,
            onRefresh = { if (!busy) { refreshing = true; work { refresh() } } },
            modifier = Modifier.weight(1f).fillMaxWidth().testTag("home.feed.refresh"),
        ) {
        if (offline && feed.isEmpty() && !busy) {
            // Sem rede e sem conteúdo: ícone + texto centralizados (puxar tenta de novo).
            Column(
                Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(24.dp).testTag("home.feed.offline"),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
            ) {
                CupertinoIcon(CupertinoGlyph.ExclamationmarkTriangle, 48.dp, AureaColors.LightTextMuted)
                Spacer(Modifier.height(15.dp))
                Text(stringResource(R.string.conta_erro_rede), style = AureaType.of(12f, FontWeight.Bold, 0f, AureaColors.LightText),
                    textAlign = TextAlign.Center, modifier = Modifier.widthIn(max = 300.dp))
            }
        } else if (busy && feed.isEmpty() && me == null && !refreshing) {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                CircularProgressIndicator(color = AureaColors.LightAccentInk, modifier = Modifier.testTag("home.feed.loading"))
            }
        } else
        LazyColumn(Modifier.fillMaxSize().testTag("social.feed"), state = feedList,
            contentPadding = PaddingValues(top = 8.dp, bottom = 69.dp),
            horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(6.dp)) {
            val p = profile
            if (p == null && feedTools) {
                item("search") { OutlinedTextField(search, { search = it.take(25) }, label = { Text(stringResource(R.string.social_search)) }, singleLine = true, modifier = Modifier.widthIn(max = 620.dp).fillMaxWidth().padding(horizontal = 14.dp).testTag("social.search")) }
                items(results, key = { "search-${it.id}" }) { result -> CommunityAuthor(result, api, Modifier.widthIn(max = 620.dp).fillMaxWidth()
                    .clickable(enabled = !busy) { showProfile(result.id) }.padding(horizontal = 14.dp)) }
                item("filters") {
                    Column(Modifier.widthIn(max = 620.dp).fillMaxWidth().padding(horizontal = 14.dp)) {
                        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            for (tab in CommunityFeedTab.entries) {
                                if (tab == CommunityFeedTab.Official && !feedFilters) continue
                                FilterChip(feedTab == tab, enabled = !busy, onClick = {
                                    if (feedTab != tab) work { feedTab = tab; resetFeed() }
                                }, label = { Text(stringResource(tab.title)) }, modifier = Modifier.testTag("social.tab.${tab.name}"))
                            }
                        }
                        if (feedFilters) Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            CommunityChoice(stringResource(R.string.social_sort), feedSort, CommunitySort.entries, { it.title }, !busy, "social.sort") {
                                if (feedSort != it) work { feedSort = it; resetFeed() }
                            }
                            if (feedTab != CommunityFeedTab.Official) CommunityChoice(stringResource(R.string.social_filter), feedCategory,
                                CommunityCategory.entries.filter { it != CommunityCategory.Official }, { it.title }, !busy, "social.category") {
                                if (feedCategory != it) work { feedCategory = it; resetFeed() }
                            }
                        }
                        // 0.4.6: a Loja da Comunidade (plugins, efeitos e presets).
                        // Enquanto o servidor não guarda arquivos (R2): "Em breve" (GET /store/config).
                        // Beta (nov/2026): na mesma linha dos filtros o nome era espremido e
                        // quebrava no meio da palavra; agora tem a linha dele, numa linha só.
                        if (feedTab != CommunityFeedTab.Official) Row(Modifier.horizontalScroll(rememberScrollState())) {
                            TextButton(onClick = { if (storeReady) storeOpen = true else storeSoon = true }, modifier = Modifier.testTag("social.store")) {
                                Text(stringResource(R.string.store_title) + if (storeReady) "" else " · " + stringResource(R.string.store_soon), maxLines = 1)
                            }
                            TextButton(onClick = { legacy = true }) { Text(stringResource(R.string.pn_caption), maxLines = 1) }
                        }
                    }
                }
            } else if (p != null) {
                item("profile") {
                    Column(Modifier.widthIn(max = 620.dp).fillMaxWidth().padding(horizontal = 10.dp)
                        .clip(RoundedCornerShape(24.dp)).background(AureaColors.LightSurface).padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                        CommunityAuthor(p, api, large = true)
                        if (p.bio.isNotBlank()) Text(p.bio, style = LBody)
                        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                            TextButton(enabled = !busy, onClick = { listPeople("followers") }) { Text("${p.followers} " + stringResource(R.string.social_followers)) }
                            TextButton(enabled = !busy, onClick = { listPeople("following") }) { Text("${p.following} " + stringResource(R.string.social_following)) }
                        }
                        if (p.id == me?.id) Button(onClick = { edit = true }, enabled = !busy, modifier = Modifier.fillMaxWidth().testTag("social.edit")) { Text(stringResource(R.string.social_edit_profile)) }
                        // Bloqueado: o servidor já desfez o seguir e esconde o conteúdo dos dois lados.
                        else if (p.blocked) Text(stringResource(R.string.mod_blocked_note), color = AureaColors.LightTextMuted, style = LSmall, modifier = Modifier.testTag("social.blocked"))
                        else Button(enabled = !busy, onClick = { work {
                            profile = withContext(Dispatchers.IO) { api.request("/profiles/${p.id}/follow", if (p.followed) "DELETE" else "PUT") }.getJSONObject("profile").profile()
                        } }, modifier = Modifier.fillMaxWidth()) { Text(stringResource(if (p.followed) R.string.social_unfollow else R.string.social_follow)) }
                        if (canVerify) OutlinedButton(onClick = { verifying = true }, enabled = !busy, modifier = Modifier.fillMaxWidth()) { Text(stringResource(R.string.social_verification)) }
                        if (p.id != me?.id) Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            // Bloquear pede confirmação; desbloquear não (é só desfazer).
                            OutlinedButton(enabled = !busy, onClick = { if (p.blocked) work {
                                profile = withContext(Dispatchers.IO) { api.block(p.id, false) }; loadFeed()
                            } else blocking = p }, modifier = Modifier.weight(1f).heightIn(min = 48.dp).testTag("social.block")) {
                                Text(stringResource(if (p.blocked) R.string.mod_unblock else R.string.mod_block), textAlign = TextAlign.Center) }
                            OutlinedButton(enabled = !busy, onClick = { reporting = "user" to p.id }, modifier = Modifier.weight(1f).heightIn(min = 48.dp).testTag("social.report.profile")) {
                                Text(stringResource(R.string.mod_report_profile), textAlign = TextAlign.Center) }
                        }
                    }
                }
            }
            if (p != null && p.id == me?.id) item("compose") {
                Button(onClick = { if (me?.username.isNullOrBlank()) edit = true else composing = true }, enabled = me != null && !busy,
                    modifier = Modifier.widthIn(max = 620.dp).fillMaxWidth().padding(horizontal = 10.dp).heightIn(min = 48.dp).testTag("social.compose")) {
                    Text(stringResource(if (official) R.string.social_create_announcement else R.string.social_create_post))
                }
            }
            if (feed.isEmpty()) item("empty") {
                if (busy && !refreshing) Box(Modifier.fillMaxWidth().padding(24.dp), contentAlignment = Alignment.Center) {
                    CircularProgressIndicator(color = AureaColors.LightAccentInk, modifier = Modifier.testTag("home.feed.loading"))
                } else if (!busy && error == null) {
                    Text(stringResource(if (official) R.string.social_empty_announcements else R.string.social_empty),
                        color = AureaColors.LightTextMuted, modifier = Modifier.padding(vertical = 24.dp))
                }
            }
            items(feed, key = { it.id }) { post ->
                CommunityPostCard(post, api, busy, me?.id == post.author.id, canVerify, savingMedia != null || pendingSavePath != null,
                    onProfile = { showProfile(post.author.id) }, onComments = { commentsFor = post }, onDelete = { deleting = post },
                    onReport = { reporting = "post" to post.id },
                    onShare = {
                        val title = context.getString(R.string.editor_compartilhar)
                        runCatching {
                            context.startActivity(Intent.createChooser(Intent(Intent.ACTION_SEND).setType("text/plain")
                                .putExtra(Intent.EXTRA_TEXT, api.shareText(post)), title))
                        }.onFailure { store.showToast(context.getString(R.string.social_error)) }
                    },
                    onLike = { work {
                        val r = withContext(Dispatchers.IO) { api.request("/posts/${post.id}/like", if (post.liked) "DELETE" else "PUT") }
                        feed = feed.map { if (it.id == post.id) it.copy(likes = r.getInt("likes"), liked = r.getBoolean("liked")) else it }
                    } }, onDownload = { asset -> if (asset.isMedia) saveMedia(asset) else work {
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
            if (cursor != null) item("more") {
                LaunchedEffect(cursor, busy) { if (!busy && error == null && activityAvailable) work { loadFeed(true) } }
                TextButton(enabled = !busy, onClick = { work { loadFeed(true) } }, modifier = Modifier.fillMaxWidth()) { Text(stringResource(R.string.social_more)) }
            }
        }
        }
    }
    if (edit && me != null) CommunityEditProfile(me!!, api, onError = ::failure,
        onDismiss = { edit = false }, onSaved = { me = it; if (profileMode || profile?.id == it.id) profile = it; edit = false })
    if (composing) CommunityCompose(store, api, feedFilters, canVerify, mediaPosts, when {
        profile == null && feedTab == CommunityFeedTab.Official -> CommunityCategory.Official
        profile == null && feedCategory == CommunityCategory.Showcase -> CommunityCategory.Showcase
        else -> CommunityCategory.All
    }, onDismiss = { composing = false }, onPosted = { composing = false; work { refresh() } })
    if (activityOpen) CommunityActivity(api, onUnread = { unread = it }, onOpen = { activityOpen = false; commentsFor = it }, onDismiss = { activityOpen = false })
    commentsFor?.let { post -> CommunityComments(post, me, canVerify, activityAvailable, api, { commentsFor = null }, { id -> commentsFor = null; showProfile(id) },
        onCommentDelta = { delta -> feed = feed.map { if (it.id == post.id) it.copy(comments = (it.comments + delta).coerceAtLeast(0)) else it } },
        onReport = { c -> reporting = "comment" to c.id }) }
    if (storeSoon) AlertDialog(onDismissRequest = { storeSoon = false }, modifier = Modifier.testTag("social.store.soon"),
        title = { Text(stringResource(R.string.store_soon)) }, text = { Text(stringResource(R.string.store_soon_message)) },
        confirmButton = { TextButton(onClick = { storeSoon = false }) { Text(stringResource(R.string.editor_fechar)) } })
    if (storeOpen) androidx.compose.ui.window.Dialog(onDismissRequest = { storeOpen = false },
        properties = androidx.compose.ui.window.DialogProperties(usePlatformDefaultWidth = false)) {
        Box(Modifier.fillMaxSize().safeDrawingPadding()) {
            StoreScreen(store, api, canVerify, hasProfile = !me?.username.isNullOrBlank(),
                onEditProfile = { storeOpen = false; edit = true }, onClose = { storeOpen = false })
        }
    }
    if (legacy) ModalBottomSheet(onDismissRequest = { legacy = false }, containerColor = AureaColors.LightBackground) { Box(Modifier.fillMaxHeight(.88f)) { CommunityPresetsTab() } }
    if (verifying && profile != null) AlertDialog(onDismissRequest = { verifying = false }, title = { Text(stringResource(R.string.social_verification)) }, text = {
        Column { for ((badge, label) in badgeOptions) TextButton(enabled = !busy, onClick = { val id = profile!!.id; verifying = false; work {
            profile = withContext(Dispatchers.IO) { api.request("/profiles/$id/verification", "PUT", JSONObject().put("verification", badge)) }.getJSONObject("profile").profile(); loadFeed()
        } }) { CommunityBadge(badge); Text(stringResource(label), modifier = Modifier.padding(start = 8.dp)) } }
    }, confirmButton = { TextButton(onClick = { verifying = false }) { Text(stringResource(R.string.editor_fechar)) } })
    deleting?.let { post -> AlertDialog(onDismissRequest = { deleting = null }, title = { Text(stringResource(R.string.social_delete_post)) },
        text = { Text(post.body.take(160)) }, confirmButton = { TextButton(onClick = { deleting = null; work { withContext(Dispatchers.IO) { api.request("/posts/${post.id}", "DELETE") }; loadFeed() } }) { Text(stringResource(R.string.social_delete)) } },
        dismissButton = { TextButton(onClick = { deleting = null }) { Text(stringResource(R.string.editor_fechar)) } }) }
    people?.let { type -> ModalBottomSheet(onDismissRequest = { people = null }, containerColor = AureaColors.LightBackground) {
        Text(stringResource(if (type == "followers") R.string.social_followers else R.string.social_following), style = LTitle, modifier = Modifier.padding(20.dp))
        LazyColumn(Modifier.fillMaxWidth().heightIn(max = 500.dp), contentPadding = PaddingValues(20.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
            items(peopleList, key = { it.id }) { p -> CommunityAuthor(p, api, Modifier.fillMaxWidth().clickable(enabled = !busy) { people = null; showProfile(p.id) }) }
            if (peopleList.isEmpty()) item { Text(stringResource(R.string.social_empty)) }
            if (peopleCursor != null) item { TextButton(enabled = !busy, onClick = { listPeople(type, true) }) { Text(stringResource(R.string.social_more)) } }
        }
    } }
    blocking?.let { target -> AlertDialog(onDismissRequest = { blocking = null }, title = { Text(stringResource(R.string.mod_block)) },
        text = { Text(stringResource(R.string.mod_block_confirm, target.username.ifBlank { target.name })) },
        confirmButton = { TextButton(modifier = Modifier.testTag("social.block.confirm"), onClick = { blocking = null; work {
            val blocked = withContext(Dispatchers.IO) { api.block(target.id, true) }
            if (profile?.id == blocked.id) profile = blocked; loadFeed()
        } }) { Text(stringResource(R.string.mod_block)) } },
        dismissButton = { TextButton(onClick = { blocking = null }) { Text(stringResource(R.string.editor_fechar)) } }) }
    // Por último: abre por cima da folha de comentários quando a denúncia vem de lá.
    reporting?.let { (kind, id) -> key(kind, id) { ReportDialog(onDismiss = { reporting = null },
        onSent = { reporting = null; store.showToast(context.getString(R.string.mod_report_thanks)) }) { reason, details -> api.report(kind, id, reason, details) } } }
}

private val badgeOptions = listOf("blue" to R.string.social_badge_blue, "green" to R.string.social_badge_green, "gold" to R.string.social_badge_gold, "" to R.string.social_badge_none)
@Composable internal fun CommunityBadge(badge: String) {
    val option = badgeOptions.firstOrNull { it.first == badge && badge.isNotEmpty() } ?: return
    val label = stringResource(option.second)
    val color = when (badge) { "blue" -> Color(0xFF70B8FF); "green" -> Color(0xFF6CDBAA); else -> Color(0xFFFFD16A) }
    Box(Modifier.size(22.dp).semantics { contentDescription = label }, contentAlignment = Alignment.Center) { CupertinoIcon(CupertinoGlyph.CheckmarkSeal, 19.dp, color) }
}
@Composable private fun CommunityAvatar(p: CommunityProfile, api: CommunityApi, size: Int) {
    val photo by produceState<Bitmap?>(null, api, p.avatar) {
        value = p.avatar?.let { withContext(Dispatchers.IO) { runCatching { api.avatar(it) }.getOrNull() } }
    }
    Box(Modifier.size(size.dp).clip(CircleShape).background(AureaColors.LightAccentInk.copy(alpha = .14f)), contentAlignment = Alignment.Center) {
        val image = photo
        if (image != null) Image(image.asImageBitmap(), null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
        else Text((p.name.ifBlank { p.username }).take(1).uppercase().ifBlank { "A" }, color = AureaColors.LightAccentInk,
            fontSize = (size * 0.42f).sp, fontWeight = FontWeight.Bold)
    }
}

@Composable private fun CommunityAuthor(p: CommunityProfile, api: CommunityApi, modifier: Modifier = Modifier, large: Boolean = false, compact: Boolean = false) {
    Row(modifier.heightIn(min = 48.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        CommunityAvatar(p, api, if (large) 64 else if (compact) 32 else 44)
        Column(Modifier.weight(1f)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(p.name.ifBlank { stringResource(R.string.social_profile) }, style = if (compact) LSmall.copy(color = AureaColors.LightText) else LBody,
                    fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f, fill = false))
                CommunityBadge(p.verification)
            }
            Text(if (p.username.isEmpty()) stringResource(R.string.social_profile_required) else "@${p.username}", color = AureaColors.LightTextMuted,
                style = LNote, maxLines = 1, overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis)
        }
    }
}

@Composable private fun CommunityIconButton(glyph: Char, label: String, tag: String, enabled: Boolean = true,
    tint: Color = AureaColors.LightText, modifier: Modifier = Modifier, onClick: () -> Unit) {
    IconButton(onClick = onClick, enabled = enabled, modifier = modifier.size(48.dp).testTag(tag).semantics { contentDescription = label }) {
        CupertinoIcon(glyph, 21.dp, if (enabled) tint else AureaColors.LightTextMuted)
    }
}

@Composable private fun CommunityPostCard(post: CommunityPost, api: CommunityApi, busy: Boolean, owned: Boolean, canModerate: Boolean,
    mediaBusy: Boolean, onProfile: () -> Unit, onComments: () -> Unit, onDelete: () -> Unit, onReport: () -> Unit,
    onLike: () -> Unit, onShare: () -> Unit, onDownload: (CommunityAsset) -> Unit) {
    val primaryFile = post.media ?: post.asset
    val open = stringResource(R.string.social_open_post)
    val mediaBadge = AureaColors.LightBackground.copy(alpha = 0.94f)
    Column(Modifier.widthIn(max = 620.dp).fillMaxWidth().background(AureaColors.LightSurface)) {
        Row(Modifier.fillMaxWidth().padding(start = 14.dp, end = 4.dp).testTag("social.author.${post.id}"),
            verticalAlignment = Alignment.CenterVertically) {
            CommunityAuthor(post.author, api, Modifier.weight(1f).clickable(enabled = !busy, onClick = onProfile), compact = true)
            Box {
                var menu by remember { mutableStateOf(false) }
                CommunityIconButton(CupertinoGlyph.Ellipsis, stringResource(R.string.mod_more_actions),
                    "social.more.${post.id}", enabled = !busy, tint = AureaColors.LightTextMuted) { menu = true }
                DropdownMenu(menu, onDismissRequest = { menu = false }) {
                    if (post.media != null && post.asset != null) DropdownMenuItem(text = { Text(post.asset.name, maxLines = 1) },
                        onClick = { menu = false; onDownload(post.asset) })
                    if (!owned) DropdownMenuItem(text = { Text(stringResource(R.string.mod_report)) }, onClick = { menu = false; onReport() },
                        leadingIcon = { CupertinoIcon(CupertinoGlyph.Flag, 17.dp, AureaColors.LightTextMuted) }, modifier = Modifier.testTag("social.report.${post.id}"))
                    if (owned || canModerate) DropdownMenuItem(text = { Text(stringResource(if (owned) R.string.social_delete_post else R.string.mod_remove), color = AureaColors.LightDanger) }, onClick = { menu = false; onDelete() },
                        leadingIcon = { CupertinoIcon(CupertinoGlyph.Trash, 17.dp, AureaColors.LightDanger) }, modifier = Modifier.testTag("social.remove.${post.id}"))
                }
            }
        }
        Box(Modifier.fillMaxWidth().testTag("social.post.${post.id}")) {
            if (post.media != null) {
                CommunityPostMedia(post.media, api, detail = false, onOpen = onComments)
            } else {
                Column(Modifier.fillMaxWidth().heightIn(min = if (primaryFile == null) 48.dp else 64.dp)
                    .clickable(enabled = !busy, onClickLabel = open, onClick = onComments)
                    .padding(start = 14.dp, top = 10.dp, end = if (primaryFile == null) 14.dp else 65.dp, bottom = 14.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    if (post.body.isNotBlank()) Text(post.body, style = LBody, maxLines = 6,
                        overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis)
                    post.asset?.let { asset ->
                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            CupertinoIcon(if (asset.kind == "project") CupertinoGlyph.RectangleStack else CupertinoGlyph.Sparkles,
                                19.dp, AureaColors.LightAccentInk)
                            Text(asset.name, style = LSmall, maxLines = 1, overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis)
                        }
                    }
                }
            }
            if (primaryFile != null) {
                CommunityIconButton(CupertinoGlyph.ArrowDownToLine, stringResource(R.string.social_download),
                    "social.download.${post.id}", enabled = !busy && (!primaryFile.isMedia || !mediaBusy),
                    modifier = Modifier.align(Alignment.BottomEnd).padding(8.dp).clip(CircleShape).background(mediaBadge)) {
                    onDownload(primaryFile)
                }
            }
        }
        Row(Modifier.padding(horizontal = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            val likeLabel = "${post.likes} " + stringResource(R.string.social_likes)
            TextButton(enabled = !busy, onClick = onLike, contentPadding = PaddingValues(horizontal = 9.dp),
                modifier = Modifier.heightIn(min = 48.dp).testTag("social.like.${post.id}")
                    .semantics { contentDescription = likeLabel }) {
                Icon(if (post.liked) Icons.Filled.Favorite else Icons.Outlined.FavoriteBorder, contentDescription = null, modifier = Modifier.size(23.dp),
                    tint = if (post.liked) AureaColors.LightAccentInk else AureaColors.LightText)
                if (post.likes > 0) Text("${post.likes}", Modifier.padding(start = 5.dp), color = AureaColors.LightText)
            }
            val commentLabel = "${post.comments} " + stringResource(R.string.social_comments)
            TextButton(enabled = !busy, onClick = onComments, contentPadding = PaddingValues(horizontal = 9.dp),
                modifier = Modifier.heightIn(min = 48.dp).testTag("social.comments.${post.id}")
                    .semantics { contentDescription = commentLabel }) {
                Icon(Icons.Outlined.ChatBubbleOutline, contentDescription = null, modifier = Modifier.size(21.dp), tint = AureaColors.LightText)
                if (post.comments > 0) Text("${post.comments}", Modifier.padding(start = 5.dp), color = AureaColors.LightText)
            }
            IconButton(onClick = onShare, modifier = Modifier.testTag("social.share.${post.id}")) {
                Icon(Icons.Outlined.Share, stringResource(R.string.editor_compartilhar), tint = AureaColors.LightText, modifier = Modifier.size(22.dp))
            }
        }
        if (post.media != null && post.body.isNotBlank()) Text(post.body, style = LBody, maxLines = 3,
            overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis,
            modifier = Modifier.fillMaxWidth().clickable(onClickLabel = open, onClick = onComments).padding(horizontal = 14.dp))
        Row(Modifier.padding(start = 14.dp, end = 14.dp, top = 2.dp, bottom = 10.dp), verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            CommunityCategory.entries.firstOrNull { it != CommunityCategory.All && it != CommunityCategory.Text && it.wire == post.category }?.let {
                Text(stringResource(it.title), style = LNote, color = AureaColors.LightAccentInk)
            }
            Text(android.text.format.DateUtils.getRelativeTimeSpanString(post.createdAt).toString(), style = LNote, color = AureaColors.LightTextMuted)
        }
        HorizontalDivider(color = AureaColors.LightTextMuted.copy(alpha = 0.15f))
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
                val bitmap = AureaEngine.decodeBitmapRgba(context, uri, 512) ?: throw CommunityFailure("invalid_image")
                val factor = 512f / maxOf(bitmap.width, bitmap.height); val result = Bitmap.createScaledBitmap(bitmap, (bitmap.width * factor).toInt().coerceAtLeast(1), (bitmap.height * factor).toInt().coerceAtLeast(1), true)
                if (result !== bitmap) bitmap.recycle(); result
            } } catch (e: Exception) { error = context.getString(R.string.social_error) } finally { busy = false }
        }
    }
    ModalBottomSheet(onDismissRequest = { if (!busy) onDismiss() }, containerColor = AureaColors.LightBackground) {
        Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).imePadding().padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(stringResource(R.string.social_edit_profile), style = LTitle)
            Row(verticalAlignment = Alignment.CenterVertically) {
                photo?.let { Image(it.asImageBitmap(), null, Modifier.size(64.dp).clip(CircleShape), contentScale = ContentScale.Crop) }
                TextButton(enabled = !busy, onClick = { picker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly)) }) { Text(stringResource(R.string.social_photo)) }
            }
            OutlinedTextField(username, { username = it.take(24) }, singleLine = true, label = { Text(stringResource(R.string.social_username)) }, modifier = Modifier.fillMaxWidth().testTag("social.username"))
            Text(stringResource(R.string.social_profile_hint), style = LNote, color = AureaColors.LightTextMuted)
            OutlinedTextField(name, { name = it.take(50) }, singleLine = true, label = { Text(stringResource(R.string.social_name)) }, modifier = Modifier.fillMaxWidth().testTag("social.name"))
            OutlinedTextField(bio, { bio = it.take(240) }, label = { Text(stringResource(R.string.social_bio)) }, supportingText = { Text("${bio.length}/240") }, modifier = Modifier.fillMaxWidth())
            error?.let { Text(it, color = AureaColors.LightDanger) }
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
@Composable private fun CommunityCompose(store: EditorStore, api: CommunityApi, feedFilters: Boolean, canAnnounce: Boolean, mediaPosts: Boolean,
    initialCategory: CommunityCategory, onDismiss: () -> Unit, onPosted: () -> Unit) {
    val context = LocalContext.current; val scope = rememberCoroutineScope()
    var body by remember { mutableStateOf("") }; var selected by remember { mutableStateOf<Pair<File, String>?>(null) }
    var picker by remember { mutableStateOf<String?>(null) }; var busy by remember { mutableStateOf(false) }; var error by remember { mutableStateOf<String?>(null) }
    var category by remember { mutableStateOf(initialCategory) }
    val files = remember { mutableListOf<File>() }
    DisposableEffect(Unit) { onDispose { files.forEach { it.delete() } } }
    val pickMedia = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri ->
        if (uri != null) scope.launch {
            busy = true; error = null
            try {
                val prepared = withContext(Dispatchers.IO) { prepareCommunityMedia(context, uri) }
                files.add(prepared.first)
                selected = prepared
            } catch (e: CancellationException) { throw e }
            catch (e: Exception) {
                error = context.getString(if ((e as? CommunityFailure)?.code == "file_too_large") R.string.social_media_limits else R.string.social_media_error)
            } finally { busy = false }
        }
    }
    ModalBottomSheet(onDismissRequest = { if (!busy) onDismiss() }, containerColor = AureaColors.LightBackground) {
        Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).imePadding().padding(20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(stringResource(R.string.social_create_post), style = LTitle)
            if (feedFilters) {
                val categories = if (canAnnounce) listOf(CommunityCategory.All, CommunityCategory.Showcase, CommunityCategory.Official)
                    else listOf(CommunityCategory.All, CommunityCategory.Showcase)
                CommunityChoice(stringResource(R.string.social_post_type), category, categories,
                    { if (it == CommunityCategory.All) R.string.social_standard_post else it.title }, !busy, "social.post.category") { category = it }
            }
            OutlinedTextField(body, { body = it.take(2000) }, label = { Text(stringResource(R.string.social_post_hint)) }, minLines = 3, maxLines = 6, modifier = Modifier.fillMaxWidth().testTag("social.post.body"))
            if (mediaPosts) OutlinedButton(enabled = !busy, onClick = {
                pickMedia.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageAndVideo))
            }, modifier = Modifier.fillMaxWidth().testTag("social.attach.media")) {
                CupertinoIcon(CupertinoGlyph.PhotoOnRectangle, 20.dp, AureaColors.LightAccentInk)
                Spacer(Modifier.width(8.dp))
                Text(stringResource(R.string.social_attach_media))
            }
            Row { TextButton(enabled = !busy, onClick = { picker = "preset" }) { Text(stringResource(R.string.social_attach_preset)) }; TextButton(enabled = !busy, onClick = { picker = "project" }) { Text(stringResource(R.string.social_attach_project)) } }
            selected?.let { item -> Row(verticalAlignment = Alignment.CenterVertically) { Text(item.first.name, Modifier.weight(1f)); TextButton(enabled = !busy, onClick = { selected = null }) { Text(stringResource(R.string.social_remove)) } } }
            Text(stringResource(if (selected?.second in setOf("image", "video")) R.string.social_media_limits else R.string.social_file_limit),
                style = LNote, color = AureaColors.LightTextMuted)
            error?.let { Text(it, color = AureaColors.LightDanger) }
            Button(enabled = !busy && (body.isNotBlank() || selected != null), modifier = Modifier.fillMaxWidth().testTag("social.publish"), onClick = {
                scope.launch { busy = true; error = null
                    try { withContext(Dispatchers.IO) {
                        val item = selected
                        val media = item?.second == "image" || item?.second == "video"
                        val attachment = item?.let { if (media) api.uploadMedia(it.first, it.second) else api.upload(it.first, it.second) }
                        val post = JSONObject().put("body", body)
                            .put(if (media) "media" else "asset", attachment?.id ?: JSONObject.NULL)
                        if (feedFilters && category != CommunityCategory.All) post.put("category", category.wire)
                        try { api.request("/posts", "POST", post) }
                        catch (e: Exception) {
                            if (media && attachment != null) runCatching { api.request("/media/${attachment.id}", "DELETE") }
                            throw e
                        }
                    }; onPosted() } catch (e: CancellationException) { throw e } catch (e: Exception) {
                        error = context.getString(when ((e as? CommunityFailure)?.code) {
                            "file_too_large" -> if (selected?.second == "video" || selected?.second == "image") R.string.social_media_limits else R.string.social_file_limit
                            "invalid_video", "invalid_image" -> R.string.social_media_error
                            else -> R.string.social_error
                        })
                    } finally { busy = false }
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
@Composable private fun CommunityComments(post: CommunityPost, own: CommunityProfile?, canModerate: Boolean, replies: Boolean, api: CommunityApi,
    onDismiss: () -> Unit, onProfile: (String) -> Unit, onCommentDelta: (Int) -> Unit, onReport: (CommunityComment) -> Unit) {
    var items by remember { mutableStateOf(emptyList<CommunityComment>()) }; var cursor by remember { mutableStateOf<String?>(null) }
    var removing by remember { mutableStateOf<CommunityComment?>(null) }
    var replyTo by remember { mutableStateOf<CommunityComment?>(null) }
    var submittedComment by remember { mutableStateOf<String?>(null) }
    var text by remember { mutableStateOf("") }; var busy by remember { mutableStateOf(false) }; var error by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    val listState = rememberLazyListState()
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    val focus = LocalFocusManager.current
    val keyboard = LocalSoftwareKeyboardController.current
    suspend fun load(more: Boolean = false) {
        val r = withContext(Dispatchers.IO) { api.request("/posts/${post.id}/comments" + if (more && cursor != null) "?cursor=${CommunityApi.query(cursor!!)}" else "") }
        val values = r.optJSONArray("items").mapObjects { it.comment() }
        items = if (more) (items + values).distinctBy { it.id }
            .sortedWith(compareBy<CommunityComment> { it.createdAt }.thenBy { it.id }) else values
        cursor = r.nextCursor()
    }
    fun work(action: suspend () -> Unit) { if (!busy) scope.launch { busy = true; error = false
        try { action() } catch (e: CancellationException) { throw e } catch (e: Exception) { error = true } finally { busy = false } } }
    LaunchedEffect(post.id) { work { load() } }
    LaunchedEffect(submittedComment, items) {
        val index = submittedComment?.let { id -> items.indexOfFirst { it.id == id } } ?: -1
        if (index >= 0) {
            listState.scrollToItem(index + 1)
            submittedComment = null
        }
    }
    ModalBottomSheet(onDismissRequest = { if (!busy) onDismiss() }, sheetState = sheetState, containerColor = AureaColors.LightBackground) {
        Column(Modifier.fillMaxWidth().fillMaxHeight(.9f).imePadding().padding(horizontal = 20.dp)) {
            Text(stringResource(R.string.social_post_title), style = LTitle)
            if (busy) LinearProgressIndicator(Modifier.fillMaxWidth())
            if (error) TextButton(onClick = { work { load() } }) { Text(stringResource(R.string.social_error)) }
            LazyColumn(Modifier.weight(1f), state = listState, verticalArrangement = Arrangement.spacedBy(16.dp),
                contentPadding = PaddingValues(vertical = 16.dp)) {
                item(key = "post") {
                    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                        CommunityAuthor(post.author, api, Modifier.fillMaxWidth().clickable { onProfile(post.author.id) })
                        post.media?.let { CommunityPostMedia(it, api, detail = true) }
                        if (post.body.isNotBlank()) Text(post.body, style = LBody)
                        HorizontalDivider()
                        Text(stringResource(R.string.social_comments), style = LBody, fontWeight = FontWeight.SemiBold)
                    }
                }
                if (items.isEmpty() && !busy) item { Text(stringResource(R.string.social_no_comments), color = AureaColors.LightTextMuted) }
                items(items, key = { it.id }) { c -> Column(Modifier.padding(start = if (c.replyName == null) 0.dp else 16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    CommunityAuthor(c.author, api, Modifier.fillMaxWidth().clickable(enabled = !busy) { onProfile(c.author.id) })
                    c.replyName?.let { Text(stringResource(R.string.social_reply_to, it), style = LNote, color = AureaColors.LightAccentInk) }
                    Text(c.body, modifier = Modifier.testTag("social.comment.message.${c.id}"))
                    Row {
                        if (replies) TextButton(enabled = !busy, onClick = { replyTo = c },
                            modifier = Modifier.testTag("social.comment.reply.${c.id}")) { Text(stringResource(R.string.social_reply)) }
                        if (c.author.id == own?.id) TextButton(enabled = !busy, onClick = { work {
                            withContext(Dispatchers.IO) { api.request("/comments/${c.id}", "DELETE") }
                            onCommentDelta(-1); load()
                        } }) { Text(stringResource(R.string.social_delete)) }
                        else {
                        TextButton(enabled = !busy, onClick = { onReport(c) }, modifier = Modifier.testTag("social.comment.report.${c.id}")) { Text(stringResource(R.string.mod_report), color = AureaColors.LightTextMuted) }
                        if (canModerate) TextButton(enabled = !busy, onClick = { removing = c }, modifier = Modifier.testTag("social.comment.remove.${c.id}")) { Text(stringResource(R.string.mod_remove), color = AureaColors.LightDanger) }
                        }
                    }
                } }
                if (cursor != null) item { TextButton(enabled = !busy, onClick = { work { load(true) } }) { Text(stringResource(R.string.social_more)) } }
            }
            replyTo?.let { parent ->
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.social_reply_to, parent.author.name.ifBlank { parent.author.username }),
                        style = LSmall, modifier = Modifier.weight(1f))
                    TextButton(enabled = !busy, onClick = { replyTo = null }) { Text(stringResource(R.string.common_cancel)) }
                }
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                OutlinedTextField(text, { text = it.take(1000) }, label = { Text(stringResource(R.string.social_comment_hint)) }, modifier = Modifier.weight(1f).testTag("social.comment.body"), maxLines = 3)
                TextButton(enabled = !busy && text.isNotBlank(), onClick = { work {
                    val parent = replyTo
                    val message = text.trim()
                    val request = JSONObject().put("body", message)
                    parent?.let { request.put("replyTo", it.id) }
                    val id = withContext(Dispatchers.IO) { api.request("/posts/${post.id}/comments", "POST", request).getString("id") }
                    focus.clearFocus()
                    keyboard?.hide()
                    text = ""; replyTo = null; onCommentDelta(1)
                    if (cursor == null || own == null) load()
                    else items = items + CommunityComment(id, message, System.currentTimeMillis(), own,
                        parent?.let { it.author.name.ifBlank { it.author.username } })
                    submittedComment = id
                } }) { Text(stringResource(R.string.social_send)) }
            }
            Spacer(Modifier.height(24.dp))
        }
    }
    // Remoção pelo admin de um comentário alheio: confirma antes (o próprio apaga direto, como sempre).
    removing?.let { c -> AlertDialog(onDismissRequest = { removing = null }, title = { Text(stringResource(R.string.mod_remove)) }, text = { Text(c.body.take(160)) },
        confirmButton = { TextButton(modifier = Modifier.testTag("social.comment.remove.confirm"), onClick = { removing = null
            work { withContext(Dispatchers.IO) { api.request("/comments/${c.id}", "DELETE") }; onCommentDelta(-1); load() } }) { Text(stringResource(R.string.social_delete)) } },
        dismissButton = { TextButton(onClick = { removing = null }) { Text(stringResource(R.string.editor_fechar)) } }) }
}

// Omega home: os estilos do social nos papéis claros (escuros nos outros temas).
private val LTitle get() = AureaType.ScreenTitle.copy(color = AureaColors.LightText)
private val LBody get() = AureaType.Body.copy(color = AureaColors.LightText)
private val LSmall get() = AureaType.BodySmall.copy(color = AureaColors.LightTextMuted)
private val LNote get() = AureaType.Note.copy(color = AureaColors.LightTextMuted)
