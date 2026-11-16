package com.aurea.pc

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.MediaMetadataRetriever as M
import org.json.JSONObject
import java.io.File
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.TimeUnit

/**
 * Metadados, quadros e miniaturas de mídia pelo FFmpeg que vai junto com o
 * Aurea PC (ffprobe.exe/ffmpeg.exe ao lado da aurea.dll). A decodificação de
 * verdade, no palco e na exportação, é do motor.
 */
object PcMedia {
    private fun tool(name: String): String {
        val dirs = listOfNotNull(
            PcPaths.appDir, PcPaths.appDir.parentFile?.let { File(it, "windows") },
            System.getProperty("aurea.ffmpegBin")?.let { File(it) },
        )
        return dirs.map { File(it, "$name.exe") }.firstOrNull { it.isFile }?.absolutePath ?: name
    }
    private val ffprobe by lazy { tool("ffprobe") }
    private val ffmpeg by lazy { tool("ffmpeg") }

    private fun run(vararg cmd: String, timeoutS: Long = 20): ByteArray? = runCatching {
        val p = ProcessBuilder(*cmd).redirectError(ProcessBuilder.Redirect.DISCARD).start()
        val out = p.inputStream.readBytes()
        if (!p.waitFor(timeoutS, TimeUnit.SECONDS)) { p.destroyForcibly(); return null }
        if (p.exitValue() != 0) null else out
    }.getOrNull()

    private val probes = ConcurrentHashMap<String, Map<Int, String>>()

    /** As chaves de MediaMetadataRetriever (+1000 = MIME do codec de vídeo). */
    fun probe(f: File): Map<Int, String> {
        val key = f.absolutePath + ":" + f.lastModified() + ":" + f.length()
        probes[key]?.let { return it }
        val json = run(ffprobe, "-v", "error", "-print_format", "json", "-show_format", "-show_streams", f.absolutePath)
            ?.toString(Charsets.UTF_8) ?: return emptyMap()
        val out = HashMap<Int, String>()
        runCatching {
            val o = JSONObject(json)
            val format = o.optJSONObject("format")
            val streams = o.optJSONArray("streams")
            var hasVideo = false; var hasAudio = false
            for (i in 0 until (streams?.length() ?: 0)) {
                val s = streams!!.getJSONObject(i)
                when (s.optString("codec_type")) {
                    "video" -> if (!hasVideo && s.optJSONObject("disposition")?.optInt("attached_pic") != 1) {
                        hasVideo = true
                        out[M.METADATA_KEY_VIDEO_WIDTH] = s.optInt("width").toString()
                        out[M.METADATA_KEY_VIDEO_HEIGHT] = s.optInt("height").toString()
                        var rot = s.optJSONObject("tags")?.optString("rotate")?.toIntOrNull() ?: 0
                        s.optJSONArray("side_data_list")?.let { sd ->
                            for (j in 0 until sd.length()) sd.getJSONObject(j).optDouble("rotation", Double.NaN).takeIf { !it.isNaN() }?.let { rot = -it.toInt() }
                        }
                        out[M.METADATA_KEY_VIDEO_ROTATION] = (((rot % 360) + 360) % 360).toString()
                        s.optString("nb_frames").toIntOrNull()?.let { out[M.METADATA_KEY_VIDEO_FRAME_COUNT] = it.toString() }
                        s.optString("avg_frame_rate").split('/').let { r ->
                            val fps = (r.getOrNull(0)?.toDoubleOrNull() ?: 0.0) / (r.getOrNull(1)?.toDoubleOrNull()?.takeIf { it > 0 } ?: 1.0)
                            if (fps > 0) out[M.METADATA_KEY_CAPTURE_FRAMERATE] = fps.toString()
                        }
                        out[1000] = when (s.optString("codec_name")) { "hevc" -> "video/hevc"; "av1" -> "video/av01"; "vp9" -> "video/x-vnd.on2.vp9"; else -> "video/avc" }
                        s.optString("color_transfer").let { t -> out[M.METADATA_KEY_COLOR_TRANSFER] = when (t) { "smpte2084" -> "6"; "arib-std-b67" -> "7"; else -> "3" } }
                        s.optString("color_primaries").let { p -> out[M.METADATA_KEY_COLOR_STANDARD] = if (p == "bt2020") "6" else "1" }
                    }
                    "audio" -> if (!hasAudio) {
                        hasAudio = true
                        s.optString("sample_rate").toIntOrNull()?.let { out[M.METADATA_KEY_SAMPLERATE] = it.toString() }
                    }
                }
            }
            out[M.METADATA_KEY_HAS_VIDEO] = if (hasVideo) "yes" else "no"
            out[M.METADATA_KEY_HAS_AUDIO] = if (hasAudio) "yes" else "no"
            format?.optString("duration")?.toDoubleOrNull()?.let { out[M.METADATA_KEY_DURATION] = (it * 1000).toLong().toString() }
            format?.optString("bit_rate")?.toLongOrNull()?.let { out[M.METADATA_KEY_BITRATE] = it.toString() }
            out[M.METADATA_KEY_MIMETYPE] = android.content.ContentResolver.mimeOf(f.name)
            format?.optJSONObject("tags")?.let { t ->
                t.optString("title").takeIf { it.isNotEmpty() }?.let { out[M.METADATA_KEY_TITLE] = it }
                t.optString("artist").takeIf { it.isNotEmpty() }?.let { out[M.METADATA_KEY_ARTIST] = it }
            }
        }
        probes[key] = out
        return out
    }

    /** Um quadro em [timeUs] (o mais perto), opcionalmente reduzido para caber em dstW x dstH. */
    fun frameAt(f: File, timeUs: Long, dstW: Int, dstH: Int): Bitmap? {
        val scale = if (dstW > 0 && dstH > 0) listOf("-vf", "scale=w=$dstW:h=$dstH:force_original_aspect_ratio=decrease") else emptyList()
        val cmd = listOf(ffmpeg, "-v", "error", "-ss", String.format(java.util.Locale.ROOT, "%.3f", timeUs / 1e6), "-i", f.absolutePath,
            "-frames:v", "1") + scale + listOf("-f", "image2pipe", "-vcodec", "png", "-")
        val png = run(*cmd.toTypedArray()) ?: return null
        return BitmapFactory.decodeByteArray(png, 0, png.size)
    }

    fun thumbnail(f: File, w: Int, h: Int): Bitmap? {
        val mime = android.content.ContentResolver.mimeOf(f.name)
        if (mime.startsWith("image/")) {
            val o = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeFile(f.path, o)
            var sample = 1
            while (o.outWidth / (sample * 2) >= w && o.outHeight / (sample * 2) >= h) sample *= 2
            return BitmapFactory.decodeFile(f.path, BitmapFactory.Options().apply { inSampleSize = sample })
        }
        return frameAt(f, 0, w, h)
    }

    /** Formatos que o Skia não lê (HEIC, TIFF, PSD…): o FFmpeg converte para PNG. */
    fun decodeFallback(bytes: ByteArray, opts: BitmapFactory.Options?): Bitmap? {
        val tmp = File.createTempFile("aurea-img", ".bin", PcPaths.cache)
        return try {
            tmp.writeBytes(bytes)
            val png = run(ffmpeg, "-v", "error", "-i", tmp.absolutePath, "-frames:v", "1", "-f", "image2pipe", "-vcodec", "png", "-") ?: return null
            val o = BitmapFactory.Options()
            BitmapFactory.decodeByteArray(png, 0, png.size, o)?.also {
                if (opts != null) { opts.outWidth = it.width; opts.outHeight = it.height; opts.outMimeType = "image/png" }
            }
        } finally { tmp.delete() }
    }

    fun openExternally(uri: android.net.Uri?) {}

    /** Orientação EXIF de um JPEG (1 = normal). */
    fun exifOrientation(f: File): Int = runCatching { f.inputStream().use { exifOrientation(it.readNBytes(256 * 1024)) } }.getOrDefault(1)

    fun exifOrientation(b: ByteArray): Int {
        if (b.size < 4 || b[0] != 0xFF.toByte() || b[1] != 0xD8.toByte()) return 1
        var i = 2
        while (i + 4 < b.size) {
            if (b[i] != 0xFF.toByte()) return 1
            val marker = b[i + 1].toInt() and 0xFF
            val len = ((b[i + 2].toInt() and 0xFF) shl 8) or (b[i + 3].toInt() and 0xFF)
            if (marker == 0xE1 && i + 10 < b.size && String(b, i + 4, 4, Charsets.US_ASCII) == "Exif") {
                val t = i + 10
                val le = b[t] == 'I'.code.toByte()
                fun u16(p: Int) = if (le) (b[p].toInt() and 0xFF) or ((b[p + 1].toInt() and 0xFF) shl 8) else ((b[p].toInt() and 0xFF) shl 8) or (b[p + 1].toInt() and 0xFF)
                fun u32(p: Int) = if (le) u16(p) or (u16(p + 2) shl 16) else (u16(p) shl 16) or u16(p + 2)
                val ifd = t + u32(t + 4)
                if (ifd + 2 > b.size) return 1
                val n = u16(ifd)
                for (k in 0 until n) {
                    val e = ifd + 2 + k * 12
                    if (e + 10 > b.size) break
                    if (u16(e) == 0x0112) return u16(e + 8).coerceIn(1, 8)
                }
                return 1
            }
            i += 2 + len
        }
        return 1
    }
}

/** Rostos: o detector do Android não existe no PC (o Headbop segue sem auto-rosto). */
object PcFaces {
    fun find(bitmap: Bitmap, faces: Array<android.media.FaceDetector.Face?>): Int = 0
}
