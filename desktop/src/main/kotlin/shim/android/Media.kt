@file:Suppress("unused", "UNUSED_PARAMETER")

package android.media

import android.content.Context
import android.graphics.Bitmap
import android.net.Uri
import com.aurea.pc.PcMedia
import java.io.File

/** Metadados e quadros de vídeo pelo FFmpeg que acompanha o Aurea PC (ffprobe/ffmpeg). */
class MediaMetadataRetriever : AutoCloseable {
    private var file: File? = null
    private val meta: Map<Int, String> by lazy { file?.let { PcMedia.probe(it) }.orEmpty() }

    fun setDataSource(path: String?) { file = path?.let { File(it) } }
    fun setDataSource(context: Context?, uri: Uri?) { file = uri?.let { u -> if (u.scheme == "file" || u.scheme == null) File(u.path ?: "") else null } }
    fun setDataSource(fd: java.io.FileDescriptor?) {}
    fun setDataSource(fd: java.io.FileDescriptor?, offset: Long, length: Long) {}
    fun setDataSource(uri: String, headers: Map<String, String>) { file = File(Uri.parse(uri).path ?: uri) }

    fun extractMetadata(key: Int): String? = meta[key]

    fun getFrameAtTime(timeUs: Long, option: Int): Bitmap? = file?.let { PcMedia.frameAt(it, timeUs, 0, 0) }
    fun getFrameAtTime(timeUs: Long): Bitmap? = getFrameAtTime(timeUs, OPTION_CLOSEST_SYNC)
    fun getFrameAtTime(): Bitmap? = getFrameAtTime(0L)
    fun getScaledFrameAtTime(timeUs: Long, option: Int, dstW: Int, dstH: Int): Bitmap? = file?.let { PcMedia.frameAt(it, timeUs, dstW, dstH) }
    fun getFrameAtIndex(index: Int): Bitmap? = getFrameAtTime(0L)
    val embeddedPicture: ByteArray? get() = null
    fun release() {}
    override fun close() {}

    companion object {
        const val OPTION_PREVIOUS_SYNC = 0
        const val OPTION_NEXT_SYNC = 1
        const val OPTION_CLOSEST_SYNC = 2
        const val OPTION_CLOSEST = 3
        const val METADATA_KEY_ALBUM = 1
        const val METADATA_KEY_ARTIST = 2
        const val METADATA_KEY_GENRE = 6
        const val METADATA_KEY_ALBUMARTIST = 13
        const val METADATA_KEY_TITLE = 7
        const val METADATA_KEY_DURATION = 9
        const val METADATA_KEY_NUM_TRACKS = 10
        const val METADATA_KEY_MIMETYPE = 12
        const val METADATA_KEY_HAS_AUDIO = 16
        const val METADATA_KEY_HAS_VIDEO = 17
        const val METADATA_KEY_VIDEO_WIDTH = 18
        const val METADATA_KEY_VIDEO_HEIGHT = 19
        const val METADATA_KEY_BITRATE = 20
        const val METADATA_KEY_LOCATION = 23
        const val METADATA_KEY_VIDEO_ROTATION = 24
        const val METADATA_KEY_CAPTURE_FRAMERATE = 25
        const val METADATA_KEY_HAS_IMAGE = 26
        const val METADATA_KEY_IMAGE_COUNT = 27
        const val METADATA_KEY_IMAGE_WIDTH = 29
        const val METADATA_KEY_IMAGE_HEIGHT = 30
        const val METADATA_KEY_VIDEO_FRAME_COUNT = 32
        const val METADATA_KEY_SAMPLERATE = 38
        const val METADATA_KEY_BITS_PER_SAMPLE = 39
        const val METADATA_KEY_COLOR_STANDARD = 35
        const val METADATA_KEY_COLOR_TRANSFER = 36
        const val METADATA_KEY_COLOR_RANGE = 37
    }
}

/** Áudio de prévia (biblioteca de sons, vídeo gerado): o tocador do motor fica com o projeto. */
class MediaPlayer {
    fun interface OnPreparedListener { fun onPrepared(mp: MediaPlayer) }
    fun interface OnCompletionListener { fun onCompletion(mp: MediaPlayer) }
    fun interface OnErrorListener { fun onError(mp: MediaPlayer, what: Int, extra: Int): Boolean }
    fun interface OnInfoListener { fun onInfo(mp: MediaPlayer, what: Int, extra: Int): Boolean }
    fun interface OnVideoSizeChangedListener { fun onVideoSizeChanged(mp: MediaPlayer, width: Int, height: Int) }
    fun interface OnSeekCompleteListener { fun onSeekComplete(mp: MediaPlayer) }

    private var source: File? = null
    private var prepared: OnPreparedListener? = null
    private var completion: OnCompletionListener? = null
    private val player = com.aurea.pc.PcAudioPreview()

    fun setDataSource(path: String) { source = File(path) }
    fun setDataSource(context: Context, uri: Uri) { source = File(uri.path ?: "") }
    fun setDataSource(fd: java.io.FileDescriptor) {}
    fun setDataSource(fd: java.io.FileDescriptor, offset: Long, length: Long) {}
    fun setSurface(s: android.view.Surface?) {}
    fun setDisplay(h: android.view.SurfaceHolder?) {}
    fun setAudioAttributes(a: AudioAttributes?) {}
    fun setAudioStreamType(t: Int) {}
    fun setVolume(l: Float, r: Float) { player.volume = l }
    var isLooping: Boolean
        get() = player.looping
        set(v) { player.looping = v }
    fun setOnPreparedListener(l: OnPreparedListener?) { prepared = l }
    fun setOnCompletionListener(l: OnCompletionListener?) { completion = l; player.onEnd = { com.aurea.pc.PcApp.runOnUi { l?.onCompletion(this) } } }
    fun setOnErrorListener(l: OnErrorListener?) {}
    fun setOnInfoListener(l: OnInfoListener?) {}
    fun setOnVideoSizeChangedListener(l: OnVideoSizeChangedListener?) {}
    fun setOnSeekCompleteListener(l: OnSeekCompleteListener?) {}
    fun prepare() { source?.let { player.open(it) } }
    fun prepareAsync() {
        val f = source ?: return
        Thread { player.open(f); com.aurea.pc.PcApp.runOnUi { prepared?.onPrepared(this) } }.apply { isDaemon = true }.start()
    }
    fun start() = player.play()
    fun pause() = player.pause()
    fun stop() = player.stop()
    fun reset() { player.stop(); source = null }
    fun release() = player.release()
    fun seekTo(ms: Int) = player.seek(ms.toLong())
    fun seekTo(ms: Long, mode: Int) = player.seek(ms)
    val isPlaying: Boolean get() = player.playing
    val duration: Int get() = player.durationMs.toInt()
    val currentPosition: Int get() = player.positionMs.toInt()
    val videoWidth: Int get() = 0
    val videoHeight: Int get() = 0
    companion object {
        const val SEEK_CLOSEST = 3
        const val SEEK_PREVIOUS_SYNC = 0
        @JvmStatic fun create(context: Context, uri: Uri): MediaPlayer? = MediaPlayer().also { it.setDataSource(context, uri); it.prepare() }
    }
}

class AudioAttributes private constructor() {
    class Builder {
        fun setUsage(u: Int) = this
        fun setContentType(c: Int) = this
        fun build() = AudioAttributes()
    }
    companion object {
        const val USAGE_MEDIA = 1
        const val CONTENT_TYPE_MUSIC = 2
        const val CONTENT_TYPE_MOVIE = 3
    }
}

/** Codificadores: no PC o Aurea exporta pelo FFmpeg (H.264/HEVC por software ou NVENC). */
/**
 * Os codecs do PC, no formato do Android. Decodificar: Media Foundation com
 * DXVA (hardware da placa de vídeo) para H.264, HEVC (10 bits), VP9 e AV1.
 * Codificar: o FFmpeg do Aurea PC (software). Com a tabela, o motor sabe que o
 * PC decodifica em tempo real e não gera proxy à toa.
 */
class MediaCodecList(kind: Int) {
    val codecInfos: Array<MediaCodecInfo> get() = ALL
    fun findEncoderForFormat(format: MediaFormat): String? =
        ALL.firstOrNull { it.isEncoder && format.getString(MediaFormat.KEY_MIME) in it.supportedTypes }?.name
    fun findDecoderForFormat(format: MediaFormat): String? =
        ALL.firstOrNull { !it.isEncoder && format.getString(MediaFormat.KEY_MIME) in it.supportedTypes }?.name
    companion object {
        const val REGULAR_CODECS = 0
        const val ALL_CODECS = 1
        private val ALL = arrayOf(
            MediaCodecInfo("mf.avc.decoder", false, arrayOf(MediaFormat.MIMETYPE_VIDEO_AVC), hardware = true),
            MediaCodecInfo("mf.hevc.decoder", false, arrayOf(MediaFormat.MIMETYPE_VIDEO_HEVC), hardware = true, tenBit = true),
            MediaCodecInfo("mf.vp9.decoder", false, arrayOf(MediaFormat.MIMETYPE_VIDEO_VP9), hardware = true, tenBit = true),
            MediaCodecInfo("mf.av1.decoder", false, arrayOf(MediaFormat.MIMETYPE_VIDEO_AV1), hardware = true, tenBit = true),
            MediaCodecInfo("ffmpeg.h264.encoder", true, arrayOf(MediaFormat.MIMETYPE_VIDEO_AVC)),
            MediaCodecInfo("ffmpeg.hevc.encoder", true, arrayOf(MediaFormat.MIMETYPE_VIDEO_HEVC)),
            MediaCodecInfo("ffmpeg.aac.encoder", true, arrayOf(MediaFormat.MIMETYPE_AUDIO_AAC)),
        )
        @JvmStatic fun getCodecCount(): Int = ALL.size
        @JvmStatic fun getCodecInfoAt(i: Int): MediaCodecInfo = ALL[i]
    }
}

class MediaCodecInfo(
    val name: String,
    val isEncoder: Boolean,
    val supportedTypes: Array<String>,
    private val hardware: Boolean = false,
    private val tenBit: Boolean = false,
) {
    val isHardwareAccelerated: Boolean get() = hardware
    val isSoftwareOnly: Boolean get() = !hardware
    val isVendor: Boolean get() = false
    val canonicalName: String get() = name
    val isAlias: Boolean get() = false
    fun getCapabilitiesForType(type: String): CodecCapabilities = CodecCapabilities(type, tenBit)

    class CodecCapabilities(private val type: String = "", private val tenBit: Boolean = false) {
        val videoCapabilities: VideoCapabilities = VideoCapabilities()
        val encoderCapabilities: EncoderCapabilities = EncoderCapabilities()
        val profileLevels: Array<CodecProfileLevel> get() = buildList {
            add(CodecProfileLevel().apply { profile = 1; level = 0x10000 })
            if (tenBit) add(CodecProfileLevel().apply {
                profile = when (type) {
                    MediaFormat.MIMETYPE_VIDEO_HEVC -> CodecProfileLevel.HEVCProfileMain10
                    MediaFormat.MIMETYPE_VIDEO_VP9 -> CodecProfileLevel.VP9Profile2
                    else -> CodecProfileLevel.AV1ProfileMain10
                }
                level = 0x10000
            })
        }.toTypedArray()
        val colorFormats: IntArray get() = intArrayOf(COLOR_FormatSurface, COLOR_FormatYUV420Flexible)
        fun isFeatureSupported(name: String): Boolean = false
        val maxSupportedInstances: Int get() = 16
        companion object {
            const val COLOR_FormatSurface = 0x7F000789
            const val COLOR_FormatYUV420Flexible = 0x7F420888
            const val FEATURE_HdrEditing = "hdr-editing"
        }
    }
    class VideoCapabilities {
        val supportedWidths: android.util.Range<Int> get() = android.util.Range(16, 8192)
        val supportedHeights: android.util.Range<Int> get() = android.util.Range(16, 8192)
        val bitrateRange: android.util.Range<Int> get() = android.util.Range(1, 400_000_000)
        val supportedFrameRates: android.util.Range<Int> get() = android.util.Range(1, 240)
        val widthAlignment: Int get() = 2
        val heightAlignment: Int get() = 2
        fun isSizeSupported(w: Int, h: Int): Boolean = w in 16..8192 && h in 16..8192
        fun areSizeAndRateSupported(w: Int, h: Int, fps: Double): Boolean = isSizeSupported(w, h) && fps <= 240
        fun getSupportedFrameRatesFor(w: Int, h: Int): android.util.Range<Double> = android.util.Range(1.0, 240.0)
        fun getAchievableFrameRatesFor(w: Int, h: Int): android.util.Range<Double>? = android.util.Range(1.0, 240.0)
        fun getSupportedHeightsFor(w: Int): android.util.Range<Int> = supportedHeights
        fun getSupportedWidthsFor(h: Int): android.util.Range<Int> = supportedWidths
    }
    class EncoderCapabilities {
        fun isBitrateModeSupported(mode: Int): Boolean = true
        val complexityRange: android.util.Range<Int> get() = android.util.Range(0, 0)
        val qualityRange: android.util.Range<Int> get() = android.util.Range(0, 100)
        companion object { const val BITRATE_MODE_CQ = 0; const val BITRATE_MODE_VBR = 1; const val BITRATE_MODE_CBR = 2 }
    }
    class CodecProfileLevel {
        @JvmField var profile = 0
        @JvmField var level = 0
        companion object {
            const val AVCProfileHigh = 8; const val AVCProfileBaseline = 1; const val AVCProfileMain = 2
            const val HEVCProfileMain = 1; const val HEVCProfileMain10 = 2; const val HEVCProfileMain10HDR10 = 0x1000
            const val VP9Profile0 = 1; const val VP9Profile2 = 4; const val AV1ProfileMain8 = 1; const val AV1ProfileMain10 = 2
            const val AVCLevel51 = 0x4000; const val AVCLevel52 = 0x10000; const val HEVCMainTierLevel51 = 0x10000
        }
    }
}

class MediaFormat {
    private val map = HashMap<String, Any>()
    fun setInteger(k: String, v: Int) { map[k] = v }
    fun setLong(k: String, v: Long) { map[k] = v }
    fun setFloat(k: String, v: Float) { map[k] = v }
    fun setString(k: String, v: String) { map[k] = v }
    fun getInteger(k: String): Int = map[k] as Int
    fun getInteger(k: String, def: Int): Int = map[k] as? Int ?: def
    fun getLong(k: String): Long = map[k] as Long
    fun getFloat(k: String): Float = map[k] as Float
    fun getString(k: String): String? = map[k] as? String
    fun containsKey(k: String) = k in map
    companion object {
        const val MIMETYPE_VIDEO_AVC = "video/avc"
        const val MIMETYPE_VIDEO_HEVC = "video/hevc"
        const val MIMETYPE_VIDEO_AV1 = "video/av01"
        const val MIMETYPE_VIDEO_VP9 = "video/x-vnd.on2.vp9"
        const val MIMETYPE_AUDIO_AAC = "audio/mp4a-latm"
        const val KEY_MIME = "mime"; const val KEY_WIDTH = "width"; const val KEY_HEIGHT = "height"
        const val KEY_BIT_RATE = "bitrate"; const val KEY_FRAME_RATE = "frame-rate"; const val KEY_COLOR_FORMAT = "color-format"
        const val KEY_I_FRAME_INTERVAL = "i-frame-interval"; const val KEY_DURATION = "durationUs"; const val KEY_ROTATION = "rotation-degrees"
        const val KEY_PROFILE = "profile"; const val KEY_LEVEL = "level"; const val KEY_SAMPLE_RATE = "sample-rate"; const val KEY_CHANNEL_COUNT = "channel-count"
        const val KEY_COLOR_STANDARD = "color-standard"; const val KEY_COLOR_TRANSFER = "color-transfer"; const val KEY_COLOR_RANGE = "color-range"
        const val KEY_BITRATE_MODE = "bitrate-mode"; const val KEY_PRIORITY = "priority"; const val KEY_OPERATING_RATE = "operating-rate"
        const val KEY_MAX_INPUT_SIZE = "max-input-size"; const val KEY_CAPTURE_RATE = "capture-rate"
        const val COLOR_STANDARD_BT2020 = 6; const val COLOR_STANDARD_BT709 = 1; const val COLOR_TRANSFER_ST2084 = 6; const val COLOR_TRANSFER_HLG = 7
        const val COLOR_TRANSFER_SDR_VIDEO = 3; const val COLOR_RANGE_LIMITED = 2; const val COLOR_RANGE_FULL = 1
        @JvmStatic fun createVideoFormat(mime: String, w: Int, h: Int) = MediaFormat().apply { setString(KEY_MIME, mime); setInteger(KEY_WIDTH, w); setInteger(KEY_HEIGHT, h) }
        @JvmStatic fun createAudioFormat(mime: String, rate: Int, ch: Int) = MediaFormat().apply { setString(KEY_MIME, mime); setInteger(KEY_SAMPLE_RATE, rate); setInteger(KEY_CHANNEL_COUNT, ch) }
    }
}

class MediaExtractor {
    private var file: File? = null
    private val meta by lazy { file?.let { PcMedia.probe(it) }.orEmpty() }
    fun setDataSource(path: String) { file = File(path) }
    fun setDataSource(context: Context, uri: Uri, headers: Map<String, String>?) { file = File(uri.path ?: "") }
    fun setDataSource(fd: java.io.FileDescriptor) {}
    val trackCount: Int get() = (if (meta[MediaMetadataRetriever.METADATA_KEY_HAS_VIDEO] == "yes") 1 else 0) + (if (meta[MediaMetadataRetriever.METADATA_KEY_HAS_AUDIO] == "yes") 1 else 0)
    fun getTrackFormat(i: Int): MediaFormat {
        val hasVideo = meta[MediaMetadataRetriever.METADATA_KEY_HAS_VIDEO] == "yes"
        return if (i == 0 && hasVideo) MediaFormat.createVideoFormat(meta[1000] ?: MediaFormat.MIMETYPE_VIDEO_AVC,
            meta[MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH]?.toIntOrNull() ?: 0,
            meta[MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT]?.toIntOrNull() ?: 0).apply {
            meta[MediaMetadataRetriever.METADATA_KEY_DURATION]?.toLongOrNull()?.let { setLong(MediaFormat.KEY_DURATION, it * 1000) }
            meta[MediaMetadataRetriever.METADATA_KEY_CAPTURE_FRAMERATE]?.toFloatOrNull()?.let { setFloat(MediaFormat.KEY_FRAME_RATE, it) }
            meta[MediaMetadataRetriever.METADATA_KEY_VIDEO_ROTATION]?.toIntOrNull()?.let { setInteger(MediaFormat.KEY_ROTATION, it) }
            meta[MediaMetadataRetriever.METADATA_KEY_BITRATE]?.toIntOrNull()?.let { setInteger(MediaFormat.KEY_BIT_RATE, it) }
        } else MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC,
            meta[MediaMetadataRetriever.METADATA_KEY_SAMPLERATE]?.toIntOrNull() ?: 48000, 2)
    }
    fun selectTrack(i: Int) {}
    fun release() {}
}

class MediaRecorder(context: Context? = null) {
    fun setAudioSource(s: Int) {}
    fun setOutputFormat(f: Int) {}
    fun setAudioEncoder(e: Int) {}
    fun setAudioSamplingRate(r: Int) {}
    fun setAudioEncodingBitRate(r: Int) {}
    fun setAudioChannels(c: Int) {}
    fun interface OnErrorListener { fun onError(mr: MediaRecorder, what: Int, extra: Int) }
    fun setOnErrorListener(l: OnErrorListener?) {}
    fun setOutputFile(path: String) { out = File(path) }
    fun setOutputFile(f: File) { out = f }
    private var out: File? = null
    private val rec = com.aurea.pc.PcMicrophone()
    fun prepare() {}
    fun start() { out?.let { rec.start(it) } }
    fun stop() { rec.stop() }
    fun pause() = rec.pause()
    fun resume() = rec.resume()
    fun reset() {}
    fun release() { rec.stop() }
    val maxAmplitude: Int get() = rec.maxAmplitude()
    object AudioSource { const val MIC = 1; const val DEFAULT = 0; const val VOICE_RECOGNITION = 6; const val UNPROCESSED = 9; const val CAMCORDER = 5 }
    object OutputFormat { const val MPEG_4 = 2; const val AAC_ADTS = 6; const val THREE_GPP = 1; const val DEFAULT = 0 }
    object AudioEncoder { const val AAC = 3; const val DEFAULT = 0; const val HE_AAC = 4; const val OPUS = 7 }
}

/** Galeria do Windows: Vídeos/Imagens já aparecem no Explorer; nada a escanear. */
object MediaScannerConnection {
    fun interface OnScanCompletedListener { fun onScanCompleted(path: String?, uri: Uri?) }
    @JvmStatic fun scanFile(context: Context, paths: Array<String>?, mimeTypes: Array<String>?, cb: OnScanCompletedListener?) {
        paths?.forEach { p -> cb?.onScanCompleted(p, Uri.fromFile(File(p))) }
    }
}

/** Detector de rosto do Android: no PC o Headbop usa o rastreamento do motor. */
class FaceDetector(private val width: Int, private val height: Int, private val maxFaces: Int) {
    class Face {
        fun confidence(): Float = 0f
        fun getMidPoint(p: android.graphics.PointF) {}
        fun eyesDistance(): Float = 0f
        fun pose(axis: Int): Float = 0f
        companion object { const val CONFIDENCE_THRESHOLD = 0.4f; const val EULER_X = 0; const val EULER_Y = 1; const val EULER_Z = 2 }
    }
    fun findFaces(bitmap: Bitmap, faces: Array<Face?>): Int = com.aurea.pc.PcFaces.find(bitmap, faces)
}

class ExifInterface {
    private val orientation: Int
    constructor(path: String) { orientation = com.aurea.pc.PcMedia.exifOrientation(File(path)) }
    constructor(file: File) { orientation = com.aurea.pc.PcMedia.exifOrientation(file) }
    constructor(input: java.io.InputStream) { orientation = com.aurea.pc.PcMedia.exifOrientation(input.readBytes()) }
    constructor(fd: java.io.FileDescriptor) { orientation = ORIENTATION_NORMAL }
    fun getAttributeInt(tag: String, def: Int): Int = if (tag == TAG_ORIENTATION) orientation else def
    fun getAttribute(tag: String): String? = if (tag == TAG_ORIENTATION) orientation.toString() else null
    val rotationDegrees: Int get() = when (orientation) { ORIENTATION_ROTATE_90, ORIENTATION_TRANSPOSE -> 90; ORIENTATION_ROTATE_180 -> 180; ORIENTATION_ROTATE_270, ORIENTATION_TRANSVERSE -> 270; else -> 0 }
    val isFlipped: Boolean get() = orientation in setOf(ORIENTATION_FLIP_HORIZONTAL, ORIENTATION_FLIP_VERTICAL, ORIENTATION_TRANSPOSE, ORIENTATION_TRANSVERSE)
    companion object {
        const val TAG_ORIENTATION = "Orientation"
        const val TAG_DATETIME_ORIGINAL = "DateTimeOriginal"
        const val ORIENTATION_UNDEFINED = 0
        const val ORIENTATION_NORMAL = 1
        const val ORIENTATION_FLIP_HORIZONTAL = 2
        const val ORIENTATION_ROTATE_180 = 3
        const val ORIENTATION_FLIP_VERTICAL = 4
        const val ORIENTATION_TRANSPOSE = 5
        const val ORIENTATION_ROTATE_90 = 6
        const val ORIENTATION_TRANSVERSE = 7
        const val ORIENTATION_ROTATE_270 = 8
    }
}

class AudioManager private constructor() {
    companion object {
        const val STREAM_MUSIC = 3
        const val AUDIOFOCUS_GAIN = 1
        const val AUDIOFOCUS_REQUEST_GRANTED = 1
        const val PROPERTY_OUTPUT_SAMPLE_RATE = "android.media.property.OUTPUT_SAMPLE_RATE"
        const val PROPERTY_OUTPUT_FRAMES_PER_BUFFER = "android.media.property.OUTPUT_FRAMES_PER_BUFFER"
    }
}
