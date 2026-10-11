package com.aurea.aurea.editor

import android.Manifest
import android.content.ContentUris
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.database.ContentObserver
import android.graphics.Bitmap
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.provider.MediaStore
import android.provider.Settings
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

internal data class GalleryItem(val uri: Uri, val name: String, val durationMs: Long)

internal fun galleryItems(context: Context, video: Boolean): List<GalleryItem> {
    val collection = if (video) MediaStore.Video.Media.EXTERNAL_CONTENT_URI else MediaStore.Images.Media.EXTERNAL_CONTENT_URI
    val columns = mutableListOf(MediaStore.MediaColumns._ID, MediaStore.MediaColumns.DISPLAY_NAME)
    if (video) columns.add(MediaStore.Video.Media.DURATION)
    return buildList {
        context.contentResolver.query(collection, columns.toTypedArray(), null, null,
            "${MediaStore.MediaColumns.DATE_ADDED} DESC, ${MediaStore.MediaColumns._ID} DESC")?.use { cursor ->
            while (cursor.moveToNext()) add(GalleryItem(ContentUris.withAppendedId(collection, cursor.getLong(0)),
                cursor.getString(1).orEmpty(), if (video) cursor.getLong(2) else 0))
        }
    }
}

private fun Context.galleryGranted(permission: String) = ContextCompat.checkSelfPermission(this, permission) == PackageManager.PERMISSION_GRANTED
private fun Context.galleryFull(video: Boolean) = galleryGranted(if (Build.VERSION.SDK_INT >= 33)
    if (video) Manifest.permission.READ_MEDIA_VIDEO else Manifest.permission.READ_MEDIA_IMAGES
    else Manifest.permission.READ_EXTERNAL_STORAGE)
private fun Context.galleryPartial() = Build.VERSION.SDK_INT >= 34 && galleryGranted(Manifest.permission.READ_MEDIA_VISUAL_USER_SELECTED)
private fun galleryPermissions(): Array<String> = when {
    Build.VERSION.SDK_INT >= 34 -> arrayOf(Manifest.permission.READ_MEDIA_IMAGES, Manifest.permission.READ_MEDIA_VIDEO, Manifest.permission.READ_MEDIA_VISUAL_USER_SELECTED)
    Build.VERSION.SDK_INT >= 33 -> arrayOf(Manifest.permission.READ_MEDIA_IMAGES, Manifest.permission.READ_MEDIA_VIDEO)
    else -> arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE)
}

/** The editor owns the grid; the system picker remains available for cloud providers. */
@Composable internal fun MediaGallery(store: EditorStore, close: () -> Unit, openAI: () -> Unit) {
    val context = LocalContext.current
    val owner = LocalLifecycleOwner.current
    var video by rememberSaveable { mutableStateOf(false) }
    var revision by remember { mutableIntStateOf(0) }
    var requested by rememberSaveable { mutableStateOf(false) }
    var items by remember { mutableStateOf<List<GalleryItem>>(emptyList()) }
    var loading by remember { mutableStateOf(true) }
    var failed by remember { mutableStateOf(false) }
    var selecting by rememberSaveable { mutableStateOf(false) }
    val selectedItems = remember { mutableStateListOf<Uri>() }
    val full = remember(revision, video) { context.galleryFull(video) }
    val partial = remember(revision) { context.galleryPartial() }
    val permissions = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { revision++ }
    fun importItem(uri: Uri) {
        if (context.contentResolver.getType(uri)?.startsWith("video/") == true) store.importVideo(uri) else store.importImage(uri)
        close()
    }
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.PickMultipleVisualMedia()) { uris ->
        if (uris.isNotEmpty()) { store.importMediaBatch(uris); close() }
    }
    LaunchedEffect(Unit) {
        if (!requested && !context.galleryFull(false) && !context.galleryFull(true) && !context.galleryPartial()) {
            requested = true; permissions.launch(galleryPermissions())
        }
    }
    DisposableEffect(owner, context) {
        val observer = object : ContentObserver(Handler(Looper.getMainLooper())) { override fun onChange(selfChange: Boolean) { revision++ } }
        context.contentResolver.registerContentObserver(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, true, observer)
        context.contentResolver.registerContentObserver(MediaStore.Video.Media.EXTERNAL_CONTENT_URI, true, observer)
        val lifecycle = LifecycleEventObserver { _, event -> if (event == Lifecycle.Event.ON_RESUME) revision++ }
        owner.lifecycle.addObserver(lifecycle)
        onDispose { context.contentResolver.unregisterContentObserver(observer); owner.lifecycle.removeObserver(lifecycle) }
    }
    LaunchedEffect(video, revision) {
        loading = true; failed = false; items = emptyList()
        if (full || partial) {
            val result = withContext(Dispatchers.IO) { runCatching { galleryItems(context, video) } }
            items = result.getOrDefault(emptyList()); failed = result.isFailure
        }
        loading = false
    }
    Column(Modifier.fillMaxSize().testTag("gallery.panel")) {
        Row(Modifier.fillMaxWidth().heightIn(min = 48.dp), verticalAlignment = Alignment.CenterVertically) {
            for (isVideo in listOf(false, true)) {
                TextButton(onClick = { video = isVideo }, modifier = Modifier.weight(1f).heightIn(min = 48.dp)
                    .testTag(if (isVideo) "gallery.videos" else "gallery.photos")) {
                    Text(stringResource(if (isVideo) R.string.editor_video else R.string.editor_foto),
                        color = if (isVideo == video) AureaColors.Accent else AureaColors.Muted)
                }
            }
            ChromeButton(CupertinoGlyph.Folder, stringResource(R.string.gallery_files), size = 20.dp, width = 48.dp, height = 48.dp,
                onClick = { picker.launch(PickVisualMediaRequest(if (video) ActivityResultContracts.PickVisualMedia.VideoOnly else ActivityResultContracts.PickVisualMedia.ImageOnly)) })
            ChromeButton(CupertinoGlyph.WandStars, stringResource(R.string.sh_add_ai_video), size = 20.dp, width = 48.dp, height = 48.dp, onClick = openAI)
        }
        if (partial && !full) {
            Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.gallery_limited), color = AureaColors.Muted, fontSize = 11.sp, modifier = Modifier.weight(1f))
                TextButton(onClick = { permissions.launch(galleryPermissions()) }) { Text(stringResource(R.string.gallery_manage)) }
            }
        }
        Text(stringResource(R.string.gallery_recent), color = AureaColors.Muted, fontSize = 11.sp, modifier = Modifier.padding(start = 12.dp, bottom = 6.dp))
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = { selecting = !selecting; if (!selecting) selectedItems.clear() },
                modifier = Modifier.heightIn(min = 48.dp).testTag("gallery.select")) {
                Text(stringResource(if (selecting) R.string.editor_cancelar else R.string.beta_select_media))
            }
            if (selecting) {
                Spacer(Modifier.weight(1f))
                TextButton(onClick = { store.importMediaBatch(selectedItems.toList()); close() }, enabled = selectedItems.isNotEmpty(),
                    modifier = Modifier.heightIn(min = 48.dp).testTag("gallery.addSelected")) {
                    Text(stringResource(R.string.beta_add_media, selectedItems.size))
                }
            }
        }
        Box(Modifier.weight(1f).fillMaxWidth(), contentAlignment = Alignment.Center) {
            when {
                loading -> CircularProgressIndicator(Modifier.size(28.dp), color = AureaColors.Accent)
                !full && !partial -> Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text(stringResource(R.string.gallery_access), color = AureaColors.Muted, modifier = Modifier.padding(horizontal = 16.dp))
                    TextButton(onClick = {
                        context.startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:${context.packageName}")))
                    }) { Text(stringResource(R.string.gallery_allow)) }
                }
                failed -> TextButton(onClick = { revision++ }) { Text(stringResource(R.string.gallery_error)) }
                items.isEmpty() -> Text(stringResource(R.string.gallery_empty), color = AureaColors.Muted)
                else -> LazyVerticalGrid(columns = GridCells.Adaptive(88.dp), modifier = Modifier.fillMaxSize().testTag("gallery.grid"),
                    horizontalArrangement = Arrangement.spacedBy(3.dp), verticalArrangement = Arrangement.spacedBy(3.dp), contentPadding = PaddingValues(3.dp)) {
                    items(items, key = { it.uri.toString() }) { item ->
                        Box(Modifier.semantics { selected = item.uri in selectedItems }) {
                            GalleryThumbnail(item, video) {
                                if (selecting) {
                                    if (!selectedItems.remove(item.uri)) selectedItems.add(item.uri)
                                } else importItem(item.uri)
                            }
                            if (item.uri in selectedItems) Text("✓ ${selectedItems.indexOf(item.uri) + 1}",
                                color = AureaColors.Accent, modifier = Modifier.align(Alignment.TopEnd)
                                    .background(AureaColors.Chip).padding(6.dp))
                        }
                    }
                }
            }
        }
    }
}

@Suppress("DEPRECATION")
@Composable private fun GalleryThumbnail(item: GalleryItem, video: Boolean, pick: () -> Unit) {
    val context = LocalContext.current
    val bitmap by produceState<Bitmap?>(null, item.uri) {
        value = withContext(Dispatchers.IO) {
            runCatching {
                if (Build.VERSION.SDK_INT >= 29) context.contentResolver.loadThumbnail(item.uri, android.util.Size(256, 256), null)
                else if (video) MediaStore.Video.Thumbnails.getThumbnail(context.contentResolver, ContentUris.parseId(item.uri), MediaStore.Video.Thumbnails.MINI_KIND, null)
                else MediaStore.Images.Thumbnails.getThumbnail(context.contentResolver, ContentUris.parseId(item.uri), MediaStore.Images.Thumbnails.MINI_KIND, null)
            }.getOrNull()
        }
    }
    Box(Modifier.fillMaxWidth().aspectRatio(1f).background(AureaColors.Chip).tocavel(onClick = pick)
        .testTag("gallery.item.${item.name}"), contentAlignment = Alignment.Center) {
        if (bitmap != null) Image(bitmap!!.asImageBitmap(), item.name, Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
        else Text(item.name, color = AureaColors.Muted, fontSize = 10.sp, maxLines = 2, modifier = Modifier.padding(4.dp))
        if (video) {
            val seconds = item.durationMs / 1000
            Text("${seconds / 60}:${(seconds % 60).toString().padStart(2, '0')}", color = androidx.compose.ui.graphics.Color.White, fontSize = 11.sp,
                modifier = Modifier.align(Alignment.BottomEnd).background(androidx.compose.ui.graphics.Color.Black.copy(alpha = .65f)).padding(4.dp))
        }
    }
}
