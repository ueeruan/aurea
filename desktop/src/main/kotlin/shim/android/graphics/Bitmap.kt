package android.graphics

import androidx.compose.ui.graphics.asComposeImageBitmap
import org.jetbrains.skia.ColorAlphaType
import org.jetbrains.skia.ColorType
import org.jetbrains.skia.EncodedImageFormat
import org.jetbrains.skia.Image
import org.jetbrains.skia.ImageInfo
import org.jetbrains.skia.SamplingMode
import java.io.InputStream
import java.io.OutputStream
import java.nio.Buffer
import java.nio.ByteBuffer
import java.util.concurrent.atomic.AtomicInteger

/**
 * `Bitmap` do Android sobre `org.jetbrains.skia.Bitmap`: a mesma memória que o
 * Compose desenha (sem cópia em `asImageBitmap`). Pixels em RGBA_8888
 * pré-multiplicado — o layout de `copyPixelsToBuffer`/`FromBuffer` do Android.
 */
class Bitmap internal constructor(internal val sk: org.jetbrains.skia.Bitmap, config: Config) {

    enum class Config { ALPHA_8, RGB_565, ARGB_4444, ARGB_8888, RGBA_F16, HARDWARE, RGBA_1010102 }
    enum class CompressFormat { JPEG, PNG, WEBP, WEBP_LOSSY, WEBP_LOSSLESS }

    var config: Config = config; private set
    val width: Int get() = sk.width
    val height: Int get() = sk.height
    var density: Int = DENSITY_NONE
    var isMutable: Boolean = true; private set
    @Volatile var isRecycled: Boolean = false; private set
    private var gen = nextGen.incrementAndGet()
    val generationId: Int get() = gen
    val rowBytes: Int get() = sk.rowBytes
    val byteCount: Int get() = sk.rowBytes * height
    val allocationByteCount: Int get() = byteCount
    var isPremultiplied: Boolean = true
    val colorSpace: ColorSpace? get() = ColorSpace.get(ColorSpace.Named.SRGB)

    fun hasAlpha(): Boolean = !sk.isOpaque
    fun setHasAlpha(v: Boolean) {}
    fun hasMipMap(): Boolean = false
    fun setHasMipMap(v: Boolean) {}
    fun prepareToDraw() {}
    fun recycle() {
        if (!isRecycled) {
            isRecycled = true
            synchronized(drawLock) { drawImage?.close(); drawImage = null }
            sk.close()
        }
    }

    /**
     * 0.5.3: a imagem do Skia usada por `Canvas.drawBitmap`, guardada até os pixels
     * mudarem (`gen`). Antes cada chamada criava uma imagem nova (cópia dos pixels,
     * porque o bitmap é mutável) e, por ser outra imagem, o Skia reenviava a textura
     * à GPU: uma cópia + um envio por miniatura a cada desenho da timeline. O bloco
     * roda com a trava: a imagem não é fechada por outra thread no meio do desenho.
     */
    private val drawLock = Any()
    private var drawImage: Image? = null
    private var drawImageGen = 0
    internal fun <T> withSkiaImage(block: (Image) -> T): T = synchronized(drawLock) {
        var img = drawImage
        if (img == null || drawImageGen != gen) {
            img?.close()
            img = Image.makeFromBitmap(sk)
            drawImage = img
            drawImageGen = gen
        }
        block(img)
    }
    fun notifyPixelsChanged() { gen = nextGen.incrementAndGet(); sk.notifyPixelsChanged() }
    fun getScaledWidth(targetDensity: Int): Int = width
    fun getScaledHeight(targetDensity: Int): Int = height

    fun getPixel(x: Int, y: Int): Int = sk.getColor(x, y)

    fun getPixels(pixels: IntArray, offset: Int, stride: Int, x: Int, y: Int, w: Int, h: Int) {
        for (row in 0 until h) for (col in 0 until w) pixels[offset + row * stride + col] = sk.getColor(x + col, y + row)
    }

    fun setPixel(x: Int, y: Int, color: Int) = setPixels(intArrayOf(color), 0, 1, x, y, 1, 1)

    fun setPixels(pixels: IntArray, offset: Int, stride: Int, x: Int, y: Int, w: Int, h: Int) {
        val bytes = sk.readPixels() ?: return
        val rb = sk.rowBytes
        for (row in 0 until h) for (col in 0 until w) {
            val c = pixels[offset + row * stride + col]
            val a = c ushr 24 and 0xff
            val p = (y + row) * rb + (x + col) * 4
            bytes[p] = premul(c shr 16 and 0xff, a).toByte()
            bytes[p + 1] = premul(c shr 8 and 0xff, a).toByte()
            bytes[p + 2] = premul(c and 0xff, a).toByte()
            bytes[p + 3] = a.toByte()
        }
        sk.installPixels(bytes)
        notifyPixelsChanged()
    }

    fun eraseColor(color: Int) {
        org.jetbrains.skia.Canvas(sk).use { it.clear(color) }
        notifyPixelsChanged()
    }

    fun copyPixelsToBuffer(dst: Buffer) {
        val bytes = sk.readPixels() ?: return
        when (dst) {
            is ByteBuffer -> dst.put(bytes, 0, minOf(bytes.size, dst.remaining()))
            is java.nio.IntBuffer -> {
                val ib = ByteBuffer.wrap(bytes).order(java.nio.ByteOrder.nativeOrder()).asIntBuffer()
                val n = minOf(ib.remaining(), dst.remaining())
                val tmp = IntArray(n); ib.get(tmp); dst.put(tmp)
            }
            else -> error("buffer não suportado: ${dst.javaClass}")
        }
    }

    fun copyPixelsFromBuffer(src: Buffer) {
        val n = sk.rowBytes * height
        val bytes = ByteArray(n)
        when (src) {
            is ByteBuffer -> src.get(bytes, 0, minOf(n, src.remaining()))
            is java.nio.IntBuffer -> {
                val ints = IntArray(minOf(n / 4, src.remaining())); src.get(ints)
                ByteBuffer.wrap(bytes).order(java.nio.ByteOrder.nativeOrder()).asIntBuffer().put(ints)
            }
            else -> error("buffer não suportado: ${src.javaClass}")
        }
        sk.installPixels(bytes)
        notifyPixelsChanged()
    }

    fun compress(format: CompressFormat, quality: Int, stream: OutputStream): Boolean {
        val fmt = when (format) {
            CompressFormat.JPEG -> EncodedImageFormat.JPEG
            CompressFormat.PNG -> EncodedImageFormat.PNG
            else -> EncodedImageFormat.WEBP
        }
        val data = Image.makeFromBitmap(sk).use { it.encodeToData(fmt, quality.coerceIn(0, 100)) } ?: return false
        stream.write(data.bytes)
        return true
    }

    fun copy(config: Config, isMutable: Boolean): Bitmap? {
        val out = createBitmap(width, height, config)
        val bytes = sk.readPixels() ?: return null
        out.sk.installPixels(bytes)
        out.isMutable = isMutable
        out.density = density
        return out
    }

    fun asShared(): Bitmap = this
    fun extractAlpha(): Bitmap = copy(Config.ARGB_8888, true)!!
    fun sameAs(other: Bitmap?): Boolean = other != null && other.width == width && other.height == height &&
        sk.readPixels().contentEquals(other.sk.readPixels())
    fun reconfigure(width: Int, height: Int, config: Config) {
        sk.allocPixels(info(width, height))
        this.config = config
        notifyPixelsChanged()
    }

    /** A imagem do Compose sobre os MESMOS pixels (cacheada até a próxima mudança). */
    @Volatile private var composeCache: Pair<Int, androidx.compose.ui.graphics.ImageBitmap>? = null
    internal fun toCompose(): androidx.compose.ui.graphics.ImageBitmap {
        composeCache?.let { if (it.first == gen) return it.second }
        val img = sk.asComposeImageBitmap()
        composeCache = gen to img
        return img
    }

    companion object {
        const val DENSITY_NONE = 0
        private val nextGen = AtomicInteger(1)

        internal fun info(w: Int, h: Int) = ImageInfo(w, h, ColorType.RGBA_8888, ColorAlphaType.PREMUL)
        private fun premul(c: Int, a: Int) = (c * a + 127) / 255

        internal fun wrap(sk: org.jetbrains.skia.Bitmap): Bitmap = Bitmap(sk, Config.ARGB_8888)

        @JvmStatic fun createBitmap(width: Int, height: Int, config: Config): Bitmap {
            val sk = org.jetbrains.skia.Bitmap()
            sk.allocPixels(info(width.coerceAtLeast(1), height.coerceAtLeast(1)))
            sk.erase(0)
            return Bitmap(sk, config)
        }
        @JvmStatic fun createBitmap(width: Int, height: Int, config: Config, hasAlpha: Boolean): Bitmap = createBitmap(width, height, config)
        @JvmStatic fun createBitmap(width: Int, height: Int, config: Config, hasAlpha: Boolean, colorSpace: ColorSpace): Bitmap = createBitmap(width, height, config)
        @JvmStatic fun createBitmap(display: Any?, width: Int, height: Int, config: Config): Bitmap = createBitmap(width, height, config)

        @JvmStatic fun createBitmap(colors: IntArray, width: Int, height: Int, config: Config): Bitmap =
            createBitmap(width, height, config).also { it.setPixels(colors, 0, width, 0, 0, width, height) }
        @JvmStatic fun createBitmap(colors: IntArray, offset: Int, stride: Int, width: Int, height: Int, config: Config): Bitmap =
            createBitmap(width, height, config).also { it.setPixels(colors, offset, stride, 0, 0, width, height) }

        @JvmStatic fun createBitmap(src: Bitmap): Bitmap = src.copy(src.config, false)!!

        @JvmStatic fun createBitmap(src: Bitmap, x: Int, y: Int, w: Int, h: Int): Bitmap = createBitmap(src, x, y, w, h, null, false)

        @JvmStatic fun createBitmap(src: Bitmap, x: Int, y: Int, w: Int, h: Int, m: Matrix?, filter: Boolean): Bitmap {
            val (ow, oh) = m?.mapSize(w, h) ?: (w to h)
            val out = createBitmap(ow, oh, Config.ARGB_8888)
            val c = Canvas(out)
            if (m != null) c.concat(m.centeredFor(w, h, ow, oh))
            c.drawBitmap(src, Rect(x, y, x + w, y + h), RectF(0f, 0f, w.toFloat(), h.toFloat()), Paint().also { it.isFilterBitmap = filter })
            out.notifyPixelsChanged()
            return out
        }

        @JvmStatic fun createScaledBitmap(src: Bitmap, dstWidth: Int, dstHeight: Int, filter: Boolean): Bitmap {
            val out = createBitmap(dstWidth.coerceAtLeast(1), dstHeight.coerceAtLeast(1), Config.ARGB_8888)
            org.jetbrains.skia.Canvas(out.sk).use { c ->
                Image.makeFromBitmap(src.sk).use { img ->
                    c.drawImageRect(img, org.jetbrains.skia.Rect.makeWH(src.width.toFloat(), src.height.toFloat()),
                        org.jetbrains.skia.Rect.makeWH(out.width.toFloat(), out.height.toFloat()),
                        if (filter) SamplingMode.LINEAR else SamplingMode.DEFAULT, null, true)
                }
            }
            out.notifyPixelsChanged()
            return out
        }
    }
}

class BitmapFactory private constructor() {
    class Options {
        @JvmField var inJustDecodeBounds = false
        @JvmField var inSampleSize = 1
        @JvmField var inPreferredConfig: Bitmap.Config? = Bitmap.Config.ARGB_8888
        @JvmField var inPreferredColorSpace: ColorSpace? = null
        @JvmField var inMutable = false
        @JvmField var inScaled = true
        @JvmField var inPremultiplied = true
        @JvmField var inDensity = 0
        @JvmField var inTargetDensity = 0
        @JvmField var inBitmap: Bitmap? = null
        @JvmField var inDither = false
        @JvmField var outWidth = -1
        @JvmField var outHeight = -1
        @JvmField var outMimeType: String? = null
        @JvmField var outConfig: Bitmap.Config? = null
    }

    companion object {
        @JvmStatic fun decodeByteArray(data: ByteArray, offset: Int, length: Int): Bitmap? = decodeByteArray(data, offset, length, null)

        @JvmStatic fun decodeByteArray(data: ByteArray, offset: Int, length: Int, opts: Options?): Bitmap? {
            val bytes = if (offset == 0 && length == data.size) data else data.copyOfRange(offset, offset + length)
            return decode(bytes, opts)
        }

        @JvmStatic fun decodeStream(input: InputStream?): Bitmap? = decodeStream(input, null, null)
        @JvmStatic fun decodeStream(input: InputStream?, outPadding: Rect?, opts: Options?): Bitmap? =
            input?.readBytes()?.let { decode(it, opts) }

        @JvmStatic fun decodeFile(path: String?): Bitmap? = decodeFile(path, null)
        @JvmStatic fun decodeFile(path: String?, opts: Options?): Bitmap? =
            path?.let { java.io.File(it) }?.takeIf { it.isFile }?.let { decode(it.readBytes(), opts) }

        @JvmStatic fun decodeFileDescriptor(fd: java.io.FileDescriptor?, outPadding: Rect?, opts: Options?): Bitmap? =
            fd?.let { java.io.FileInputStream(it).readBytes() }?.let { decode(it, opts) }
        @JvmStatic fun decodeFileDescriptor(fd: java.io.FileDescriptor?): Bitmap? = decodeFileDescriptor(fd, null, null)

        @JvmStatic fun decodeResource(res: android.content.res.Resources, id: Int): Bitmap? =
            runCatching { res.openRawResource(id).use { decodeStream(it) } }.getOrNull()

        private fun decode(bytes: ByteArray, opts: Options?): Bitmap? {
            val data = org.jetbrains.skia.Data.makeFromBytes(bytes)
            val codec = runCatching { org.jetbrains.skia.Codec.makeFromData(data) }.getOrNull()
                ?: return com.aurea.pc.PcMedia.decodeFallback(bytes, opts)
            val w = codec.width
            val h = codec.height
            if (opts != null) {
                opts.outWidth = w; opts.outHeight = h
                opts.outMimeType = when (codec.encodedImageFormat) {
                    EncodedImageFormat.PNG -> "image/png"
                    EncodedImageFormat.JPEG -> "image/jpeg"
                    EncodedImageFormat.WEBP -> "image/webp"
                    EncodedImageFormat.GIF -> "image/gif"
                    EncodedImageFormat.BMP -> "image/bmp"
                    EncodedImageFormat.HEIF -> "image/heif"
                    else -> null
                }
                opts.outConfig = Bitmap.Config.ARGB_8888
                if (opts.inJustDecodeBounds) { codec.close(); return null }
            }
            val full = org.jetbrains.skia.Bitmap()
            full.allocPixels(Bitmap.info(w, h))
            // A orientação EXIF fica com quem chama (ExifInterface), como no Android.
            runCatching { codec.readPixels(full, 0) }.onFailure { codec.close(); full.close(); return null }
            codec.close()
            val sample = (opts?.inSampleSize ?: 1).coerceAtLeast(1).let { s -> Integer.highestOneBit(s) }
            val bmp = Bitmap.wrap(full)
            if (sample <= 1) return bmp
            val scaled = Bitmap.createScaledBitmap(bmp, (w / sample).coerceAtLeast(1), (h / sample).coerceAtLeast(1), true)
            bmp.recycle()
            if (opts != null) { opts.outWidth = scaled.width; opts.outHeight = scaled.height }
            return scaled
        }
    }
}


class ColorSpace private constructor(val named: Named) {
    enum class Named { SRGB, LINEAR_SRGB, DISPLAY_P3, BT2020, BT709, EXTENDED_SRGB, ADOBE_RGB, BT2020_HLG, BT2020_PQ }
    val name: String get() = named.name
    val isSrgb: Boolean get() = named == Named.SRGB
    val isWideGamut: Boolean get() = named in setOf(Named.DISPLAY_P3, Named.BT2020, Named.ADOBE_RGB)
    companion object {
        private val cache = Named.entries.associateWith { ColorSpace(it) }
        @JvmStatic fun get(n: Named): ColorSpace = cache.getValue(n)
    }
}
