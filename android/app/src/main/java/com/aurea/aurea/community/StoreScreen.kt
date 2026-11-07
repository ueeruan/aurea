package com.aurea.aurea.community

import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.effects.PluginOverlays
import com.aurea.aurea.effects.PluginRuntime
import com.aurea.aurea.presets.PresetKind
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.*
import kotlinx.coroutines.*
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.UUID

/**
 * 0.4.6 · Loja da Comunidade: plugins, efeitos e presets feitos por outras
 * pessoas. Instalar um plugin passa pela mesma folha de permissões do
 * "Instalar plugin" (PluginRuntime.beginInstall); um preset vai direto para
 * Meus Presets. Plugin publicado só aparece depois da aprovação de um admin.
 * Par do iOS: StoreView.swift.
 */

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun StoreScreen(store: EditorStore, api: CommunityApi, canReview: Boolean, hasProfile: Boolean,
    onEditProfile: () -> Unit, onClose: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var type by remember { mutableStateOf("") }          // "" = tudo
    var popular by remember { mutableStateOf(false) }
    var scope2 by remember { mutableStateOf("all") }     // all | mine | pending
    var query by remember { mutableStateOf("") }
    var items by remember { mutableStateOf(emptyList<StoreItem>()) }
    var cursor by remember { mutableStateOf<String?>(null) }
    var busy by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    var open by remember { mutableStateOf<StoreItem?>(null) }
    var publishing by remember { mutableStateOf(false) }
    fun failure(e: Exception) {
        error = context.getString(when ((e as? CommunityFailure)?.code) {
            "storage_unavailable" -> R.string.store_err_storage
            "invalid_plugin" -> R.string.store_err_plugin
            "invalid_preset", "invalid_package" -> R.string.store_err_package
            "invalid_image", "invalid_photo" -> R.string.store_err_photo
            "plugin_id_taken" -> R.string.store_err_taken
            "version_not_newer" -> R.string.store_err_version
            "file_too_large" -> R.string.social_file_limit
            "profile_required" -> R.string.social_profile_required
            "rate_limited" -> R.string.app_community_limit
            "unauthorized" -> R.string.social_session_error
            else -> R.string.social_error
        })
    }
    fun work(action: suspend () -> Unit) {
        if (busy) return
        scope.launch { busy = true; error = null
            try { action() } catch (e: CancellationException) { throw e } catch (e: Exception) { failure(e) } finally { busy = false }
        }
    }
    suspend fun load(more: Boolean = false) {
        val q = buildString {
            append("?sort=").append(if (popular) "popular" else "new")
            if (type.isNotEmpty()) append("&type=").append(type)
            if (query.isNotBlank()) append("&q=").append(CommunityApi.query(query.trim()))
            if (scope2 == "mine") append("&mine=1") else if (scope2 == "pending") append("&status=pending")
            if (more && cursor != null) append("&cursor=").append(CommunityApi.query(cursor!!))
        }
        val r = withContext(Dispatchers.IO) { api.request("/store$q") }
        val page = r.optJSONArray("items").mapObjects { it.storeItem() }
        items = if (more) (items + page).distinctBy { it.id } else page
        cursor = r.nextCursor()
    }
    // Filtros mudam na hora; a busca espera a pessoa parar de digitar.
    LaunchedEffect(type, popular, scope2, query) {
        if (query.isNotEmpty()) delay(350)
        while (busy) delay(50)
        work { load() }
    }

    Column(Modifier.fillMaxSize().background(AureaColors.LightBackground).testTag("store.screen")) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = onClose, modifier = Modifier.testTag("store.close")) { Text(stringResource(R.string.editor_fechar)) }
            Text(stringResource(R.string.store_title), style = AureaType.ScreenTitle.copy(color = AureaColors.LightText), modifier = Modifier.weight(1f))
            Button(onClick = { if (hasProfile) publishing = true else onEditProfile() }, enabled = !busy,
                modifier = Modifier.heightIn(min = 44.dp).testTag("store.publish")) { Text(stringResource(R.string.store_publish)) }
        }
        OutlinedTextField(query, { query = it.take(40) }, singleLine = true, label = { Text(stringResource(R.string.store_search)) },
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp).testTag("store.search"))
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 16.dp, vertical = 6.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            for ((value, label) in listOf("" to R.string.store_all, "plugin" to R.string.store_plugins, "effect" to R.string.store_effects, "preset" to R.string.store_presets))
                FilterChip(type == value, onClick = { type = value }, label = { Text(stringResource(label)) }, modifier = Modifier.testTag("store.type.${value.ifEmpty { "all" }}"))
            FilterChip(popular, onClick = { popular = !popular }, label = { Text(stringResource(R.string.store_popular)) })
            FilterChip(scope2 == "mine", onClick = { scope2 = if (scope2 == "mine") "all" else "mine" }, label = { Text(stringResource(R.string.store_mine)) },
                modifier = Modifier.testTag("store.mine"))
            if (canReview) FilterChip(scope2 == "pending", onClick = { scope2 = if (scope2 == "pending") "all" else "pending" },
                label = { Text(stringResource(R.string.store_pending)) }, modifier = Modifier.testTag("store.pending"))
        }
        error?.let { Text(it, color = MaterialTheme.colorScheme.error, modifier = Modifier.padding(horizontal = 16.dp).testTag("store.error")) }
        Box(Modifier.weight(1f)) {
            if (busy && items.isEmpty()) CircularProgressIndicator(Modifier.align(Alignment.Center), color = AureaColors.LightAccentInk)
            LazyColumn(Modifier.fillMaxSize().testTag("store.list"), contentPadding = PaddingValues(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                if (!busy && items.isEmpty()) item("empty") {
                    Text(stringResource(R.string.store_empty), color = AureaColors.LightTextMuted, modifier = Modifier.padding(vertical = 24.dp))
                }
                items(items, key = { it.id }) { item -> StoreCard(item, api) { open = item } }
                if (cursor != null) item("more") {
                    TextButton(enabled = !busy, onClick = { work { load(true) } }, modifier = Modifier.fillMaxWidth()) { Text(stringResource(R.string.social_more)) }
                }
            }
        }
    }
    open?.let { item ->
        ModalBottomSheet(onDismissRequest = { open = null }, containerColor = AureaColors.LightBackground) {
            StoreDetail(store, api, item, canReview, busy, onWork = ::work,
                onChanged = { changed -> open = changed; items = items.map { if (it.id == changed.id) changed else it } },
                onRemoved = { open = null; items = items.filterNot { it.id == item.id } })
        }
    }
    if (publishing) ModalBottomSheet(onDismissRequest = { if (!busy) publishing = false }, containerColor = AureaColors.LightBackground) {
        StorePublish(api, busy, error, onWork = ::work, onDone = { created ->
            publishing = false; scope2 = "mine"; items = listOf(created) + items.filterNot { it.id == created.id }
            store.showToast(context.getString(if (created.status == "pending") R.string.store_sent_review else R.string.store_published))
        })
    }
    // A folha de permissões do plugin baixado (a mesma do "Instalar plugin").
    PluginOverlays(store)
}

@Composable
private fun StorePhoto(api: CommunityApi, item: StoreItem, index: Int, modifier: Modifier, maxSide: Int = 720) {
    var bitmap by remember(item.id, index) { mutableStateOf<Bitmap?>(null) }
    LaunchedEffect(item.id, index) { if (index < item.photos) bitmap = withContext(Dispatchers.IO) { runCatching { api.storePhoto(item.id, index, maxSide) }.getOrNull() } }
    Box(modifier.clip(RoundedCornerShape(16.dp)).background(AureaColors.LightSurface), contentAlignment = Alignment.Center) {
        val b = bitmap
        if (b != null) Image(b.asImageBitmap(), contentDescription = null, contentScale = ContentScale.Crop, modifier = Modifier.fillMaxSize())
        else Text(typeLabel(item.type), color = AureaColors.LightTextMuted, style = AureaType.BodySmall)
    }
}

@Composable
private fun typeLabel(type: String) = stringResource(when (type) { "plugin" -> R.string.store_type_plugin; "effect" -> R.string.store_type_effect; else -> R.string.store_type_preset })

@Composable
private fun StoreCard(item: StoreItem, api: CommunityApi, onOpen: () -> Unit) {
    val desc = stringResource(R.string.store_card_a11y, item.name, typeLabel(item.type), item.author.username)
    Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(20.dp)).background(AureaColors.LightSurface).clickable(onClick = onOpen)
        .semantics { contentDescription = desc }.padding(12.dp).testTag("store.item.${item.id}"),
        horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
        StorePhoto(api, item, 0, Modifier.size(88.dp), maxSide = 256)
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(item.name, style = AureaType.Body.copy(color = AureaColors.LightText, fontWeight = FontWeight.SemiBold), maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text("@${item.author.username} · ${typeLabel(item.type)}", style = AureaType.BodySmall.copy(color = AureaColors.LightTextMuted), maxLines = 1)
            if (item.description.isNotBlank()) Text(item.description, style = AureaType.BodySmall.copy(color = AureaColors.LightText), maxLines = 2, overflow = TextOverflow.Ellipsis)
            Text(stringResource(R.string.store_stats, item.downloads, item.likes) + statusSuffix(item),
                style = AureaType.Note.copy(color = AureaColors.LightTextMuted))
        }
    }
}

@Composable
private fun statusSuffix(item: StoreItem): String = when (item.status) {
    "pending" -> " · " + stringResource(R.string.store_status_pending)
    "rejected" -> " · " + stringResource(R.string.store_status_rejected)
    else -> ""
}

/** Salva um preset baixado em Meus Presets, sem sobrescrever um de mesmo nome. */
internal fun saveDownloadedPreset(store: EditorStore, source: String, fallbackName: String): Boolean {
    val obj = JSONObject(source)
    val kind = when (obj.getString("kind")) { "effects" -> PresetKind.Effects; "text" -> PresetKind.Text; "animation" -> PresetKind.Animation
        "caption" -> PresetKind.Caption; "curve" -> PresetKind.Curve; else -> return false }
    val name = obj.optString("name").ifBlank { fallbackName }
    var free = name; var n = 2
    while (store.presets.exists(kind, free)) { free = "$name ($n)"; n++ }
    return store.presets.save(kind, free, source) != null
}

@Composable
private fun StoreDetail(store: EditorStore, api: CommunityApi, item: StoreItem, canReview: Boolean, busy: Boolean,
    onWork: (suspend () -> Unit) -> Unit, onChanged: (StoreItem) -> Unit, onRemoved: () -> Unit) {
    val context = LocalContext.current
    var reporting by remember { mutableStateOf(false) }
    var confirmDelete by remember { mutableStateOf(false) }
    var note by remember { mutableStateOf("") }
    Column(Modifier.fillMaxWidth().fillMaxHeight(.9f).verticalScroll(rememberScrollState()).padding(horizontal = 20.dp, vertical = 8.dp)
        .testTag("store.detail"), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        if (item.photos > 0) LazyRow(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            items((0 until item.photos).toList()) { i ->
                StorePhoto(api, item, i, Modifier.width(260.dp).aspectRatio(4f / 5f).semantics {
                    contentDescription = context.getString(R.string.store_photo_a11y, i + 1, item.photos) })
            }
        }
        Text(item.name, style = AureaType.ScreenTitle.copy(color = AureaColors.LightText))
        Text("@${item.author.username} · ${typeLabel(item.type)}" + (if (item.version.isNotEmpty()) " · v${item.version}" else ""),
            style = AureaType.BodySmall.copy(color = AureaColors.LightTextMuted))
        Text(stringResource(R.string.store_stats, item.downloads, item.likes) + statusSuffix(item), style = AureaType.BodySmall.copy(color = AureaColors.LightTextMuted))
        if (item.reviewNote.isNotBlank() && item.status != "approved") Text(stringResource(R.string.store_review_note, item.reviewNote),
            style = AureaType.BodySmall.copy(color = MaterialTheme.colorScheme.error))
        if (item.description.isNotBlank()) Text(item.description, style = AureaType.Body.copy(color = AureaColors.LightText))
        if (item.format == "plugin") {
            // O que o plugin pede (o aparelho pergunta de novo na instalação).
            val needs = buildList {
                if (item.hasScript) add(stringResource(R.string.store_perm_script))
                if (item.network.isNotEmpty()) add(stringResource(R.string.store_perm_network, item.network.joinToString(", ")))
                if (item.clipboard) add(stringResource(R.string.store_perm_clipboard))
                if (item.files) add(stringResource(R.string.store_perm_files))
            }
            Text(if (needs.isEmpty()) stringResource(R.string.store_perm_none) else needs.joinToString("\n"),
                style = AureaType.BodySmall.copy(color = AureaColors.LightTextMuted), modifier = Modifier.testTag("store.permissions"))
        }
        if (item.tutorialUrl.startsWith("https://")) OutlinedButton(onClick = {
            runCatching { context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(item.tutorialUrl)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) }
        }, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp).testTag("store.tutorial")) { Text(stringResource(R.string.store_tutorial)) }
        Button(enabled = !busy && (item.status == "approved" || item.mine || canReview), onClick = { onWork {
            val ext = if (item.format == "plugin") "aureaplugin" else "json"
            val target = File(context.cacheDir, "store-${UUID.randomUUID()}.$ext")
            withContext(Dispatchers.IO) { api.storeDownload(item, target) }
            if (item.format == "plugin") {
                // Mesmo caminho do arquivo local: lê o pacote e abre o consentimento.
                PluginRuntime.beginInstall(Uri.fromFile(target))
            } else try {
                val source = withContext(Dispatchers.IO) { target.readText() }
                if (!saveDownloadedPreset(store, source, item.name)) throw CommunityFailure("invalid_preset")
                store.showToast(context.getString(R.string.social_saved))
            } finally { target.delete() }
            onChanged(item.copy(downloads = item.downloads + if (item.mine) 0 else 1))
        } }, modifier = Modifier.fillMaxWidth().heightIn(min = 52.dp).testTag("store.install")) {
            Text(stringResource(if (item.format == "plugin") R.string.store_install else R.string.store_get_preset))
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            if (item.status == "approved") OutlinedButton(enabled = !busy, onClick = { onWork {
                val r = withContext(Dispatchers.IO) { api.request("/store/${item.id}/like", if (item.liked) "DELETE" else "PUT") }
                onChanged(item.copy(likes = r.getInt("likes"), liked = r.getBoolean("liked")))
            } }, modifier = Modifier.weight(1f).heightIn(min = 48.dp).testTag("store.like")) {
                Text(stringResource(if (item.liked) R.string.store_unlike else R.string.store_like))
            }
            if (!item.mine) OutlinedButton(enabled = !busy, onClick = { reporting = true }, modifier = Modifier.weight(1f).heightIn(min = 48.dp).testTag("store.report")) {
                Text(stringResource(R.string.store_report))
            }
            if (item.mine || canReview) OutlinedButton(enabled = !busy, onClick = { confirmDelete = true }, modifier = Modifier.weight(1f).heightIn(min = 48.dp).testTag("store.delete")) {
                Text(stringResource(R.string.store_delete))
            }
        }
        if (canReview) {
            // Revisão (admin): aprovar ou recusar com um motivo que o criador vê.
            OutlinedTextField(note, { note = it.take(500) }, label = { Text(stringResource(R.string.store_review_reason)) }, modifier = Modifier.fillMaxWidth())
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                for ((status, label) in listOf("approved" to R.string.store_approve, "rejected" to R.string.store_reject))
                    Button(enabled = !busy && item.status != status, onClick = { onWork {
                        val r = withContext(Dispatchers.IO) { api.request("/store/${item.id}/review", "PUT", JSONObject().put("status", status).put("note", note)) }
                        onChanged(r.getJSONObject("item").storeItem())
                    } }, modifier = Modifier.weight(1f).heightIn(min = 48.dp).testTag("store.review.$status")) { Text(stringResource(label)) }
            }
        }
        Spacer(Modifier.height(24.dp))
    }
    if (reporting) ReportDialog(onDismiss = { reporting = false }, onSent = { reporting = false; store.showToast(context.getString(R.string.mod_report_thanks)) }) { reason, details ->
        withContext(Dispatchers.IO) { api.report("store", item.id, reason, details) }
    }
    if (confirmDelete) AlertDialog(onDismissRequest = { confirmDelete = false },
        title = { Text(stringResource(R.string.store_delete_title)) }, text = { Text(stringResource(R.string.store_delete_message)) },
        confirmButton = { TextButton(onClick = { confirmDelete = false; onWork {
            withContext(Dispatchers.IO) { api.request("/store/${item.id}", "DELETE") }; onRemoved()
        } }) { Text(stringResource(R.string.store_delete)) } },
        dismissButton = { TextButton(onClick = { confirmDelete = false }) { Text(stringResource(R.string.editor_cancelar)) } })
}

/** Lê uma foto escolhida e a regrava em JPEG de até 2048 px (o servidor aceita até 3 MB). */
private fun preparePhoto(context: android.content.Context, uri: Uri, target: File): Boolean {
    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, bounds) } ?: return false
    if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return false
    var sample = 1
    while (maxOf(bounds.outWidth, bounds.outHeight) / (sample * 2) >= 2048) sample *= 2
    val decoded = context.contentResolver.openInputStream(uri)?.use {
        BitmapFactory.decodeStream(it, null, BitmapFactory.Options().apply { inSampleSize = sample })
    } ?: return false
    val scale = minOf(1f, 2048f / maxOf(decoded.width, decoded.height))
    val bitmap = if (scale < 1f) Bitmap.createScaledBitmap(decoded, (decoded.width * scale).toInt().coerceAtLeast(1), (decoded.height * scale).toInt().coerceAtLeast(1), true) else decoded
    var quality = 88
    while (true) {
        target.outputStream().use { bitmap.compress(Bitmap.CompressFormat.JPEG, quality, it) }
        if (target.length() <= 3L * 1024 * 1024 || quality <= 50) break
        quality -= 12
    }
    if (bitmap !== decoded) bitmap.recycle()
    decoded.recycle()
    return target.length() in 1..3L * 1024 * 1024
}

@Composable
private fun StorePublish(api: CommunityApi, busy: Boolean, error: String?, onWork: (suspend () -> Unit) -> Unit, onDone: (StoreItem) -> Unit) {
    val context = LocalContext.current
    var pkg by remember { mutableStateOf<JSONObject?>(null) }      // resposta do upload do pacote
    var photos by remember { mutableStateOf(emptyList<String>()) }
    var name by remember { mutableStateOf("") }
    var description by remember { mutableStateOf("") }
    var tutorial by remember { mutableStateOf("") }
    val pickPackage = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri -> if (uri != null) onWork {
        val display = runCatching { context.contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) c.getString(0) else null } }.getOrNull() ?: "pacote"
        val safe = File(display).name.replace(Regex("[^A-Za-z0-9._ -]"), "_")
        val fileName = if (safe.endsWith(".json", true) || safe.endsWith(".aureaplugin", true)) safe else "$safe.aureaplugin"
        val temp = File(context.cacheDir, "store-up-${UUID.randomUUID()}")
        try {
            withContext(Dispatchers.IO) {
                context.contentResolver.openInputStream(uri)?.use { input -> temp.outputStream().use { input.copyTo(it) } } ?: throw CommunityFailure("invalid_package")
            }
            val r = withContext(Dispatchers.IO) { api.storeUpload(temp, "package", fileName) }
            pkg = r
            val meta = r.optJSONObject("meta")
            if (name.isBlank()) name = meta?.optString("name").orEmpty().take(64)
            if (description.isBlank()) description = meta?.optString("description").orEmpty()
        } finally { temp.delete() }
    } }
    val pickPhotos = rememberLauncherForActivityResult(ActivityResultContracts.PickMultipleVisualMedia(4)) { uris -> if (uris.isNotEmpty()) onWork {
        val ids = mutableListOf<String>()
        for (uri in uris.take(4 - photos.size)) {
            val temp = File(context.cacheDir, "store-photo-${UUID.randomUUID()}.jpg")
            try {
                if (!withContext(Dispatchers.IO) { preparePhoto(context, uri, temp) }) throw CommunityFailure("invalid_image")
                ids += withContext(Dispatchers.IO) { api.storeUpload(temp, "photo", "foto.jpg") }.getString("id")
            } finally { temp.delete() }
        }
        photos = (photos + ids).take(4)
    } }
    Column(Modifier.fillMaxWidth().fillMaxHeight(.9f).verticalScroll(rememberScrollState()).padding(horizontal = 20.dp, vertical = 8.dp)
        .testTag("store.publish.sheet"), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text(stringResource(R.string.store_publish_title), style = AureaType.ScreenTitle.copy(color = AureaColors.LightText))
        Text(stringResource(R.string.store_publish_hint), style = AureaType.BodySmall.copy(color = AureaColors.LightTextMuted))
        OutlinedButton(enabled = !busy, onClick = { runCatching { pickPackage.launch(arrayOf("*/*")) } },
            modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp).testTag("store.publish.package")) {
            Text(pkg?.let { stringResource(R.string.store_package_ready, it.optString("name")) } ?: stringResource(R.string.store_pick_package))
        }
        pkg?.optJSONObject("meta")?.let { meta ->
            if (meta.optString("format") == "plugin") Text(stringResource(R.string.store_plugin_review_note), style = AureaType.BodySmall.copy(color = AureaColors.LightTextMuted))
        }
        OutlinedTextField(name, { name = it.take(64) }, label = { Text(stringResource(R.string.store_field_name)) }, singleLine = true,
            modifier = Modifier.fillMaxWidth().testTag("store.publish.name"))
        OutlinedTextField(description, { description = it.take(2000) }, label = { Text(stringResource(R.string.store_field_description)) }, minLines = 3,
            modifier = Modifier.fillMaxWidth().testTag("store.publish.description"))
        OutlinedTextField(tutorial, { tutorial = it.take(300).trim() }, label = { Text(stringResource(R.string.store_field_tutorial)) }, singleLine = true,
            isError = tutorial.isNotEmpty() && !tutorial.startsWith("https://"),
            supportingText = { Text(stringResource(R.string.store_field_tutorial_hint)) },
            modifier = Modifier.fillMaxWidth().testTag("store.publish.tutorial"))
        OutlinedButton(enabled = !busy && photos.size < 4, onClick = {
            runCatching { pickPhotos.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly)) }
        }, modifier = Modifier.fillMaxWidth().heightIn(min = 48.dp).testTag("store.publish.photos")) {
            Text(stringResource(R.string.store_add_photos, photos.size))
        }
        if (photos.isNotEmpty()) TextButton(enabled = !busy, onClick = { photos = emptyList() }) { Text(stringResource(R.string.store_clear_photos)) }
        error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
        val ready = pkg != null && name.isNotBlank() && (tutorial.isEmpty() || tutorial.startsWith("https://"))
        Button(enabled = !busy && ready, onClick = { onWork {
            val body = JSONObject().put("package", pkg!!.getString("id")).put("name", name.trim()).put("description", description.trim())
                .put("tutorialUrl", tutorial).put("photos", JSONArray(photos))
            val r = withContext(Dispatchers.IO) { api.request("/store", "POST", body) }
            onDone(r.getJSONObject("item").storeItem())
        } }, modifier = Modifier.fillMaxWidth().heightIn(min = 52.dp).testTag("store.publish.send")) {
            if (busy) CircularProgressIndicator(Modifier.size(20.dp), strokeWidth = 2.dp) else Text(stringResource(R.string.store_publish_send))
        }
        Spacer(Modifier.height(24.dp))
    }
}
