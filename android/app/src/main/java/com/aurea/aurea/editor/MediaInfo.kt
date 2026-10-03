package com.aurea.aurea.editor

import com.aurea.aurea.ui.i18n.ltrPlain
import android.content.Context
import android.graphics.BitmapFactory
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import android.provider.OpenableColumns
import android.text.format.Formatter
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.util.Locale

/**
 * "Informações da mídia": o que o arquivo de origem da camada é — nome,
 * tamanho, resolução, fps, duração, codecs, taxa de amostragem e HDR. Lido do
 * próprio arquivo (MediaExtractor / cabeçalho da imagem), não do projeto.
 */
internal data class MediaInfo(
    val fileName: String,
    val sizeBytes: Long,
    val width: Int = 0,
    val height: Int = 0,
    val fps: Float = 0f,
    val durationUs: Long = 0,
    val videoCodec: String = "",
    val audioCodec: String = "",
    val sampleRate: Int = 0,
    val channels: Int = 0,
    val hdr: Boolean? = null,
)

internal object MediaInfoProbe {
    /** Nome legível do codec pelo MIME do Android ("video/hevc" → "HEVC (H.265)"). */
    fun codecName(mime: String): String = when (mime.lowercase(Locale.ROOT)) {
        "video/avc" -> "H.264 (AVC)"
        "video/hevc" -> "HEVC (H.265)"
        "video/av01" -> "AV1"
        "video/x-vnd.on2.vp9" -> "VP9"
        "video/x-vnd.on2.vp8" -> "VP8"
        "video/dolby-vision" -> "Dolby Vision"
        "video/mp4v-es" -> "MPEG-4"
        "video/3gpp" -> "H.263"
        "audio/mp4a-latm" -> "AAC"
        "audio/opus" -> "Opus"
        "audio/vorbis" -> "Vorbis"
        "audio/mpeg" -> "MP3"
        "audio/flac" -> "FLAC"
        "audio/raw" -> "PCM"
        "audio/ac3" -> "AC-3"
        "audio/eac3" -> "E-AC-3"
        "audio/3gpp" -> "AMR-NB"
        "audio/amr-wb" -> "AMR-WB"
        "image/jpeg" -> "JPEG"
        "image/png" -> "PNG"
        "image/webp" -> "WebP"
        "image/heif", "image/heic" -> "HEIF"
        "image/gif" -> "GIF"
        "image/avif" -> "AVIF"
        else -> mime.substringAfter('/').uppercase(Locale.ROOT)
    }

    /** Lê o arquivo (URI `content://`, `file://` ou caminho). null = ilegível. Roda em IO. */
    fun read(context: Context, source: String): MediaInfo? = try {
        val uri = if (source.startsWith("/")) Uri.fromFile(File(source)) else Uri.parse(source)
        var name = File(uri.path ?: source).name
        var size = if (uri.scheme == "file") File(uri.path ?: "").length() else -1L
        if (uri.scheme == "content") {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)?.use { c ->
                if (c.moveToFirst()) {
                    c.getString(0)?.let { name = it }
                    if (!c.isNull(1)) size = c.getLong(1)
                }
            }
        }
        val mime = runCatching { context.contentResolver.getType(uri) }.getOrNull().orEmpty()
        if (mime.startsWith("image/") || isImageName(name)) readImage(context, uri, name, size) else readAv(context, uri, name, size)
    } catch (_: Exception) {
        null
    }

    private fun isImageName(name: String): Boolean =
        name.substringAfterLast('.', "").lowercase(Locale.ROOT) in setOf("jpg", "jpeg", "png", "webp", "heic", "heif", "gif", "bmp", "avif")

    private fun readImage(context: Context, uri: Uri, name: String, size: Long): MediaInfo? {
        val o = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, o) } ?: return null
        if (o.outWidth <= 0) return null
        return MediaInfo(name, size, o.outWidth, o.outHeight, videoCodec = o.outMimeType?.let(::codecName).orEmpty())
    }

    private fun readAv(context: Context, uri: Uri, name: String, size: Long): MediaInfo? {
        val ex = MediaExtractor()
        try {
            ex.setDataSource(context, uri, null)
            var info = MediaInfo(name, size)
            for (i in 0 until ex.trackCount) {
                val f = ex.getTrackFormat(i)
                val mime = f.getString(MediaFormat.KEY_MIME).orEmpty()
                val dur = if (f.containsKey(MediaFormat.KEY_DURATION)) f.getLong(MediaFormat.KEY_DURATION) else 0L
                if (mime.startsWith("video/") && info.videoCodec.isEmpty()) {
                    var w = f.intOr(MediaFormat.KEY_WIDTH)
                    var h = f.intOr(MediaFormat.KEY_HEIGHT)
                    val rot = f.intOr("rotation-degrees")
                    if (rot == 90 || rot == 270) { val t = w; w = h; h = t }
                    val fps = when {
                        !f.containsKey(MediaFormat.KEY_FRAME_RATE) -> 0f
                        else -> runCatching { f.getInteger(MediaFormat.KEY_FRAME_RATE).toFloat() }
                            .getOrElse { runCatching { f.getFloat(MediaFormat.KEY_FRAME_RATE) }.getOrDefault(0f) }
                    }
                    val transfer = f.intOr(MediaFormat.KEY_COLOR_TRANSFER)
                    val hdr = transfer == MediaFormat.COLOR_TRANSFER_ST2084 || transfer == MediaFormat.COLOR_TRANSFER_HLG ||
                        mime == "video/dolby-vision"
                    info = info.copy(width = w, height = h, fps = fps, videoCodec = codecName(mime), hdr = hdr,
                        durationUs = maxOf(info.durationUs, dur))
                } else if (mime.startsWith("audio/") && info.audioCodec.isEmpty()) {
                    info = info.copy(audioCodec = codecName(mime), sampleRate = f.intOr(MediaFormat.KEY_SAMPLE_RATE),
                        channels = f.intOr(MediaFormat.KEY_CHANNEL_COUNT), durationUs = maxOf(info.durationUs, dur))
                }
            }
            return if (info.videoCodec.isEmpty() && info.audioCodec.isEmpty()) null else info
        } finally {
            ex.release()
        }
    }

    private fun MediaFormat.intOr(key: String): Int = if (containsKey(key)) runCatching { getInteger(key) }.getOrDefault(0) else 0

    /** "1:02,5" / "12,0 s": duração curta e legível. */
    fun duration(us: Long): String {
        val s = us / 1e6
        return if (s < 60) String.format(Locale.getDefault(), "%.1f s", s)
        else String.format(Locale.getDefault(), "%d:%04.1f", (s / 60).toInt(), s % 60)
    }
}

/** A folha com as informações da mídia da camada `layer`. */
@Composable
internal fun MediaInfoSheet(store: EditorStore, layer: Long, onDismiss: () -> Unit) {
    val context = LocalContext.current
    var loaded by remember { mutableStateOf(false) }
    var info by remember { mutableStateOf<MediaInfo?>(null) }
    LaunchedEffect(layer) {
        info = withContext(Dispatchers.IO) { store.layerSourcePath(layer)?.let { MediaInfoProbe.read(context.applicationContext, it) } }
        loaded = true
    }
    ShellMenuSheet(onDismiss) {
        MenuSection(stringResource(R.string.media_info_title))
        val i = info
        when {
            !loaded -> Unit
            i == null -> InfoLine(stringResource(R.string.media_info_unreadable), "")
            else -> {
                InfoLine(stringResource(R.string.media_info_file), i.fileName)
                if (i.sizeBytes > 0) InfoLine(stringResource(R.string.media_info_size), Formatter.formatFileSize(context, i.sizeBytes))
                if (i.width > 0) InfoLine(stringResource(R.string.media_info_resolution), ltrPlain("${i.width} × ${i.height}"))
                if (i.fps > 0f) InfoLine(stringResource(R.string.media_info_fps), String.format(Locale.getDefault(), "%.3f", i.fps).trimEnd('0').trimEnd(',', '.'))
                if (i.durationUs > 0) InfoLine(stringResource(R.string.media_info_duration), MediaInfoProbe.duration(i.durationUs))
                if (i.videoCodec.isNotEmpty()) InfoLine(stringResource(R.string.media_info_video_codec), i.videoCodec)
                if (i.audioCodec.isNotEmpty()) InfoLine(stringResource(R.string.media_info_audio_codec), i.audioCodec)
                if (i.sampleRate > 0) InfoLine(stringResource(R.string.media_info_sample_rate), "${i.sampleRate} Hz" + if (i.channels > 0) " · ${i.channels} ch" else "")
                i.hdr?.let { InfoLine(stringResource(R.string.media_info_hdr), stringResource(if (it) R.string.common_yes else R.string.common_no)) }
            }
        }
    }
}

@Composable
private fun InfoLine(label: String, value: String) {
    Row(
        Modifier.fillMaxWidth().heightIn(min = 40.dp).padding(horizontal = 20.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(label, style = AureaType.Base.merge(TextStyle(fontSize = 15.sp, color = AureaColors.Text)))
        Spacer(Modifier.width(16.dp))
        Text(value, Modifier.weight(1f), style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Muted, textAlign = TextAlign.End)))
    }
}
