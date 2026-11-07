package com.aurea.aurea.community

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.graphics.Bitmap
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.widget.VideoView
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.core.content.edit
import com.aurea.aurea.BuildConfig
import com.aurea.aurea.R
import com.aurea.aurea.conta.ContaViewModel
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

private tailrec fun Context.activity(): Activity? = when (this) {
    is Activity -> this
    is ContextWrapper -> baseContext.activity()
    else -> null
}

/** A faixa grande do Mural no topo do Início: é o que chama para a aba. */
@Composable
internal fun MuralBanner(onOpen: () -> Unit) {
    Box(
        Modifier
            .fillMaxWidth()
            .padding(horizontal = 16.dp, vertical = 8.dp)
            .height(104.dp)
            .clip(RoundedCornerShape(20.dp))
            .background(Brush.linearGradient(listOf(AureaColors.Accent, AureaColors.Brand)))
            .clickable(onClick = onOpen)
            .testTag("home.mural"),
    ) {
        Row(Modifier.fillMaxSize().padding(horizontal = 20.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(stringResource(R.string.mural_title), color = AureaColors.OnAccent, fontSize = 30.sp, fontWeight = FontWeight.Bold)
                Text(stringResource(R.string.mural_tagline), color = AureaColors.OnAccent, fontSize = 14.sp, maxLines = 2)
            }
            CupertinoIcon(CupertinoGlyph.RectangleStack, 34.dp, AureaColors.OnAccent)
        }
    }
}

private data class PickedVideo(val file: File, val thumbFile: File, val thumb: Bitmap, val width: Int, val height: Int, val durationMs: Long)

/** Copia o vídeo para o cache e tira a capa e as medidas. Nulo = o arquivo não abre. */
private fun prepare(context: Context, uri: Uri): PickedVideo? = runCatching {
    val file = File(context.cacheDir, "mural-upload.mp4")
    context.contentResolver.openInputStream(uri)?.use { input -> file.outputStream().use { input.copyTo(it) } } ?: return null
    val retriever = MediaMetadataRetriever()
    try {
        retriever.setDataSource(file.absolutePath)
        fun number(key: Int) = retriever.extractMetadata(key)?.toLongOrNull() ?: 0L
        val rotation = number(MediaMetadataRetriever.METADATA_KEY_VIDEO_ROTATION).toInt()
        val w = number(MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH).toInt()
        val h = number(MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT).toInt()
        val frame = retriever.getFrameAtTime(1_000_000) ?: return null
        val scale = 540f / maxOf(frame.width, frame.height)
        val thumb = if (scale < 1f) Bitmap.createScaledBitmap(frame, (frame.width * scale).toInt(), (frame.height * scale).toInt(), true) else frame
        val thumbFile = File(context.cacheDir, "mural-upload.jpg")
        thumbFile.outputStream().use { thumb.compress(Bitmap.CompressFormat.JPEG, 80, it) }
        PickedVideo(file, thumbFile, thumb, if (rotation % 180 == 0) w else h, if (rotation % 180 == 0) h else w,
            number(MediaMetadataRetriever.METADATA_KEY_DURATION))
    } finally { retriever.release() }
}.getOrNull()

@Composable
internal fun MuralScreen(store: EditorStore, conta: ContaViewModel, onBack: () -> Unit) {
    com.aurea.aurea.home.HomeMaterial { MuralContent(store, conta, onBack) }
}

@Composable
private fun MuralContent(store: EditorStore, conta: ContaViewModel, onBack: () -> Unit) {
    BackHandler(onBack = onBack)
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val token = conta.sessao()?.token.orEmpty()
    val api = remember(token) { MuralApi(token) }
    val reports = remember(token) { CommunityApi(token) }
    val posts = remember { mutableStateListOf<MuralPost>() }
    var cursor by remember { mutableStateOf<String?>(null) }
    var failed by remember { mutableStateOf(false) }
    var config by remember { mutableStateOf<MuralConfig?>(null) }
    var billing by remember { mutableStateOf<MuralBilling?>(null) }
    var price by remember { mutableStateOf<String?>(null) }
    var playing by remember { mutableStateOf<MuralPost?>(null) }
    var picked by remember { mutableStateOf<PickedVideo?>(null) }
    var reporting by remember { mutableStateOf<MuralPost?>(null) }
    var removing by remember { mutableStateOf<MuralPost?>(null) }
    var stage by remember { mutableStateOf<Int?>(null) }
    var progress by remember { mutableFloatStateOf(0f) }
    val prefs = remember { context.getSharedPreferences("aurea.mural", Context.MODE_PRIVATE) }
    DisposableEffect(billing) { onDispose { billing?.close() } }

    suspend fun load(reset: Boolean) {
        failed = false
        runCatching { withContext(Dispatchers.IO) { api.feed(if (reset) null else cursor) } }
            .onSuccess { if (reset) posts.clear(); posts.addAll(it.items); cursor = it.cursor }
            .onFailure { failed = true }
    }
    LaunchedEffect(api) {
        load(true)
        val loaded = runCatching { withContext(Dispatchers.IO) { api.config() } }.getOrNull()
        config = loaded
        if (loaded != null && loaded.posting && BuildConfig.PLAY_STORE) {
            val client = MuralBilling(context, loaded.productId)
            billing = client
            price = runCatching { client.details()?.oneTimePurchaseOfferDetails?.formattedPrice }.getOrNull()
        }
    }

    val pick = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { uri ->
        if (uri != null) scope.launch {
            stage = R.string.mural_preparing
            val video = withContext(Dispatchers.IO) { prepare(context, uri) }
            stage = null
            when {
                video == null -> store.showToast(context.getString(R.string.mural_failed_open))
                video.file.length() > (config?.maxBytes ?: 0L) -> store.showToast(context.getString(R.string.mural_too_big))
                else -> picked = video
            }
        }
    }
    fun startPost() {
        when {
            !BuildConfig.PLAY_STORE -> store.showToast(context.getString(R.string.mural_store_only))
            config?.posting != true -> store.showToast(context.getString(R.string.mural_unavailable))
            token.isEmpty() -> store.showToast(context.getString(R.string.mural_sign_in))
            else -> pick.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.VideoOnly))
        }
    }
    fun send(video: PickedVideo, caption: String) {
        val client = billing ?: return
        val activity = context.activity() ?: return
        scope.launch {
            try {
                // Crédito guardado: já pagou e a publicação caiu. Não cobra de novo.
                var credit = prefs.getString("credit", null)
                if (credit == null) {
                    stage = R.string.mural_paying
                    val purchase = client.unconsumed() ?: client.details()?.let { client.buy(activity, it) }
                    if (purchase == null) { stage = null; return@launch }
                    if (purchase.purchaseState != com.android.billingclient.api.Purchase.PurchaseState.PURCHASED) {
                        stage = null; store.showToast(context.getString(R.string.mural_pending)); return@launch
                    }
                    credit = withContext(Dispatchers.IO) { api.credit(purchase.purchaseToken) }
                    prefs.edit { putString("credit", credit) }
                    client.consume(purchase)
                }
                stage = R.string.mural_uploading
                withContext(Dispatchers.IO) {
                    val id = api.create(credit, caption, video.file.length(), video.width, video.height, video.durationMs)
                    api.upload(id, "video", video.file) { progress = it }
                    api.upload(id, "thumb", video.thumbFile)
                    api.publish(id)
                }
                prefs.edit { remove("credit") }
                picked = null
                store.showToast(context.getString(R.string.mural_done))
                load(true)
            } catch (e: Exception) {
                val code = (e as? CommunityFailure)?.code
                if (code == "credit_used") prefs.edit { remove("credit") }
                store.showToast(context.getString(if (code == "profile_required") R.string.mural_profile else R.string.mural_failed))
            } finally { stage = null; progress = 0f }
        }
    }

    Column(Modifier.fillMaxSize().background(AureaColors.LightBackground).testTag("mural.screen")) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = onBack) { Text(stringResource(R.string.mural_back), color = AureaColors.LightText) }
            Spacer(Modifier.weight(1f))
            Text(stringResource(R.string.mural_title), color = AureaColors.LightText, fontSize = 22.sp, fontWeight = FontWeight.Bold)
            Spacer(Modifier.weight(1f))
            Spacer(Modifier.width(64.dp))
        }
        Box(
            Modifier.fillMaxWidth().padding(horizontal = 16.dp).height(52.dp).clip(RoundedCornerShape(14.dp))
                .background(AureaColors.Accent).clickable(enabled = stage == null) { startPost() }.testTag("mural.post"),
            contentAlignment = Alignment.Center,
        ) {
            val label = price?.let { stringResource(R.string.mural_post_price, it) } ?: stringResource(R.string.mural_post)
            Text(label, color = AureaColors.OnAccent, fontSize = 16.sp, fontWeight = FontWeight.SemiBold)
        }
        stage?.let { s ->
            Column(Modifier.fillMaxWidth().padding(16.dp)) {
                Text(stringResource(s), color = AureaColors.LightTextMuted, fontSize = 13.sp)
                Spacer(Modifier.height(6.dp))
                LinearProgressIndicator(progress = { progress }, modifier = Modifier.fillMaxWidth())
            }
        }
        if (posts.isEmpty()) {
            Box(Modifier.fillMaxSize().padding(32.dp), contentAlignment = Alignment.Center) {
                Text(stringResource(if (failed) R.string.mural_load_failed else R.string.mural_empty),
                    color = AureaColors.LightTextMuted, fontSize = 15.sp)
            }
        } else {
            LazyVerticalGrid(GridCells.Fixed(2), Modifier.fillMaxSize().padding(top = 12.dp), horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp), contentPadding = androidx.compose.foundation.layout.PaddingValues(12.dp)) {
                items(posts, key = { it.id }) { post ->
                    MuralCard(api, post, onPlay = { playing = post }, onMore = { if (post.mine) removing = post else reporting = post })
                }
                if (cursor != null) item(span = { GridItemSpan(2) }) {
                    LaunchedEffect(cursor) { load(false) }
                    Spacer(Modifier.height(1.dp))
                }
            }
        }
    }
    playing?.let { post ->
        Dialog(onDismissRequest = { playing = null }, properties = DialogProperties(usePlatformDefaultWidth = false)) {
            Box(Modifier.fillMaxSize().background(Color.Black), contentAlignment = Alignment.Center) {
                AndroidView(factory = { VideoView(it).apply {
                    setVideoURI(Uri.parse(api.videoUrl(post.id)))
                    setOnPreparedListener { player -> player.isLooping = true; start() }
                } }, modifier = Modifier.fillMaxWidth().aspectRatio(post.width.toFloat() / maxOf(post.height, 1)))
            }
        }
    }
    picked?.let { video ->
        var caption by remember(video) { mutableStateOf("") }
        AlertDialog(onDismissRequest = { if (stage == null) picked = null },
            title = { Text(stringResource(R.string.mural_post)) },
            text = { Column {
                Image(video.thumb.asImageBitmap(), null, Modifier.fillMaxWidth().height(180.dp).clip(RoundedCornerShape(12.dp)),
                    contentScale = ContentScale.Fit)
                Spacer(Modifier.height(10.dp))
                OutlinedTextField(caption, { if (it.length <= 200) caption = it }, label = { Text(stringResource(R.string.mural_caption)) },
                    maxLines = 3, modifier = Modifier.fillMaxWidth())
            } },
            confirmButton = { TextButton(enabled = stage == null, onClick = { send(video, caption.trim()) }) {
                Text(price?.let { stringResource(R.string.mural_post_price, it) } ?: stringResource(R.string.mural_post))
            } },
            dismissButton = { TextButton(enabled = stage == null, onClick = { picked = null }) { Text(stringResource(R.string.editor_fechar)) } })
    }
    reporting?.let { post ->
        ReportDialog(onDismiss = { reporting = null }, onSent = {
            reporting = null; store.showToast(context.getString(R.string.mod_report_thanks))
        }) { reason, details -> withContext(Dispatchers.IO) { reports.report("mural", post.id, reason, details) } }
    }
    removing?.let { post ->
        AlertDialog(onDismissRequest = { removing = null }, title = { Text(stringResource(R.string.mural_delete)) },
            confirmButton = { TextButton(onClick = {
                removing = null
                scope.launch {
                    runCatching { withContext(Dispatchers.IO) { api.remove(post.id) } }
                        .onSuccess { posts.remove(post) }
                        .onFailure { store.showToast(context.getString(R.string.mural_load_failed)) }
                }
            }) { Text(stringResource(R.string.mural_delete)) } },
            dismissButton = { TextButton(onClick = { removing = null }) { Text(stringResource(R.string.editor_fechar)) } })
    }
}

@Composable
private fun MuralCard(api: MuralApi, post: MuralPost, onPlay: () -> Unit, onMore: () -> Unit) {
    val thumb by produceState<Bitmap?>(null, post.id) {
        value = withContext(Dispatchers.IO) { runCatching { api.thumb(post.id) }.getOrNull() }
    }
    Column(Modifier.clip(RoundedCornerShape(14.dp)).background(AureaColors.LightSurface).testTag("mural.card")) {
        Box(Modifier.fillMaxWidth().aspectRatio(9f / 14f).background(Color.Black).clickable(onClick = onPlay), contentAlignment = Alignment.Center) {
            thumb?.let { Image(it.asImageBitmap(), null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop) }
            CupertinoIcon(CupertinoGlyph.Plus, 1.dp, Color.Transparent)
        }
        Row(Modifier.fillMaxWidth().padding(start = 10.dp, end = 2.dp, top = 6.dp, bottom = 6.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("@" + post.author.username, color = AureaColors.LightText, fontSize = 13.sp, fontWeight = FontWeight.SemiBold,
                    maxLines = 1, overflow = TextOverflow.Ellipsis)
                if (post.caption.isNotEmpty()) Text(post.caption, color = AureaColors.LightTextMuted, fontSize = 12.sp, maxLines = 2,
                    overflow = TextOverflow.Ellipsis)
            }
            Box(Modifier.size(36.dp).clickable(onClick = onMore), contentAlignment = Alignment.Center) {
                Text("⋯", color = AureaColors.LightTextMuted, fontSize = 18.sp)
            }
        }
    }
}
