package com.aurea.aurea.editor

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.os.Build
import android.provider.Settings
import android.widget.VideoView
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.core.Easing
import androidx.compose.animation.core.animate
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.key
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.nestedscroll.NestedScrollConnection
import androidx.compose.ui.input.nestedscroll.NestedScrollSource
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.Velocity
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaDialog
import com.aurea.aurea.ui.ds.DialogAction
import com.aurea.aurea.ui.ds.DialogRole
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.Locale
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.roundToLong

/** Overshoot (tensão 2) do recolher da prévia, como o da referência. */
private val Overshoot = Easing { x -> val t = x - 1f; t * t * (3f * t + 2f) + 1f }

/** O corte de um vídeo escolhido no navegador (µs da fonte). */
private data class Trim(val inUs: Long, val outUs: Long)

/**
 * Omega adicionar (área 08, P1-1): o navegador de mídia em tela cheia ("Ver
 * tudo"). Prévia 1 : 0,7 da mídia em foco, a barra de controle (régua da
 * duração da imagem ou corte do vídeo), a grade de 4 colunas com o álbum no
 * título. Pressão longa entra na seleção múltipla numerada; "Mesmo início" e
 * "Em sequência" adicionam na ordem de escolha, cada mídia com a sua duração
 * ou o seu corte, num passo de desfazer só (motor: EngineOmegaAdd.cpp).
 */
@Composable
internal fun MediaBrowser(store: EditorStore, onAdded: () -> Unit, onDismiss: () -> Unit) {
    val context = LocalContext.current
    val density = LocalDensity.current
    val scope = rememberCoroutineScope()
    var revision by remember { mutableIntStateOf(0) }
    var items by remember { mutableStateOf<List<MediaItem>?>(null) }
    var bucket by remember { mutableStateOf(OmegaMediaPrefs.bucket(context)) }
    var albumsOpen by remember { mutableStateOf(false) }
    var tip by remember { mutableStateOf(!OmegaMediaPrefs.albumTipShown(context)) }
    var focused by remember { mutableStateOf<MediaItem?>(null) }
    var multi by remember { mutableStateOf(false) }
    val selection = remember { mutableStateListOf<MediaItem>() }
    val ownDuration = remember { mutableStateMapOf<Uri, Long>() }
    val trims = remember { mutableStateMapOf<Uri, Trim>() }
    var info by remember { mutableStateOf(false) }
    val full = remember(revision) { context.galleryFull(false) || context.galleryFull(true) }
    val partial = remember(revision) { context.galleryPartial() }
    val permissions = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { revision++ }
    val fps = (store.composition?.fps ?: store.project.fps.toDouble()).takeIf { it > 0 } ?: 30.0
    val maxFrames = (fps * 10 + 50).roundToLong()
    var imageFrames by remember {
        mutableLongStateOf(OmegaMediaPrefs.imageDurationFrames(context).takeIf { it in 1..maxFrames } ?: (fps * 3).roundToLong())
    }
    LaunchedEffect(revision) {
        if (full || partial) items = withContext(Dispatchers.IO) { runCatching { mediaItems(context) }.getOrDefault(emptyList()) }
        else items = emptyList()
    }
    DisposableEffect(Unit) { onDispose { OmegaMediaPrefs.setImageDurationFrames(context, imageFrames) } }
    val allLabel = stringResource(R.string.om_media_all_albums)
    val all = items.orEmpty()
    val albums = remember(all, allLabel) { mediaAlbums(all, allLabel) }
    val bucketId = if (bucket.first == 0L || albums.any { it.id == bucket.first }) bucket.first else 0L
    val shown = remember(all, bucketId) { if (bucketId == 0L) all else all.filter { it.bucketId == bucketId } }
    LaunchedEffect(shown) { if (focused == null || focused !in shown) focused = shown.firstOrNull() }
    val albumName = if (bucketId == 0L) allLabel else albums.firstOrNull { it.id == bucketId }?.name ?: allLabel

    fun durationOf(item: MediaItem): Long = ownDuration[item.uri] ?: imageFrames
    fun pickOf(item: MediaItem): EditorStore.OmegaMediaPick {
        val trim = trims[item.uri]
        return EditorStore.OmegaMediaPick(item.uri, item.video,
            durationFrames = if (item.video) 0 else durationOf(item),
            trimInUs = trim?.inUs ?: 0, trimOutUs = trim?.outUs ?: 0)
    }
    fun add(list: List<MediaItem>, placement: Int) {
        if (list.isEmpty()) return
        OmegaMediaPrefs.markUsed(context, list.map { it.uri })
        OmegaMediaPrefs.setImageDurationFrames(context, imageFrames)
        store.importMediaPicks(list.map(::pickOf), placement)
        onAdded()
        onDismiss()
    }
    fun leaveMulti() { multi = false; selection.clear() }
    fun tap(item: MediaItem) {
        if (!multi) { focused = item; return }
        when {
            item !in selection -> { selection.add(item); focused = item }
            focused == item -> {
                selection.remove(item)
                ownDuration.remove(item.uri); trims.remove(item.uri)
                focused = selection.lastOrNull() ?: item
            }
            else -> focused = item
        }
    }

    Dialog(onDismissRequest = { if (albumsOpen) albumsOpen = false else if (multi) leaveMulti() else onDismiss() },
        properties = DialogProperties(usePlatformDefaultWidth = false, decorFitsSystemWindows = false)) {
        BackHandler(enabled = albumsOpen || multi) { if (albumsOpen) albumsOpen = false else leaveMulti() }
        BoxWithConstraints(Modifier.fillMaxSize().background(AureaColors.AddPanel).statusBarsPadding().navigationBarsPadding().testTag("media.browser")) {
            val previewPx = with(density) { (maxWidth * 0.7f).toPx() }
            var collapsed by remember { mutableFloatStateOf(0f) }
            val connection = remember(previewPx) {
                object : NestedScrollConnection {
                    override fun onPreScroll(available: Offset, source: NestedScrollSource): Offset {
                        val dy = available.y
                        if (dy < 0f && collapsed < previewPx) {
                            val take = max(dy, -(previewPx - collapsed))
                            collapsed -= take
                            return Offset(0f, take)
                        }
                        return Offset.Zero
                    }
                    override fun onPostScroll(consumed: Offset, available: Offset, source: NestedScrollSource): Offset {
                        val dy = available.y
                        if (dy > 0f && collapsed > 0f) {
                            val take = min(dy, collapsed)
                            collapsed -= take
                            return Offset(0f, take)
                        }
                        return Offset.Zero
                    }
                    override suspend fun onPreFling(available: Velocity): Velocity {
                        if (collapsed <= 0f || collapsed >= previewPx) return Velocity.Zero
                        // Soltou: abaixo de 1/3 volta; acima, recolhe. Fling para baixo reabre.
                        val target = if (available.y > 1500f) 0f else if (collapsed > previewPx / 3f) previewPx else 0f
                        val ms = (abs(target - collapsed) / 2f).roundToInt().coerceIn(120, 450)
                        animate(collapsed, target, animationSpec = tween(ms, easing = Overshoot)) { value, _ -> collapsed = value }
                        collapsed = target
                        return available
                    }
                }
            }
            Column(Modifier.fillMaxSize()) {
                if (multi) {
                    MultiBar(selection.size, onClose = ::leaveMulti,
                        onSameStart = { add(selection.toList(), 0) }, onSequence = { add(selection.toList(), 1) })
                } else {
                    TitleBar(albumName, albumsOpen, onClose = onDismiss,
                        onAlbum = { albumsOpen = !albumsOpen; if (tip) { tip = false; OmegaMediaPrefs.setAlbumTipShown(context) } },
                        onMulti = { multi = true; focused?.let { if (it !in selection) selection.add(it) } },
                        onAdd = { focused?.let { add(listOf(it), 0) } }, canAdd = focused != null)
                }
                val previewHeight = with(density) { (previewPx - collapsed.coerceIn(0f, previewPx)).toDp() }
                val item = focused
                if (items == null) {
                    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) { CircularProgressIndicator(Modifier.size(28.dp), color = AureaColors.Accent) }
                } else if (!full && !partial) {
                    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                        AccessNotice {
                            context.startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:${context.packageName}")))
                        }
                    }
                } else {
                    if (previewHeight > 0.5.dp && item != null) {
                        Preview(item, trims[item.uri], Modifier.fillMaxWidth().height(previewHeight), onInfo = { info = true })
                    }
                    if (item != null) {
                        Box(Modifier.fillMaxWidth().height(55.dp).background(AureaColors.AddControlBar)) {
                            if (item.video) {
                                TrimBar(item, trims[item.uri]) { trims[item.uri] = it }
                            } else {
                                val own = ownDuration[item.uri]
                                DurationRuler(own ?: imageFrames, maxFrames, fps, linkVisible = multi, own = own != null,
                                    onLink = {
                                        if (own != null) ownDuration.remove(item.uri) else ownDuration[item.uri] = imageFrames
                                    },
                                ) { frames -> if (own != null) ownDuration[item.uri] = frames else imageFrames = frames }
                            }
                            if (collapsed > 0f) {
                                val showLabel = stringResource(R.string.om_media_show_preview)
                                Box(
                                    Modifier.align(Alignment.TopEnd).size(28.dp).testTag("media.collapse")
                                        .semantics { contentDescription = showLabel }
                                        .tocavel {
                                            scope.launch {
                                                animate(collapsed, 0f, animationSpec = tween(250, easing = Overshoot)) { v, _ -> collapsed = v }
                                                collapsed = 0f
                                            }
                                        },
                                    contentAlignment = Alignment.Center,
                                ) { CupertinoIcon(CupertinoGlyph.ChevronDown, 14.dp, AureaColors.Text) }
                            }
                        }
                    }
                    if (partial && !context.galleryFull(false)) {
                        PartialNotice { permissions.launch(galleryPermissions()) }
                    }
                    if (shown.isEmpty()) {
                        Box(Modifier.fillMaxSize().testTag("media.empty"), contentAlignment = Alignment.Center) {
                            Text(stringResource(R.string.gallery_empty), style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Text)))
                        }
                    } else {
                        LazyVerticalGrid(
                            columns = GridCells.Fixed(4),
                            modifier = Modifier.fillMaxSize().padding(top = 8.dp).nestedScroll(connection).testTag("media.grid"),
                        ) {
                            items(shown, key = { it.uri.toString() }) { media ->
                                val order = if (multi) selection.indexOf(media) else -1
                                GridCell(media, focused = media == focused, order = order,
                                    ownDuration = ownDuration.containsKey(media.uri), trimmed = trims.containsKey(media.uri),
                                    onTap = { tap(media) },
                                    onLongPress = { if (!multi) { multi = true; selection.clear(); selection.add(media); focused = media } })
                            }
                        }
                    }
                }
            }
            if (albumsOpen) {
                AlbumList(albums, bucketId, Modifier.padding(top = 44.dp)) { album ->
                    bucket = album.id to album.name
                    OmegaMediaPrefs.setBucket(context, album.id, album.name)
                    albumsOpen = false
                }
            }
            if (tip && !multi && !albumsOpen && items != null) {
                // P2-2: dica de primeiro uso, uma vez só.
                Text(
                    stringResource(R.string.om_media_change_album_tip),
                    modifier = Modifier
                        .align(Alignment.TopCenter)
                        .padding(top = 48.dp)
                        .clip(RoundedCornerShape(6.dp))
                        .background(AureaColors.LightSurface)
                        .tocavel { tip = false; OmegaMediaPrefs.setAlbumTipShown(context) }
                        .padding(horizontal = 12.dp, vertical = 8.dp),
                    style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.LightText)),
                )
            }
        }
        val current = focused
        if (info && current != null) {
            val lines = buildList {
                add(current.name)
                if (current.width > 0 && current.height > 0) add("${current.width} × ${current.height}")
                if (current.video) add(mediaDuration(current.durationMs))
                if (current.sizeBytes > 0) add(String.format(Locale.ROOT, "%.1f MB", current.sizeBytes / 1e6))
            }
            AureaDialog(
                title = stringResource(R.string.media_info_title),
                message = lines.joinToString("\n"),
                actions = listOf(DialogAction(stringResource(R.string.common_ok), DialogRole.Positive)),
                onDismiss = { info = false },
                tag = "media.info",
            )
        }
    }
}

/** Barra de título (44 dp): ✕, álbum ▾ centrado (gira 180° aberto), múltipla e +. */
@Composable
private fun TitleBar(album: String, open: Boolean, onClose: () -> Unit, onAlbum: () -> Unit, onMulti: () -> Unit, onAdd: () -> Unit, canAdd: Boolean) {
    val turn by animateFloatAsState(if (open) 180f else 0f, tween(150), label = "album arrow")
    Box(Modifier.fillMaxWidth().height(44.dp).background(AureaColors.AddPanel)) {
        if (!open) BarIcon(CupertinoGlyph.Xmark, stringResource(R.string.editor_fechar), "media.close", Modifier.align(Alignment.CenterStart), onClick = onClose)
        Row(
            Modifier.align(Alignment.Center).height(44.dp).testTag("media.album").semantics { contentDescription = album }
                .tocavel(shrink = 1f, onClick = onAlbum).padding(horizontal = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(album, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.padding(end = 6.dp),
                style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, color = AureaColors.Text)))
            Box(Modifier.size(14.dp, 13.dp).rotate(turn), contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.ChevronDown, 13.dp, if (open) AureaColors.Text else AureaColors.Muted)
            }
        }
        if (!open) {
            Row(Modifier.align(Alignment.CenterEnd)) {
                BarIcon(CupertinoGlyph.CheckmarkCircle, stringResource(R.string.om_media_select_multiple), "media.multi", onClick = onMulti)
                BarIcon(CupertinoGlyph.Plus, stringResource(R.string.om_media_add), "media.add", enabled = canAdd, onClick = onAdd)
            }
        }
    }
}

/** Barra da seleção múltipla (44 dp, fundo de destaque, conteúdo escuro). */
@Composable
private fun MultiBar(count: Int, onClose: () -> Unit, onSameStart: () -> Unit, onSequence: () -> Unit) {
    val ink = AureaColors.OnAccent
    Row(Modifier.fillMaxWidth().height(44.dp).background(AureaColors.Accent), verticalAlignment = Alignment.CenterVertically) {
        BarIcon(CupertinoGlyph.Xmark, stringResource(R.string.editor_fechar), "media.multi.close", tint = ink, onClick = onClose)
        Text(pluralStringResource(R.plurals.om_media_selected, count, count), modifier = Modifier.weight(1f), maxLines = 1,
            style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, color = ink)))
        BarIcon(CupertinoGlyph.SquareOnSquare, stringResource(R.string.om_media_add_same_start), "media.multi.sameStart",
            tint = ink, enabled = count > 0, onClick = onSameStart)
        BarIcon(CupertinoGlyph.ArrowRightToLine, stringResource(R.string.om_media_add_sequence), "media.multi.sequence",
            tint = ink, enabled = count > 0, onClick = onSequence)
    }
}

@Composable
private fun BarIcon(glyph: Char, description: String, tag: String, modifier: Modifier = Modifier, tint: androidx.compose.ui.graphics.Color = AureaColors.Text,
                    enabled: Boolean = true, onClick: () -> Unit) {
    Box(
        modifier.size(44.dp).testTag(tag).semantics { contentDescription = description }.tocavel(enabled = enabled, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { CupertinoIcon(glyph, 20.dp, if (enabled) tint else tint.copy(alpha = 0.35f)) }
}

/** A prévia (preta): foto ou vídeo, ▶ de 30 dp, chip do nome que mostra as informações. */
@Composable
private fun Preview(item: MediaItem, trim: Trim?, modifier: Modifier, onInfo: () -> Unit) {
    var chip by remember(item.uri) { mutableStateOf(true) }
    Box(modifier.background(AureaColors.Stage).clip(RoundedCornerShape(0.dp)).testTag("media.preview")) {
        // Bugs 0.3.4: um VideoView por mídia (antes o mesmo era reaproveitado e
        // ficava com o quadro do vídeo anterior até o novo preparar).
        if (item.video) key(item.uri) { VideoPreview(item, trim, Modifier.fillMaxSize()) { chip = !chip } }
        else {
            Box(Modifier.fillMaxSize().tocavel(shrink = 1f) { chip = !chip }) { PreviewImage(item, Modifier.fillMaxSize()) }
        }
        if (chip) {
            Row(
                Modifier
                    .align(Alignment.TopStart)
                    .padding(8.dp)
                    .height(30.dp)
                    .clip(RoundedCornerShape(15.dp))
                    .background(AureaColors.AddChipScrim)
                    .testTag("media.preview.info")
                    .tocavel(onClick = onInfo)
                    .padding(horizontal = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CupertinoIcon(CupertinoGlyph.InfoCircle, 14.dp, AureaColors.OnImage)
                Spacer(Modifier.width(6.dp))
                Text(item.name, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.width(180.dp),
                    style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.OnImage)))
            }
        }
    }
}

@Suppress("DEPRECATION")
@Composable
private fun PreviewImage(item: MediaItem, modifier: Modifier) {
    val context = LocalContext.current
    // Bugs 0.3.4 (prévia uma mídia atrás): o `produceState` deixava a foto
    // ANTERIOR na tela até a nova carregar. Agora o resultado é da mídia em
    // foco ou nada; o pedido de uma mídia que perdeu o foco é cancelado.
    val bitmap = rememberLoaded(item.uri) {
        if (Build.VERSION.SDK_INT >= 29) context.contentResolver.loadThumbnail(item.uri, android.util.Size(1280, 1280), null)
        else if (item.video) android.provider.MediaStore.Video.Thumbnails.getThumbnail(context.contentResolver,
            android.content.ContentUris.parseId(item.uri), android.provider.MediaStore.Video.Thumbnails.MINI_KIND, null)
        else android.provider.MediaStore.Images.Media.getBitmap(context.contentResolver, item.uri)
    }
    Box(modifier, contentAlignment = Alignment.Center) {
        bitmap?.let { Image(it.asImageBitmap(), item.name, Modifier.fillMaxSize(), contentScale = ContentScale.Fit) }
    }
}

/** Vídeo da plataforma (VideoView): toca só o trecho cortado e volta ao início no fim. */
@Composable
private fun VideoPreview(item: MediaItem, trim: Trim?, modifier: Modifier, onTapImage: () -> Unit) {
    var view by remember { mutableStateOf<VideoView?>(null) }
    var playing by remember(item.uri) { mutableStateOf(false) }
    var prepared by remember(item.uri) { mutableStateOf(false) }
    val startMs = ((trim?.inUs ?: 0L) / 1000).toInt()
    val endMs = trim?.let { (it.outUs / 1000).toInt() } ?: Int.MAX_VALUE
    val latestStartMs by androidx.compose.runtime.rememberUpdatedState(startMs)
    LaunchedEffect(playing, startMs, endMs) {
        val v = view ?: return@LaunchedEffect
        if (!playing) { runCatching { v.pause() }; return@LaunchedEffect }
        runCatching { if (v.currentPosition < startMs || v.currentPosition >= endMs) v.seekTo(startMs); v.start() }
        while (playing) {
            delay(40)
            val position = runCatching { v.currentPosition }.getOrDefault(0)
            if (position >= endMs || runCatching { !v.isPlaying }.getOrDefault(true) && position > startMs + 100) {
                runCatching { v.pause(); v.seekTo(startMs) }
                playing = false
            }
        }
    }
    LaunchedEffect(item.uri, startMs) { if (!playing) runCatching { view?.seekTo(startMs) } }
    DisposableEffect(Unit) { onDispose { runCatching { view?.stopPlayback() } } }
    val playLabel = stringResource(if (playing) R.string.editor_pausar else R.string.editor_reproduzir)
    Box(modifier, contentAlignment = Alignment.Center) {
        AndroidView(
            factory = { ctx ->
                VideoView(ctx).also { v ->
                    v.setOnPreparedListener { mp -> mp.setVolume(1f, 1f); v.seekTo(latestStartMs); prepared = true }
                    view = v
                }
            },
            update = { v -> if (v.tag != item.uri) { v.tag = item.uri; playing = false; prepared = false; v.setVideoURI(item.uri) } },
            modifier = Modifier.fillMaxSize(),
        )
        // Até o vídeo DESTA mídia preparar, a miniatura dela (nunca a anterior).
        if (!prepared) PreviewImage(item, Modifier.fillMaxSize())
        Box(Modifier.fillMaxSize().tocavel(shrink = 1f, onClick = onTapImage))
        if (!playing) {
            Box(
                Modifier.size(48.dp).testTag("media.preview.play").semantics { contentDescription = playLabel }.tocavel { playing = true },
                contentAlignment = Alignment.Center,
            ) { CupertinoIcon(CupertinoGlyph.PlayFill, 30.dp, AureaColors.OnImage) }
        } else {
            Box(Modifier.fillMaxSize().semantics { contentDescription = playLabel }.tocavel(shrink = 1f) { playing = false })
        }
    }
}

/**
 * A régua da duração da imagem: agulha de destaque de 1,5 dp no meio, ticks
 * de 0,5 dp a cada 5,5 dp (um quadro cada; o maior a cada segundo), o valor
 * `mm:ss:ff` à direita. Arrastar de lado muda a duração (1 quadro a fps×10+50).
 * Na múltipla, o botão de vínculo (55 dp) à esquerda dá a duração própria.
 */
@Composable
private fun DurationRuler(frames: Long, maxFrames: Long, fps: Double, linkVisible: Boolean, own: Boolean, onLink: () -> Unit, onChange: (Long) -> Unit) {
    val density = LocalDensity.current
    val stepPx = with(density) { 5.5.dp.toPx() }
    val accent = AureaColors.Accent
    val tick = AureaColors.Text.copy(alpha = 0.55f)
    val label = stringResource(R.string.om_media_image_duration)
    val current by androidx.compose.runtime.rememberUpdatedState(frames)
    val change by androidx.compose.runtime.rememberUpdatedState(onChange)
    var drag by remember { mutableFloatStateOf(0f) }
    Row(Modifier.fillMaxSize(), verticalAlignment = Alignment.CenterVertically) {
        if (linkVisible) {
            val linkLabel = stringResource(if (own) R.string.om_media_duration_own else R.string.om_media_duration_shared)
            Box(
                Modifier.size(55.dp).testTag("media.duration.link").semantics { contentDescription = linkLabel; selected = own }.tocavel(onClick = onLink),
                contentAlignment = Alignment.Center,
            ) { CupertinoIcon(CupertinoGlyph.Link, 20.dp, if (own) AureaColors.Accent else AureaColors.Text) }
        }
        Canvas(
            Modifier
                .weight(1f)
                .fillMaxHeight()
                .testTag("media.duration")
                .semantics { contentDescription = label; stateDescription = rulerTime(frames, fps) }
                .pointerInput(maxFrames, stepPx) {
                    detectHorizontalDragGestures(onDragStart = { drag = 0f }) { pointer, amount ->
                        pointer.consume()
                        drag -= amount
                        val steps = (drag / stepPx).toLong()
                        if (steps != 0L) {
                            drag -= steps * stepPx
                            change((current + steps).coerceIn(1L, maxFrames))
                        }
                    }
                },
        ) {
            val mid = size.width / 2f
            val fpsInt = max(1, fps.roundToInt())
            val first = frames - (mid / stepPx).toLong() - 1
            val last = frames + (mid / stepPx).toLong() + 1
            for (f in first..last) {
                if (f < 1 || f > maxFrames) continue
                val x = mid + (f - frames) * stepPx
                val major = f % fpsInt == 0L
                val h = if (major) size.height * 0.42f else size.height * 0.24f
                drawLine(tick, Offset(x, (size.height - h) / 2f), Offset(x, (size.height + h) / 2f), with(density) { 0.5.dp.toPx() })
            }
            drawLine(accent, Offset(mid, size.height * 0.18f), Offset(mid, size.height * 0.82f), with(density) { 1.5.dp.toPx() })
        }
        Text(rulerTime(frames, fps), modifier = Modifier.padding(horizontal = 12.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 16.sp, fontWeight = FontWeight.W700, color = AureaColors.Text, fontFeatureSettings = "tnum")))
    }
}

/** `mm:ss:ff` na taxa da composição. */
private fun rulerTime(frames: Long, fps: Double): String {
    val f = max(1, fps.roundToInt())
    val totalSeconds = frames / f
    return String.format(Locale.ROOT, "%02d:%02d:%02d", totalSeconds / 60, totalSeconds % 60, frames % f)
}

/**
 * O corte do vídeo: a tira de miniaturas (margens 25/12) e por cima as alças
 * de início e fim; fora do trecho fica escurecido. O ▶ da prévia toca só o
 * trecho.
 */
@Composable
private fun TrimBar(item: MediaItem, trim: Trim?, onTrim: (Trim) -> Unit) {
    val context = LocalContext.current
    val durationUs = item.durationMs * 1000
    // Bugs 0.3.4: a tira é do vídeo em foco (antes ficava a do anterior até a nova carregar).
    val frames = rememberLoaded(item.uri) { videoFrames(context, item.uri, durationUs, 8) }.orEmpty()
    val label = stringResource(R.string.om_media_trim)
    val startLabel = stringResource(R.string.om_media_trim_start)
    val endLabel = stringResource(R.string.om_media_trim_end)
    val accent = AureaColors.Accent
    val veil = AureaColors.ExportScrim
    val current = trim ?: Trim(0, durationUs)
    val latest by androidx.compose.runtime.rememberUpdatedState(current)
    val trimTo by androidx.compose.runtime.rememberUpdatedState(onTrim)
    BoxWithConstraints(Modifier.fillMaxSize().padding(start = 25.dp, end = 12.dp, top = 6.dp, bottom = 6.dp).testTag("media.trim")
        .semantics { contentDescription = label }) {
        val widthPx = constraints.maxWidth.toFloat().coerceAtLeast(1f)
        Row(Modifier.fillMaxSize().clip(RoundedCornerShape(3.dp)).background(AureaColors.AddPanelDeep)) {
            frames.forEach { bmp -> Image(bmp.asImageBitmap(), null, Modifier.weight(1f).fillMaxHeight(), contentScale = ContentScale.Crop) }
        }
        if (durationUs > 0) {
            val density = LocalDensity.current
            val handle = with(density) { 12.dp.toPx() }
            val minGapUs = 100_000L
            fun xOf(us: Long) = (us.toFloat() / durationUs) * widthPx
            Canvas(Modifier.fillMaxSize()) {
                val a = xOf(current.inUs)
                val b = xOf(current.outUs)
                drawRect(veil, Offset.Zero, Size(a, size.height))
                drawRect(veil, Offset(b, 0f), Size(size.width - b, size.height))
                drawRect(accent, Offset(a, 0f), Size(b - a, size.height), style = androidx.compose.ui.graphics.drawscope.Stroke(with(density) { 2.dp.toPx() }))
            }
            // Alças (alvo de 44 dp de largura), arrastáveis.
            listOf(true, false).forEach { start ->
                val x = xOf(if (start) current.inUs else current.outUs)
                Box(
                    Modifier
                        .offset { IntOffset((x - with(density) { 22.dp.toPx() }).roundToInt(), 0) }
                        .width(44.dp)
                        .fillMaxHeight()
                        .testTag(if (start) "media.trim.start" else "media.trim.end")
                        .semantics { contentDescription = if (start) startLabel else endLabel }
                        .pointerInput(durationUs, widthPx) {
                            detectHorizontalDragGestures { change, amount ->
                                change.consume()
                                val deltaUs = (amount / widthPx * durationUs).toLong()
                                val t = latest
                                trimTo(if (start) Trim((t.inUs + deltaUs).coerceIn(0L, t.outUs - minGapUs), t.outUs)
                                    else Trim(t.inUs, (t.outUs + deltaUs).coerceIn(t.inUs + minGapUs, durationUs)))
                            }
                        },
                    contentAlignment = Alignment.Center,
                ) {
                    Box(Modifier.width(with(density) { handle.toDp() }).fillMaxHeight().clip(RoundedCornerShape(2.dp)).background(accent))
                }
            }
        }
    }
}

/** `count` quadros espalhados pelo vídeo, pequenos (tira do corte). */
private fun videoFrames(context: Context, uri: Uri, durationUs: Long, count: Int): List<Bitmap> {
    val retriever = MediaMetadataRetriever()
    return try {
        retriever.setDataSource(context, uri)
        val total = if (durationUs > 0) durationUs
            else (retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull() ?: 0L) * 1000
        (0 until count).mapNotNull { i ->
            val t = if (total > 0) total * i / count else 0L
            runCatching {
                if (Build.VERSION.SDK_INT >= 27) retriever.getScaledFrameAtTime(t, MediaMetadataRetriever.OPTION_CLOSEST_SYNC, 96, 96)
                else retriever.getFrameAtTime(t, MediaMetadataRetriever.OPTION_CLOSEST_SYNC)?.let { Bitmap.createScaledBitmap(it, 96, 96 * it.height / max(1, it.width), true) }
            }.getOrNull()
        }
    } catch (_: Exception) {
        emptyList()
    } finally {
        runCatching { retriever.release() }
    }
}

/** Item da grade: quadrado, foco em moldura, número de ordem, ícones de duração própria e corte. */
@Composable
private fun GridCell(item: MediaItem, focused: Boolean, order: Int, ownDuration: Boolean, trimmed: Boolean, onTap: () -> Unit, onLongPress: () -> Unit) {
    val label = if (item.video) stringResource(R.string.om_media_video_named, item.name, mediaDuration(item.durationMs))
        else stringResource(R.string.om_media_photo_named, item.name)
    val ownLabel = stringResource(R.string.om_media_duration_own)
    val trimLabel = stringResource(R.string.om_media_trimmed)
    val accent = AureaColors.Accent
    Box(
        Modifier
            .aspectRatio(1f)
            .padding(0.5.dp)
            .semantics {
                contentDescription = listOfNotNull(label, ownLabel.takeIf { ownDuration }, trimLabel.takeIf { trimmed }).joinToString(", ")
                selected = order >= 0 || focused
            }
            .testTag("media.item.${item.name}")
            .tocavel(shrink = 0.97f, onLongClick = onLongPress, onClick = onTap),
    ) {
        MediaThumb(item.uri, item.video, Modifier.fillMaxSize())
        if (item.video) Text(mediaDuration(item.durationMs), Modifier.align(Alignment.BottomEnd).padding(4.dp), style = OnThumbStyle)
        Row(Modifier.align(Alignment.BottomStart).padding(start = 4.dp, bottom = 5.dp)) {
            if (ownDuration) CupertinoIcon(CupertinoGlyph.Link, 12.dp, AureaColors.OnImage)
            if (trimmed) CupertinoIcon(CupertinoGlyph.Scissors, 12.dp, AureaColors.OnImage)
        }
        if (focused) Box(Modifier.fillMaxSize().border(2.dp, accent))
        if (order >= 0) {
            Box(
                Modifier.align(Alignment.TopEnd).padding(4.dp).size(20.dp).clip(CircleShape).background(accent),
                contentAlignment = Alignment.Center,
            ) {
                Text("${order + 1}", style = AureaType.Base.merge(TextStyle(fontSize = 10.sp, fontWeight = FontWeight.W700, color = AureaColors.OnAccent)))
            }
        }
    }
}

/** Aviso de acesso parcial: "Você deu acesso a algumas fotos e vídeos" + Gerenciar. */
@Composable
private fun PartialNotice(onManage: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .padding(start = 8.dp, end = 8.dp, top = 8.dp)
            .clip(RoundedCornerShape(6.dp))
            .background(AureaColors.AddControlBar)
            .padding(horizontal = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(stringResource(R.string.om_media_partial), modifier = Modifier.weight(1f).padding(vertical = 10.dp),
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Text)))
        Box(Modifier.height(48.dp).testTag("media.manage").tocavel(onClick = onManage).padding(horizontal = 8.dp), contentAlignment = Alignment.Center) {
            Text(stringResource(R.string.om_media_manage), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)))
        }
    }
}

/** A lista de álbuns (linhas de 70 dp com miniatura, nome, contagem e ▶ nos de vídeo). */
@Composable
private fun AlbumList(albums: List<MediaAlbum>, current: Long, modifier: Modifier, onPick: (MediaAlbum) -> Unit) {
    LazyColumn(modifier.fillMaxWidth().background(AureaColors.AddPanel).testTag("media.albums")) {
        itemsIndexed(albums, key = { _, a -> a.id }) { index, album ->
            Row(
                Modifier
                    .fillMaxWidth()
                    .height(70.dp)
                    .background(if (album.id == current) AureaColors.AddTile else AureaColors.AddPanel)
                    .semantics { contentDescription = "${album.name}, ${album.count}"; selected = album.id == current }
                    .testTag("media.album.$index")
                    .tocavel(shrink = 1f) { onPick(album) },
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(Modifier.padding(7.dp).size(56.dp).background(AureaColors.AddPanelDeep)) {
                    album.cover?.let { MediaThumb(it.uri, it.video, Modifier.fillMaxSize()) }
                    if (album.video) CupertinoIcon(CupertinoGlyph.PlayFill, 14.dp, AureaColors.OnImage, Modifier.align(Alignment.Center))
                }
                Column(Modifier.weight(1f).padding(start = 6.dp)) {
                    Text(album.name, maxLines = 1, overflow = TextOverflow.Ellipsis, style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text)))
                    Text(album.count.toString(), style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)))
                }
            }
            Box(Modifier.fillMaxWidth().height(0.5.dp).background(AureaColors.AddPanelDeep))
        }
    }
}
