package com.aurea.aurea.community

import android.content.ContentValues
import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.ImageDecoder
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import android.widget.MediaController
import android.widget.VideoView
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
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
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import com.aurea.aurea.R
import com.aurea.aurea.engine.ImageMemoryPolicy
import com.aurea.aurea.state.GalleryPublish
import com.aurea.aurea.ui.theme.*
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import kotlinx.coroutines.withContext
import java.io.File

internal const val CommunityImageLimit = 8L * 1024 * 1024
internal const val CommunityVideoLimit = 90L * 1024 * 1024
private val posterDecoders = Semaphore(1)

internal fun mediaFileName(asset: CommunityAsset, mime: String): String {
    val ext = when (mime) {
        "image/png" -> "png"
        "image/jpeg" -> "jpg"
        "video/quicktime" -> "mov"
        "video/mp4" -> "mp4"
        else -> throw CommunityFailure("invalid_file")
    }
    val stem = asset.name.substringBeforeLast('.').replace(Regex("[^\\p{L}\\p{N} _.-]"), "_")
        .trim(' ', '.').take(64).ifBlank { "Aurea" }
    return "$stem.$ext"
}

@androidx.annotation.RequiresApi(Build.VERSION_CODES.Q)
internal fun saveCommunityMediaToGallery(context: Context, file: File, name: String, mime: String) {
    val images = mime.startsWith("image/")
    val collection = if (images) MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
        else MediaStore.Video.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
    val directory = if (images) Environment.DIRECTORY_PICTURES else Environment.DIRECTORY_MOVIES
    fun values(columns: List<Pair<String, Any>>) = ContentValues().apply {
        columns.forEach { (key, value) ->
            when (value) {
                is String -> put(key, value)
                is Long -> put(key, value)
                is Int -> put(key, value)
            }
        }
    }
    val resolver = context.contentResolver
    val now = System.currentTimeMillis()
    val target = resolver.insert(collection, values(GalleryPublish.pendingColumns(name, mime, "$directory/Aurea", now)))
        ?: throw CommunityFailure("invalid_file")
    try {
        val count = resolver.openOutputStream(target, "w")?.use { GalleryPublish.copyFile(file, it) }
        if (count != file.length() || resolver.update(target, values(GalleryPublish.publishColumns(now)), null, null) != 1)
            throw CommunityFailure("invalid_file")
    } catch (e: Exception) {
        runCatching { resolver.delete(target, null, null) }
        throw e
    }
}

internal fun prepareCommunityMedia(context: Context, uri: Uri): Pair<File, String> {
    val resolver = context.contentResolver
    val type = resolver.getType(uri).orEmpty()
    val video = type.startsWith("video/")
    val ext = when {
        type == "video/quicktime" -> ".mov"
        video -> ".mp4"
        else -> ".jpg"
    }
    val file = File.createTempFile("aurea-media-", ext, context.cacheDir)
    try {
        if (video) {
            val input = resolver.openInputStream(uri) ?: throw CommunityFailure("invalid_file")
            input.use {
                file.outputStream().use { out ->
                    val buffer = ByteArray(64 * 1024)
                    var total = 0L
                    while (true) {
                        val n = it.read(buffer)
                        if (n < 0) break
                        total += n
                        if (total > CommunityVideoLimit) throw CommunityFailure("file_too_large")
                        out.write(buffer, 0, n)
                    }
                }
            }
        } else {
            val size = 1920
            val bitmap = if (Build.VERSION.SDK_INT >= 28) {
                ImageDecoder.decodeBitmap(ImageDecoder.createSource(resolver, uri)) { decoder, info, _ ->
                    val ratio = (size.toFloat() / maxOf(info.size.width, info.size.height)).coerceAtMost(1f)
                    decoder.setTargetSize((info.size.width * ratio).toInt().coerceAtLeast(1), (info.size.height * ratio).toInt().coerceAtLeast(1))
                    decoder.allocator = ImageDecoder.ALLOCATOR_SOFTWARE
                }
            } else {
                val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
                resolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, bounds) }
                if (bounds.outWidth <= 0 || bounds.outHeight <= 0) throw CommunityFailure("invalid_image")
                val sample = ImageMemoryPolicy.sampleSize(bounds.outWidth, bounds.outHeight, size, size.toLong() * size)
                    ?: throw CommunityFailure("invalid_image")
                val options = BitmapFactory.Options().apply { inSampleSize = sample }
                resolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, options) }
                    ?: throw CommunityFailure("invalid_image")
            }
            try {
                if (!file.outputStream().use { bitmap.compress(Bitmap.CompressFormat.JPEG, 87, it) })
                    throw CommunityFailure("invalid_image")
            } finally { bitmap.recycle() }
        }
        if (file.length() == 0L) throw CommunityFailure("invalid_file")
        return file to if (video) "video" else "image"
    } catch (failure: Throwable) {
        file.delete()
        throw failure
    }
}

@Composable
internal fun CommunityPostMedia(asset: CommunityAsset, api: CommunityApi, detail: Boolean, onOpen: () -> Unit = {}) {
    val label = stringResource(if (asset.kind == "video") R.string.social_play_video else R.string.social_open_post)
    val shape = RoundedCornerShape(14.dp)
    if (asset.kind == "video" && detail) {
        val url = api.mediaUrl(asset) ?: return
        var failed by remember(url) { mutableStateOf(false) }
        if (failed) {
            Text(stringResource(R.string.social_media_error), color = AureaColors.LightTextMuted)
        } else key(url) {
            AndroidView(factory = { context ->
                VideoView(context).apply {
                    setMediaController(MediaController(context).also { it.setAnchorView(this) })
                    setOnErrorListener { _, _, _ -> failed = true; true }
                    setVideoURI(Uri.parse(url))
                    start()
                }
            }, modifier = Modifier.fillMaxWidth().height(220.dp).clip(shape).background(AureaColors.LightSurface)
                .testTag("social.media.player.${asset.id}"), onReset = null,
                onRelease = { it.stopPlayback() })
        }
        return
    }
    val bitmap by produceState<Bitmap?>(null, api, asset, detail) {
        value = null
        try {
            value = withContext(Dispatchers.IO) {
                if (asset.kind == "video") posterDecoders.withPermit { api.videoPoster(asset, 720) }
                else api.postImage(asset, if (detail) 1440 else 840)
            }
        } catch (e: CancellationException) { throw e }
        catch (_: Exception) { }
    }
    val aspect = if (!detail) 1f else bitmap?.let { (it.width.toFloat() / it.height).coerceIn(0.8f, 1.8f) } ?: 1.78f
    Box(Modifier.fillMaxWidth().aspectRatio(aspect).then(if (detail) Modifier.clip(shape) else Modifier)
        .background(AureaColors.LightAccentInk.copy(alpha = 0.10f))
        .then(if (!detail) Modifier.testTag("social.media.open.${asset.id}").clickable(role = Role.Button, onClick = onOpen)
            .semantics { contentDescription = label } else Modifier),
        contentAlignment = Alignment.Center) {
        val preview = bitmap
        if (preview != null) {
            Image(preview.asImageBitmap(), if (detail) asset.name else null,
                Modifier.fillMaxSize().testTag("social.media.preview.${asset.id}"),
                contentScale = if (detail) ContentScale.Fit else ContentScale.Crop)
        } else if (asset.kind == "image") {
            CupertinoIcon(CupertinoGlyph.Photo, 24.dp, AureaColors.LightTextMuted)
        }
        if (asset.kind == "video") {
            Box(Modifier.size(52.dp).background(Color.Black.copy(alpha = 0.62f), CircleShape), contentAlignment = Alignment.Center) {
                CupertinoIcon(CupertinoGlyph.PlayFill, 20.dp, Color.White)
            }
        }
    }
}
