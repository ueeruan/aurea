package android.graphics

import org.jetbrains.skia.FilterMipmap
import org.jetbrains.skia.FilterMode
import org.jetbrains.skia.Font
import org.jetbrains.skia.Image
import org.jetbrains.skia.MipmapMode
import org.jetbrains.skia.PaintMode
import org.jetbrains.skia.PaintStrokeCap
import org.jetbrains.skia.PaintStrokeJoin
import org.jetbrains.skia.SamplingMode

/**
 * `Canvas` do Android sobre o Canvas do Skia: o da tela (`platformCanvas` de um
 * DrawScope) ou o de um [Bitmap].
 */
class Canvas internal constructor(internal val sk: org.jetbrains.skia.Canvas, private val target: Bitmap?) {
    constructor(bitmap: Bitmap) : this(org.jetbrains.skia.Canvas(bitmap.sk), bitmap)
    constructor(skia: org.jetbrains.skia.Canvas) : this(skia, null)

    val width: Int get() = target?.width ?: 0
    val height: Int get() = target?.height ?: 0
    val isHardwareAccelerated: Boolean get() = target == null

    private fun touched() { target?.notifyPixelsChanged() }

    fun save(): Int = sk.save()
    fun restore() = sk.restore()
    fun restoreToCount(count: Int) = sk.restoreToCount(count)
    val saveCount: Int get() = sk.saveCount
    fun saveLayer(bounds: RectF?, paint: Paint?): Int =
        if (bounds != null) sk.saveLayer(bounds.sk(), paint?.sk) else sk.saveLayer(null, paint?.sk)
    fun saveLayer(l: Float, t: Float, r: Float, b: Float, paint: Paint?): Int = sk.saveLayer(l, t, r, b, paint?.sk)
    fun saveLayerAlpha(bounds: RectF?, alpha: Int): Int = saveLayer(bounds, Paint().also { it.alpha = alpha })

    fun translate(dx: Float, dy: Float) { sk.translate(dx, dy) }
    fun scale(sx: Float, sy: Float) { sk.scale(sx, sy) }
    fun scale(sx: Float, sy: Float, px: Float, py: Float) { sk.translate(px, py); sk.scale(sx, sy); sk.translate(-px, -py) }
    fun rotate(degrees: Float) { sk.rotate(degrees) }
    fun rotate(degrees: Float, px: Float, py: Float) { sk.rotate(degrees, px, py) }
    fun skew(sx: Float, sy: Float) { sk.skew(sx, sy) }
    fun concat(m: Matrix?) { if (m != null) sk.concat(m.toSkia()) }

    fun clipRect(r: RectF): Boolean { sk.clipRect(r.sk()); return true }
    fun clipRect(r: Rect): Boolean { sk.clipRect(r.sk()); return true }
    fun clipRect(l: Float, t: Float, r: Float, b: Float): Boolean { sk.clipRect(org.jetbrains.skia.Rect(l, t, r, b)); return true }
    fun clipRect(l: Int, t: Int, r: Int, b: Int): Boolean = clipRect(l.toFloat(), t.toFloat(), r.toFloat(), b.toFloat())
    fun clipPath(p: Path): Boolean { sk.clipPath(p.sk, true); return true }
    fun clipOutRect(r: RectF): Boolean { sk.clipRect(r.sk(), org.jetbrains.skia.ClipMode.DIFFERENCE, true); return true }

    fun drawColor(color: Int) { sk.drawPaint(org.jetbrains.skia.Paint().apply { this.color = color; blendMode = org.jetbrains.skia.BlendMode.SRC_OVER }); touched() }
    fun drawColor(color: Int, mode: PorterDuff.Mode) { sk.drawPaint(org.jetbrains.skia.Paint().apply { this.color = color; blendMode = mode.skia() }); touched() }
    fun drawARGB(a: Int, r: Int, g: Int, b: Int) = drawColor(Color.argb(a, r, g, b))
    fun drawRGB(r: Int, g: Int, b: Int) = drawColor(Color.rgb(r, g, b))
    fun drawPaint(p: Paint) { sk.drawPaint(p.sk); touched() }

    fun drawLine(x0: Float, y0: Float, x1: Float, y1: Float, p: Paint) { sk.drawLine(x0, y0, x1, y1, p.strokeSk()); touched() }
    fun drawLines(pts: FloatArray, offset: Int, count: Int, p: Paint) {
        val paint = p.strokeSk()
        var i = offset
        while (i + 3 < offset + count) { sk.drawLine(pts[i], pts[i + 1], pts[i + 2], pts[i + 3], paint); i += 4 }
        touched()
    }
    fun drawLines(pts: FloatArray, p: Paint) = drawLines(pts, 0, pts.size, p)
    fun drawPoint(x: Float, y: Float, p: Paint) { sk.drawPoint(x, y, p.sk); touched() }
    fun drawPoints(pts: FloatArray, offset: Int, count: Int, p: Paint) {
        var i = offset; while (i + 1 < offset + count) { sk.drawPoint(pts[i], pts[i + 1], p.sk); i += 2 }; touched()
    }

    fun drawRect(r: RectF, p: Paint) { sk.drawRect(r.sk(), p.sk); touched() }
    fun drawRect(r: Rect, p: Paint) { sk.drawRect(r.sk(), p.sk); touched() }
    fun drawRect(l: Float, t: Float, r: Float, b: Float, p: Paint) { sk.drawRect(org.jetbrains.skia.Rect(l, t, r, b), p.sk); touched() }
    fun drawRoundRect(r: RectF, rx: Float, ry: Float, p: Paint) { sk.drawRRect(org.jetbrains.skia.RRect.makeLTRB(r.left, r.top, r.right, r.bottom, rx, ry), p.sk); touched() }
    fun drawRoundRect(l: Float, t: Float, r: Float, b: Float, rx: Float, ry: Float, p: Paint) = drawRoundRect(RectF(l, t, r, b), rx, ry, p)
    fun drawOval(r: RectF, p: Paint) { sk.drawOval(r.sk(), p.sk); touched() }
    fun drawOval(l: Float, t: Float, r: Float, b: Float, p: Paint) = drawOval(RectF(l, t, r, b), p)
    fun drawCircle(cx: Float, cy: Float, radius: Float, p: Paint) { sk.drawCircle(cx, cy, radius, p.sk); touched() }
    fun drawArc(oval: RectF, start: Float, sweep: Float, useCenter: Boolean, p: Paint) {
        sk.drawArc(oval.left, oval.top, oval.right, oval.bottom, start, sweep, useCenter, p.sk); touched()
    }
    fun drawPath(path: Path, p: Paint) { sk.drawPath(path.sk, p.sk); touched() }

    fun drawBitmap(b: Bitmap, left: Float, top: Float, p: Paint?) {
        b.withSkiaImage { sk.drawImage(it, left, top, p?.sk) }
        touched()
    }
    fun drawBitmap(b: Bitmap, src: Rect?, dst: RectF, p: Paint?) {
        val s = src?.sk() ?: org.jetbrains.skia.Rect.makeWH(b.width.toFloat(), b.height.toFloat())
        b.withSkiaImage { img ->
            sk.drawImageRect(img, s, dst.sk(), p.sampling(), p?.sk, true)
        }
        touched()
    }
    fun drawBitmap(b: Bitmap, src: Rect?, dst: Rect, p: Paint?) = drawBitmap(b, src, RectF(dst), p)
    fun drawBitmap(b: Bitmap, m: Matrix, p: Paint?) {
        sk.save(); sk.concat(m.toSkia()); drawBitmap(b, 0f, 0f, p); sk.restore()
    }

    fun drawText(text: String, x: Float, y: Float, p: Paint) {
        val font = p.font()
        val w = font.measureTextWidth(text, p.sk)
        val dx = when (p.textAlign) { Paint.Align.CENTER -> -w / 2f; Paint.Align.RIGHT -> -w; else -> 0f }
        sk.drawString(text, x + dx, y, font, p.sk)
        touched()
    }
    fun drawText(text: CharSequence, start: Int, end: Int, x: Float, y: Float, p: Paint) = drawText(text.subSequence(start, end).toString(), x, y, p)
    fun drawText(text: CharArray, index: Int, count: Int, x: Float, y: Float, p: Paint) = drawText(String(text, index, count), x, y, p)

    private fun Paint?.sampling(): SamplingMode =
        if (this == null || isFilterBitmap) SamplingMode.LINEAR else SamplingMode.DEFAULT
}

class Paint() {
    internal val sk = org.jetbrains.skia.Paint().apply { isAntiAlias = false }

    enum class Style { FILL, STROKE, FILL_AND_STROKE }
    enum class Cap { BUTT, ROUND, SQUARE }
    enum class Join { MITER, ROUND, BEVEL }
    enum class Align { LEFT, CENTER, RIGHT }

    constructor(flags: Int) : this() {
        isAntiAlias = flags and ANTI_ALIAS_FLAG != 0
        isFilterBitmap = flags and FILTER_BITMAP_FLAG != 0
    }
    constructor(other: Paint) : this() {
        color = other.color; style = other.style; strokeWidth = other.strokeWidth; strokeCap = other.strokeCap
        strokeJoin = other.strokeJoin; isAntiAlias = other.isAntiAlias; isFilterBitmap = other.isFilterBitmap
        textSize = other.textSize; typeface = other.typeface; textAlign = other.textAlign
    }

    var color: Int
        get() = sk.color
        set(v) { sk.color = v }
    var alpha: Int
        get() = sk.alpha
        set(v) { sk.alpha = v.coerceIn(0, 255) }
    var isAntiAlias: Boolean
        get() = sk.isAntiAlias
        set(v) { sk.isAntiAlias = v }
    var isFilterBitmap: Boolean = false
    var isDither: Boolean = false
    var strokeWidth: Float
        get() = sk.strokeWidth
        set(v) { sk.strokeWidth = v }
    var strokeMiter: Float
        get() = sk.strokeMiter
        set(v) { sk.strokeMiter = v }
    var style: Style = Style.FILL
        set(v) { field = v; sk.mode = when (v) { Style.FILL -> PaintMode.FILL; Style.STROKE -> PaintMode.STROKE; Style.FILL_AND_STROKE -> PaintMode.STROKE_AND_FILL } }
    var strokeCap: Cap = Cap.BUTT
        set(v) { field = v; sk.strokeCap = when (v) { Cap.BUTT -> PaintStrokeCap.BUTT; Cap.ROUND -> PaintStrokeCap.ROUND; Cap.SQUARE -> PaintStrokeCap.SQUARE } }
    var strokeJoin: Join = Join.MITER
        set(v) { field = v; sk.strokeJoin = when (v) { Join.MITER -> PaintStrokeJoin.MITER; Join.ROUND -> PaintStrokeJoin.ROUND; Join.BEVEL -> PaintStrokeJoin.BEVEL } }
    var textSize: Float = 12f
    var textAlign: Align = Align.LEFT
    var typeface: Typeface? = null
    var isFakeBoldText: Boolean = false
    var letterSpacing: Float = 0f
    var shader: Any? = null
    var xfermode: PorterDuffXfermode? = null
        set(v) { field = v; if (v != null) sk.blendMode = v.mode.skia() }
    var colorFilter: Any? = null
    var maskFilter: Any? = null
    var pathEffect: Any? = null
    var flags: Int = 0

    fun setARGB(a: Int, r: Int, g: Int, b: Int) { color = Color.argb(a, r, g, b) }
    fun setShadowLayer(radius: Float, dx: Float, dy: Float, color: Int) {}
    fun clearShadowLayer() {}

    internal fun font(): Font = Font(typeface?.sk ?: Typeface.DEFAULT.sk, textSize).apply {
        isEmboldened = isFakeBoldText
        isSubpixel = true
        edging = if (isAntiAlias) org.jetbrains.skia.FontEdging.ANTI_ALIAS else org.jetbrains.skia.FontEdging.ALIAS
    }

    fun measureText(text: String): Float = font().measureTextWidth(text, sk)
    fun measureText(text: CharSequence, start: Int, end: Int): Float = measureText(text.subSequence(start, end).toString())
    fun measureText(text: CharArray, index: Int, count: Int): Float = measureText(String(text, index, count))
    fun getTextBounds(text: String, start: Int, end: Int, bounds: Rect) {
        val r = font().measureText(text.substring(start, end), sk)
        bounds.set(r.left.toInt(), r.top.toInt(), kotlin.math.ceil(r.right).toInt(), kotlin.math.ceil(r.bottom).toInt())
    }
    fun breakText(text: String, measureForwards: Boolean, maxWidth: Float, measuredWidth: FloatArray?): Int {
        var n = 0
        while (n < text.length && measureText(text.substring(0, n + 1)) <= maxWidth) n++
        measuredWidth?.set(0, measureText(text.substring(0, n)))
        return n
    }

    class FontMetrics { @JvmField var top = 0f; @JvmField var ascent = 0f; @JvmField var descent = 0f; @JvmField var bottom = 0f; @JvmField var leading = 0f }
    class FontMetricsInt { @JvmField var top = 0; @JvmField var ascent = 0; @JvmField var descent = 0; @JvmField var bottom = 0; @JvmField var leading = 0 }

    val fontMetrics: FontMetrics get() = FontMetrics().also { getFontMetrics(it) }
    fun getFontMetrics(out: FontMetrics?): Float {
        val m = font().metrics
        out?.apply { top = m.top; ascent = m.ascent; descent = m.descent; bottom = m.bottom; leading = m.leading }
        return m.descent - m.ascent + m.leading
    }
    val fontMetricsInt: FontMetricsInt get() = font().metrics.let { m ->
        FontMetricsInt().apply { top = m.top.toInt(); ascent = m.ascent.toInt(); descent = m.descent.toInt(); bottom = m.bottom.toInt(); leading = m.leading.toInt() }
    }
    fun ascent(): Float = font().metrics.ascent
    fun descent(): Float = font().metrics.descent
    val fontSpacing: Float get() = getFontMetrics(null)

    /** O traço: o Android desenha linhas com STROKE mesmo com estilo FILL. */
    internal fun strokeSk(): org.jetbrains.skia.Paint =
        if (style == Style.FILL) org.jetbrains.skia.Paint().also { p ->
            p.color = sk.color; p.isAntiAlias = sk.isAntiAlias; p.strokeWidth = sk.strokeWidth
            p.strokeCap = sk.strokeCap; p.mode = PaintMode.STROKE; p.blendMode = sk.blendMode
        } else sk

    companion object {
        const val ANTI_ALIAS_FLAG = 1
        const val FILTER_BITMAP_FLAG = 2
        const val DITHER_FLAG = 4
        const val UNDERLINE_TEXT_FLAG = 8
        const val STRIKE_THRU_TEXT_FLAG = 16
        const val FAKE_BOLD_TEXT_FLAG = 32
        const val LINEAR_TEXT_FLAG = 64
        const val SUBPIXEL_TEXT_FLAG = 128
    }
}

class Typeface private constructor(internal val sk: org.jetbrains.skia.Typeface, val style: Int) {
    val isBold: Boolean get() = style and BOLD != 0
    val isItalic: Boolean get() = style and ITALIC != 0
    val weight: Int get() = sk.fontStyle.weight
    /** A fonte do arquivo, para o Compose (FontFamily(Typeface)). */
    internal var sourcePath: String? = null

    companion object {
        const val NORMAL = 0; const val BOLD = 1; const val ITALIC = 2; const val BOLD_ITALIC = 3
        private val mgr = org.jetbrains.skia.FontMgr.default
        @JvmField val DEFAULT: Typeface = Typeface(mgr.matchFamilyStyle("Segoe UI", org.jetbrains.skia.FontStyle.NORMAL) ?: mgr.legacyMakeTypeface("", org.jetbrains.skia.FontStyle.NORMAL)!!, NORMAL)
        @JvmField val DEFAULT_BOLD: Typeface = Typeface(mgr.matchFamilyStyle("Segoe UI", org.jetbrains.skia.FontStyle.BOLD) ?: DEFAULT.sk, BOLD)
        @JvmField val SANS_SERIF: Typeface = DEFAULT
        @JvmField val SERIF: Typeface = Typeface(mgr.matchFamilyStyle("Times New Roman", org.jetbrains.skia.FontStyle.NORMAL) ?: DEFAULT.sk, NORMAL)
        @JvmField val MONOSPACE: Typeface = Typeface(mgr.matchFamilyStyle("Consolas", org.jetbrains.skia.FontStyle.NORMAL) ?: DEFAULT.sk, NORMAL)

        @JvmStatic fun createFromFile(path: String): Typeface =
            Typeface(mgr.makeFromFile(path, 0) ?: throw RuntimeException("fonte inválida: $path"), NORMAL).also { it.sourcePath = path }
        @JvmStatic fun createFromFile(file: java.io.File): Typeface = createFromFile(file.path)
        @JvmStatic fun createFromAsset(assets: android.content.res.AssetManager, path: String): Typeface {
            val bytes = assets.open(path).use { it.readBytes() }
            return Typeface(mgr.makeFromData(org.jetbrains.skia.Data.makeFromBytes(bytes)) ?: DEFAULT.sk, NORMAL)
        }
        @JvmStatic fun create(family: Typeface?, style: Int): Typeface = family ?: DEFAULT
        @JvmStatic fun create(family: String?, style: Int): Typeface =
            Typeface(mgr.matchFamilyStyle(family, if (style and BOLD != 0) org.jetbrains.skia.FontStyle.BOLD else org.jetbrains.skia.FontStyle.NORMAL) ?: DEFAULT.sk, style)
        @JvmStatic fun create(family: Typeface?, weight: Int, italic: Boolean): Typeface = family ?: DEFAULT
        @JvmStatic fun defaultFromStyle(style: Int): Typeface = if (style and BOLD != 0) DEFAULT_BOLD else DEFAULT
    }
}

class PorterDuff {
    enum class Mode { CLEAR, SRC, DST, SRC_OVER, DST_OVER, SRC_IN, DST_IN, SRC_OUT, DST_OUT, SRC_ATOP, DST_ATOP, XOR, DARKEN, LIGHTEN, MULTIPLY, SCREEN, ADD, OVERLAY;
        internal fun skia(): org.jetbrains.skia.BlendMode = when (this) {
            CLEAR -> org.jetbrains.skia.BlendMode.CLEAR; SRC -> org.jetbrains.skia.BlendMode.SRC; DST -> org.jetbrains.skia.BlendMode.DST
            SRC_OVER -> org.jetbrains.skia.BlendMode.SRC_OVER; DST_OVER -> org.jetbrains.skia.BlendMode.DST_OVER
            SRC_IN -> org.jetbrains.skia.BlendMode.SRC_IN; DST_IN -> org.jetbrains.skia.BlendMode.DST_IN
            SRC_OUT -> org.jetbrains.skia.BlendMode.SRC_OUT; DST_OUT -> org.jetbrains.skia.BlendMode.DST_OUT
            SRC_ATOP -> org.jetbrains.skia.BlendMode.SRC_ATOP; DST_ATOP -> org.jetbrains.skia.BlendMode.DST_ATOP
            XOR -> org.jetbrains.skia.BlendMode.XOR; DARKEN -> org.jetbrains.skia.BlendMode.DARKEN; LIGHTEN -> org.jetbrains.skia.BlendMode.LIGHTEN
            MULTIPLY -> org.jetbrains.skia.BlendMode.MULTIPLY; SCREEN -> org.jetbrains.skia.BlendMode.SCREEN
            ADD -> org.jetbrains.skia.BlendMode.PLUS; OVERLAY -> org.jetbrains.skia.BlendMode.OVERLAY
        }
    }
}

class PorterDuffXfermode(val mode: PorterDuff.Mode)
